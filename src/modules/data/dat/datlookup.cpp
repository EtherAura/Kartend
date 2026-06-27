// DAT-file parsers (Logiqx + MAME listxml dialects) + indexed lookup
// store. Streaming XML via QXmlStreamReader so multi-MB DATs don't pin
// memory beyond the parsed-record list itself.
#include "datlookup.h"

#include <optional>
#include <QFile>
#include <QFileInfo>
#include <QStringDecoder>
#include <QStringView>
#include <QTemporaryDir>
#include <QXmlStreamReader>

#include "nointrodownloader.h" // extractDatsTo, for transparent .zip-packed DATs (Kartend-m6qsb.28)

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace DatLookup {

namespace {

// Defensive ingest bounds for untrusted DAT input (Kartend-4bouw). DATs arrive
// from both the network download path and arbitrary user-selected files, so a
// hostile or corrupt file must not be able to OOM the process or pollute the
// cache with absurd field values.
//
// kMaxDatBytes caps a full-file read: MAME's listxml output tops out around
// ~150MB, so 512MB leaves generous margin above any legitimate catalogue while
// still rejecting a multi-gigabyte file mislabelled `.dat`.
constexpr qint64 kMaxDatBytes = 512LL * 1024 * 1024;
// kMaxNameLen bounds a parsed game/rom name before it reaches SQLite. Real
// names are well under a few hundred chars; 4096 rejects megabyte-scale name
// blobs without clipping any genuine entry.
constexpr int kMaxNameLen = 4096;
// kMaxRomSize bounds a parsed `size` field. 1TiB is far above any real ROM /
// disc image while rejecting nonsensical (overflowed / garbage) values.
constexpr qint64 kMaxRomSize = 1LL << 40;
// kProbeHeaderBytes caps the cheap header sniff used to pick a DAT dialect
// before a full parse — a 256 KiB window is ample for the root element plus a
// handful of entries (Kartend audit D-10).
constexpr qint64 kProbeHeaderBytes = 256 * 1024;

/// Decode raw DAT bytes to a QString with a UTF-8-then-Latin-1 fallback
/// (Kartend-a3s01). clrmamepro DATs (notably TOSEC and older European
/// catalogues) are frequently Latin-1 / Windows-1252 with no encoding
/// declaration; forcing them through UTF-8 turns every non-ASCII byte into
/// U+FFFD and garbles the canonical name. Latin-1 never fails (every byte maps
/// to a code point) and is the correct interpretation for those catalogues;
/// valid UTF-8 still decodes identically via the first attempt. Used by the
/// clrmamepro text/tokeniser path only — the XML path lets QXmlStreamReader
/// honour the document's encoding declaration.
QString decodeDatText(const QByteArray &bytes) {
  QStringDecoder utf8(QStringDecoder::Utf8);
  QString s = utf8.decode(bytes);
  if (utf8.hasError()) {
    return QString::fromLatin1(bytes);
  }
  return s;
}

/// Lowercase + strip surrounding whitespace, leaving a clean hex
/// digest ready for hash-map lookup. Defensive about input source —
/// some DAT producers emit upper-case hashes, some have stray
/// whitespace inside attribute values.
QString normaliseHex(QStringView raw) {
  const QString s = raw.trimmed().toString().toLower();
  if (s.isEmpty()) {
    return QString();
  }
  // Reject anything that isn't a valid hex string of an expected hash length
  // (CRC32=8, MD5=32, SHA1=40). A malformed or wrong-length value (a truncated
  // field, or a non-hex sentinel) would otherwise become a bogus index key and
  // mis-identify files (Kartend-23o5e).
  if (s.size() != 8 && s.size() != 32 && s.size() != 40) {
    return QString();
  }
  for (const QChar c : s) {
    const bool hex = (c >= QLatin1Char('0') && c <= QLatin1Char('9')) ||
                     (c >= QLatin1Char('a') && c <= QLatin1Char('f'));
    if (!hex) {
      return QString();
    }
  }
  return s;
}

/// True when a game/machine/rom carries the `mia="yes"` flag (Kartend-34lab).
/// Tolerant of `yes`/`true`/`1` and case so the three dialects' conventions
/// all read as MIA.
bool miaFromAttrs(const QXmlStreamAttributes &attrs) {
  const QString v = attrs.value(QLatin1String("mia")).toString().trimmed().toLower();
  return v == QLatin1String("yes") || v == QLatin1String("true") || v == QLatin1String("1");
}

/// Build a DatRecord from a `<rom>` element's attributes. Returns
/// nullopt when the entry has no usable hash (parse-time drop — an
/// entry without any hash can't be reached from a lookup query).
std::optional<DatRecord> readRomElement(const QXmlStreamAttributes &attrs, const QString &gameName,
                                        const QString &cloneOf = {}, bool gameMia = false) {
  // MAME marks placeholder roms with `status="nodump"` (the chip is
  // known to exist but no good dump is available). These entries
  // carry zeroed-out hashes that would otherwise collide with every
  // legitimately-zero-hash file — drop them at parse time.
  const QStringView status = attrs.value(QLatin1String("status"));
  if (status == QLatin1String("nodump")) return std::nullopt;

  DatRecord r;
  r.gameName = gameName;
  r.cloneOf = cloneOf;
  // Logiqx's <game name> is both the title and the set-id; for the MAME path
  // (called with an empty gameName, resolved later in flushMachine) this stays
  // empty and flushMachine sets setId from the machine name= (Kartend-m6qsb.29).
  r.setId = gameName;
  // MIA can be declared on the game/machine (applies to all its roms) or on
  // the individual <rom>; either marks this entry MIA.
  r.mia = gameMia || miaFromAttrs(attrs);
  r.romName = attrs.value(QLatin1String("name")).toString();
  // Reject absurd name lengths before they reach the SQLite insert (Kartend-4bouw):
  // a megabyte-scale name blob in a hostile/corrupt DAT would bloat the cache.
  if (r.romName.size() > kMaxNameLen || gameName.size() > kMaxNameLen) {
    return std::nullopt;
  }
  const QStringView sizeStr = attrs.value(QLatin1String("size"));
  if (!sizeStr.isEmpty()) {
    bool ok = false;
    const qint64 sz = sizeStr.toString().toLongLong(&ok);
    // Reject negative and absurdly large sizes (Kartend-4bouw): a corrupt DAT
    // can carry a nonsensical or overflowed `size` that would corrupt
    // downstream lookups. Out-of-range leaves r.size at the -1 "undeclared"
    // sentinel.
    if (ok && sz >= 0 && sz <= kMaxRomSize) r.size = sz;
  }
  r.crc = normaliseHex(attrs.value(QLatin1String("crc")));
  r.md5 = normaliseHex(attrs.value(QLatin1String("md5")));
  r.sha1 = normaliseHex(attrs.value(QLatin1String("sha1")));
  if (r.crc.isEmpty() && r.md5.isEmpty() && r.sha1.isEmpty()) {
    return std::nullopt;
  }
  return r;
}

/// The parent set named by a game/machine's `cloneof` (preferred) or `romof`
/// attribute (Kartend-m6qsb.13). Empty for a parent / non-clone set.
QString cloneOfFromAttrs(const QXmlStreamAttributes &attrs) {
  QString c = attrs.value(QLatin1String("cloneof")).toString();
  if (c.isEmpty()) {
    c = attrs.value(QLatin1String("romof")).toString();
  }
  return c;
}

/// Standard parse-error builder so both parsers report errors with
/// the same shape (caller surfaces line/column to the user).
ErrorContext makeParseError(const QXmlStreamReader &reader, const char *context) {
  return ErrorContext::error(ErrorCode::InvalidArgument, "DAT file XML parse error", context)
      .withDetails(QStringLiteral("%1 (line %2, col %3)")
                       .arg(reader.errorString())
                       .arg(reader.lineNumber())
                       .arg(reader.columnNumber()));
}

// Post-parse truncation guard shared by the Logiqx / MAME-listxml readers:
// surface a QXmlStreamReader error, else reject a stream that reached atEnd()
// without its closing root tag — a mid-stream truncation a well-formed prefix
// can't otherwise be told apart from (Kartend-u4sdu). nullopt when the parse is
// clean (Kartend audit D-10).
std::optional<ErrorContext> datParseTailError(const QXmlStreamReader &reader, bool sawClosingRoot,
                                              const char *formatLabel, const char *rootTag,
                                              const char *context) {
  if (reader.hasError()) {
    return makeParseError(reader, context);
  }
  if (!sawClosingRoot) {
    return ErrorContext::error(
        ErrorCode::InvalidArgument,
        QStringLiteral("%1 ended without its closing %2 — file is truncated or incomplete")
            .arg(QString::fromLatin1(formatLabel), QString::fromLatin1(rootTag)),
        context);
  }
  return std::nullopt;
}

// --- clrmamepro text dialect ----------------------------------------------
//
// The clrmamepro format is a flat token soup of words, quoted strings, and
// `(`/`)` block delimiters — e.g.
//   game ( name "Title (USA)" rom ( name "Title (USA).bin" size 4 crc abcd1234 ) )
// We tokenise the whole buffer once, then walk the tokens. There are no
// comments or escape sequences in practice, so a quote runs to the next quote
// and a bareword runs to the next whitespace/paren/quote.

struct CmpToken {
  enum Kind { Word, String, Open, Close } kind = Word;
  QString text; // Word / String payload; empty for Open / Close
};

// Bounded-memory note (Kartend audit SEC-04): unlike the XML path — which
// streams via QXmlStreamReader and never pins the whole document — the
// clrmamepro path decodes the entire buffer to a QString (decodeDatText) and
// materializes a full QList<CmpToken> before parsing. Both are bounded by the
// kMaxDatBytes (512 MiB) read cap in readDatFile, so the worst case is a
// bounded transient spike (~2-3x the file size) on a pathologically large
// clrmamepro DAT; real clrmamepro catalogues (TOSEC / European sets) are small,
// so this is a worst-case-input concern, not a runtime one. If it ever matters,
// stream-tokenise here or apply a smaller dialect-specific cap before decode.
QList<CmpToken> tokenizeClrMamePro(const QString &s) {
  QList<CmpToken> toks;
  const int n = s.size();
  int i = 0;
  while (i < n) {
    const QChar c = s.at(i);
    if (c.isSpace()) {
      ++i;
    } else if (c == u'(') {
      toks.append(CmpToken{CmpToken::Open, {}});
      ++i;
    } else if (c == u')') {
      toks.append(CmpToken{CmpToken::Close, {}});
      ++i;
    } else if (c == u'"') {
      ++i; // opening quote
      QString val;
      while (i < n && s.at(i) != u'"') {
        val += s.at(i);
        ++i;
      }
      if (i < n) ++i; // closing quote (tolerate an unterminated trailing string)
      toks.append(CmpToken{CmpToken::String, val});
    } else {
      QString w;
      while (i < n) {
        const QChar wc = s.at(i);
        if (wc.isSpace() || wc == u'(' || wc == u')' || wc == u'"') break;
        w += wc;
        ++i;
      }
      toks.append(CmpToken{CmpToken::Word, w});
    }
  }
  return toks;
}

/// Read the value token (Word or String) following a key, advancing `i`.
/// Returns empty without consuming when the next token is a paren — a key
/// with no scalar value.
QString cmpNextValue(const QList<CmpToken> &toks, int &i) {
  if (i < toks.size() &&
      (toks.at(i).kind == CmpToken::Word || toks.at(i).kind == CmpToken::String)) {
    return toks.at(i++).text;
  }
  return QString();
}

/// Skip from just-after an `(` to just-past its matching `)`, honouring nesting.
void cmpSkipBlock(const QList<CmpToken> &toks, int &i) {
  const int n = toks.size();
  int depth = 1;
  while (i < n && depth > 0) {
    if (toks.at(i).kind == CmpToken::Open)
      ++depth;
    else if (toks.at(i).kind == CmpToken::Close)
      --depth;
    ++i;
  }
}

/// Parse a `rom ( … )` body (called with `i` just after the `(`); leaves `i`
/// just past the matching `)`. Mirrors readRomElement's drop rules.
std::optional<DatRecord> cmpParseRom(const QList<CmpToken> &toks, int &i) {
  const int n = toks.size();
  DatRecord r;
  QString status;
  while (i < n && toks.at(i).kind != CmpToken::Close) {
    if (toks.at(i).kind == CmpToken::Word) {
      const QString key = toks.at(i).text.toLower();
      ++i;
      const QString val = cmpNextValue(toks, i);
      if (key == QLatin1String("name"))
        r.romName = val;
      else if (key == QLatin1String("size")) {
        bool ok = false;
        const qint64 sz = val.toLongLong(&ok);
        // Reject negative / absurd sizes, matching readRomElement (Kartend-4bouw).
        if (ok && sz >= 0 && sz <= kMaxRomSize) r.size = sz;
      } else if (key == QLatin1String("crc"))
        r.crc = normaliseHex(val);
      else if (key == QLatin1String("md5"))
        r.md5 = normaliseHex(val);
      else if (key == QLatin1String("sha1"))
        r.sha1 = normaliseHex(val);
      else if (key == QLatin1String("status") || key == QLatin1String("flags"))
        status = val.toLower();
      else if (key == QLatin1String("mia"))
        r.mia = (val.compare(QLatin1String("yes"), Qt::CaseInsensitive) == 0); // (Kartend-34lab)
      // else: date / merge / bios / region / … — irrelevant to lookup.
    } else if (toks.at(i).kind == CmpToken::Open) {
      ++i;
      cmpSkipBlock(toks, i); // defensive: no nested blocks expected inside rom
    } else {
      ++i;
    }
  }
  if (i < n) ++i; // consume the rom block's ')'
  if (status == QLatin1String("nodump")) return std::nullopt;
  // Reject absurd rom-name lengths before SQLite insert (Kartend-4bouw).
  if (r.romName.size() > kMaxNameLen) return std::nullopt;
  if (r.crc.isEmpty() && r.md5.isEmpty() && r.sha1.isEmpty()) return std::nullopt;
  return r;
}

/// Parse a `game ( … )` / `resource ( … )` body (called with `i` just after
/// the `(`); appends its roms to `out` and leaves `i` past the matching `)`.
/// The block's `name` becomes the gameName (matching the Logiqx parser, which
/// uses the `<game name>` attribute); `description` and other fields are read
/// past but unused here.
void cmpParseGame(const QList<CmpToken> &toks, int &i, QList<DatRecord> &out) {
  const int n = toks.size();
  QString gameName;
  QString cloneOf;      // cloneof, falling back to romof (Kartend-m6qsb.13)
  bool gameMia = false; // game-level `mia yes` (Kartend-34lab)
  QList<DatRecord> roms;
  while (i < n && toks.at(i).kind != CmpToken::Close) {
    if (toks.at(i).kind == CmpToken::Word) {
      const QString key = toks.at(i).text.toLower();
      ++i;
      if (key == QLatin1String("rom")) {
        if (i < n && toks.at(i).kind == CmpToken::Open) {
          ++i;
          if (auto r = cmpParseRom(toks, i)) roms.append(std::move(*r));
        }
      } else if (i < n && toks.at(i).kind == CmpToken::Open) {
        // A nested block we don't index (disk / sample / archive / …).
        ++i;
        cmpSkipBlock(toks, i);
      } else {
        const QString val = cmpNextValue(toks, i);
        if (key == QLatin1String("name"))
          gameName = val;
        else if (key == QLatin1String("cloneof"))
          cloneOf = val;
        else if (key == QLatin1String("romof") && cloneOf.isEmpty())
          cloneOf = val;
        else if (key == QLatin1String("mia"))
          gameMia = (val.compare(QLatin1String("yes"), Qt::CaseInsensitive) == 0);
      }
    } else if (toks.at(i).kind == CmpToken::Open) {
      ++i;
      cmpSkipBlock(toks, i);
    } else {
      ++i;
    }
  }
  if (i < n) ++i; // consume the game block's ')'
  // Reject an absurd game-name length before emitting any of its roms
  // (Kartend-4bouw) — the name would otherwise reach every record's SQLite insert.
  if (gameName.size() > kMaxNameLen) {
    return;
  }
  for (auto &r : roms) {
    r.gameName = gameName;
    r.cloneOf = cloneOf;
    r.setId = gameName;       // clrmamepro's `name` is title + set-id (Kartend-m6qsb.29)
    r.mia = r.mia || gameMia; // OR game-level MIA over any rom-level flag
    out.append(std::move(r));
  }
}

/// Cheap signature check used by `detectDialect` once the bytes have failed to
/// parse as XML: the first word must be a known clrmamepro block keyword
/// immediately followed by `(`.
bool looksLikeClrMamePro(const QByteArray &bytes) {
  const QString s = decodeDatText(bytes);
  const int n = s.size();
  int i = 0;
  while (i < n && s.at(i).isSpace()) ++i;
  QString w;
  while (i < n && !s.at(i).isSpace() && s.at(i) != u'(' && s.at(i) != u'"') {
    w += s.at(i);
    ++i;
  }
  while (i < n && s.at(i).isSpace()) ++i;
  const bool hasOpen = (i < n && s.at(i) == u'(');
  w = w.toLower();
  return hasOpen && (w == QLatin1String("clrmamepro") || w == QLatin1String("emulator") ||
                     w == QLatin1String("game") || w == QLatin1String("machine") ||
                     w == QLatin1String("set") || w == QLatin1String("resource"));
}

} // namespace

Dialect detectDialect(const QByteArray &xml) {
  // Stop at the first start element — we just need the root tag.
  // Cheap even on a 100MB MAME listxml because QXmlStreamReader
  // streams from the byte buffer and doesn't materialise the tree.
  QXmlStreamReader reader(xml);
  while (!reader.atEnd()) {
    const auto token = reader.readNext();
    if (reader.hasError()) break; // not well-formed XML — fall through to the text sniff
    if (token != QXmlStreamReader::StartElement) continue;
    const QStringView name = reader.name();
    if (name == QLatin1String("datafile")) return Dialect::Logiqx;
    if (name == QLatin1String("mame")) return Dialect::Mame;
    return Dialect::Unknown; // recognised as XML, but not a DAT root we know
  }
  // Reached end without a recognised XML root, or the bytes aren't XML at all:
  // the clrmamepro text format is the remaining possibility.
  if (looksLikeClrMamePro(xml)) return Dialect::ClrMamePro;
  return Dialect::Unknown;
}

DatHeader probeHeader(const QByteArray &xml) {
  return probeHeader(xml, detectDialect(xml));
}

DatHeader probeHeader(const QByteArray &xml, Dialect dialect) {
  DatHeader out;

  // clrmamepro text: read the leading `clrmamepro (`/`emulator (` block's
  // name / description / version, then stop before the game blocks.
  if (dialect == Dialect::ClrMamePro) {
    out.dialect = Dialect::ClrMamePro;
    const QList<CmpToken> toks = tokenizeClrMamePro(decodeDatText(xml));
    const int n = toks.size();
    int i = 0;
    while (i < n) {
      if (toks.at(i).kind != CmpToken::Word) {
        ++i;
        continue;
      }
      const QString kind = toks.at(i).text.toLower();
      ++i;
      if (i >= n || toks.at(i).kind != CmpToken::Open) continue;
      ++i; // consume '('
      if (kind == QLatin1String("clrmamepro") || kind == QLatin1String("emulator")) {
        while (i < n && toks.at(i).kind != CmpToken::Close) {
          if (toks.at(i).kind == CmpToken::Word) {
            const QString key = toks.at(i).text.toLower();
            ++i;
            const QString val = cmpNextValue(toks, i);
            if (key == QLatin1String("name"))
              out.name = val;
            else if (key == QLatin1String("description"))
              out.description = val;
            else if (key == QLatin1String("version"))
              out.version = val;
          } else {
            ++i;
          }
        }
        break; // header block done; everything after is records
      }
      cmpSkipBlock(toks, i); // a game block before any header — skip it
    }
    out.name = out.name.trimmed();
    out.description = out.description.trimmed();
    out.version = out.version.trimmed();
    return out;
  }

  QXmlStreamReader reader(xml);
  bool sawRoot = false;
  bool inHeader = false;
  // Accumulates the current captured text element; nullptr between them.
  QString *capture = nullptr;

  while (!reader.atEnd()) {
    const auto token = reader.readNext();
    if (reader.hasError()) break; // keep whatever was captured before the error
    if (token == QXmlStreamReader::StartElement) {
      const QStringView name = reader.name();
      if (!sawRoot) {
        sawRoot = true;
        if (name == QLatin1String("mame")) {
          // MAME listxml carries no <header>; the build attribute is the
          // only identifying metadata it has.
          out.dialect = Dialect::Mame;
          out.name = QStringLiteral("MAME");
          out.version = reader.attributes().value(QLatin1String("build")).toString().trimmed();
          return out;
        }
        if (name != QLatin1String("datafile")) {
          return out; // Unknown dialect — nothing to probe
        }
        out.dialect = Dialect::Logiqx;
        continue;
      }
      if (name == QLatin1String("header")) {
        inHeader = true;
      } else if (!inHeader) {
        // First non-header child of <datafile> (a <game>) — this DAT has
        // no header, and one can't appear later. Stop before the records.
        break;
      } else if (name == QLatin1String("name")) {
        capture = &out.name;
      } else if (name == QLatin1String("description")) {
        capture = &out.description;
      } else if (name == QLatin1String("version")) {
        capture = &out.version;
      } else {
        capture = nullptr; // header child we don't keep (author, url, …)
      }
    } else if (token == QXmlStreamReader::Characters && capture != nullptr) {
      *capture += reader.text().toString();
    } else if (token == QXmlStreamReader::EndElement) {
      const QStringView name = reader.name();
      if (name == QLatin1String("header")) {
        break; // got everything the header holds
      }
      capture = nullptr;
    }
  }

  out.name = out.name.trimmed();
  out.description = out.description.trimmed();
  out.version = out.version.trimmed();
  return out;
}

DatHeader probeHeaderFromFile(const QString &path) {
  // 256 KiB is orders of magnitude beyond any real Logiqx <header> (a few
  // hundred bytes) while keeping a probe over a folder of 100MB listxmls
  // cheap. A header truncated mid-element just yields the fields captured up to
  // the cut — fine for a suggestion signal. A .zip-packed DAT is unpacked whole
  // (small) by readDatFile regardless of the cap (Kartend-m6qsb.28).
  const QByteArray head = readDatFile(path, kProbeHeaderBytes);
  if (head.isEmpty()) {
    return DatHeader{};
  }
  return probeHeader(head);
}

ErrorUtils::Result<QList<DatRecord>> parseLogiqxDat(const QByteArray &xml) {
  QXmlStreamReader reader(xml);
  QList<DatRecord> out;

  // Track the current `<game>` so we can attach `<rom>` entries to
  // it. A single game can carry multiple roms (multi-disc / multi-
  // chip dumps) — we emit one DatRecord per rom, all sharing the
  // game's name. `<machine>` is accepted as a synonym so a Logiqx
  // export from MAME-WIP tools still works (real MAME listxml uses
  // the `<mame>` root → goes through parseMameListXml instead).
  QString currentGameName;
  QString currentCloneOf;  // Logiqx exports may carry cloneof/romof (Kartend-m6qsb.13)
  bool currentMia = false; // game-level mia="yes" (Kartend-34lab)
  // Truncation guard (Kartend-u4sdu): a well-formed-but-truncated file (a
  // partial download cut at a tag boundary) reaches atEnd() with hasError()
  // false, so QXmlStreamReader alone can't tell it from a complete document.
  // Track the closing `</datafile>` root so we can reject a stream that ended
  // without it.
  bool sawClosingRoot = false;

  while (!reader.atEnd()) {
    const auto token = reader.readNext();
    if (reader.hasError()) break;
    if (token == QXmlStreamReader::EndElement) {
      if (reader.name() == QLatin1String("datafile")) sawClosingRoot = true;
      continue;
    }
    if (token != QXmlStreamReader::StartElement) continue;
    const QStringView name = reader.name();

    if (name == QLatin1String("game") || name == QLatin1String("machine")) {
      currentGameName = reader.attributes().value(QLatin1String("name")).toString();
      currentCloneOf = cloneOfFromAttrs(reader.attributes());
      currentMia = miaFromAttrs(reader.attributes());
    } else if (name == QLatin1String("rom") && !currentGameName.isEmpty()) {
      if (auto r =
              readRomElement(reader.attributes(), currentGameName, currentCloneOf, currentMia)) {
        out.append(std::move(*r));
      }
    }
    // Note: TOSEC's optional `<release>` child of `<game>` is
    // ignored on purpose — it carries region/language/date metadata
    // and no hashes, so it has nothing to contribute to a lookup
    // index. The default-branch fall-through here is the entire
    // handling needed.
  }

  if (auto err = datParseTailError(reader, sawClosingRoot, "Logiqx DAT", "</datafile>",
                                   "DatLookup::parseLogiqxDat")) {
    return *err;
  }
  return out;
}

ErrorUtils::Result<QList<DatRecord>> parseMameListXml(const QByteArray &xml) {
  QXmlStreamReader reader(xml);
  QList<DatRecord> out;

  // MAME listxml differs from Logiqx in two ways we care about:
  // 1. The friendly title lives in a `<description>` child element,
  //    not the `name=` attribute (which is a terse set-id like
  //    "pacman"). We capture the description text between
  //    `<machine>` start and end and use it as the gameName,
  //    falling back to the set-id if `<description>` is absent.
  // 2. `<rom status="nodump">` markers exist — handled in
  //    readRomElement above.
  QString currentSetId;
  QString currentDescription;
  QString currentCloneOf;  // cloneof/romof parent set (Kartend-m6qsb.13)
  bool currentMia = false; // machine-level mia="yes" (Kartend-34lab)
  bool inDescription = false;
  // Buffer roms seen so far for the current machine; flushed with
  // the resolved gameName at `</machine>` so any rom that appears
  // before `<description>` still gets the nice name.
  QList<DatRecord> pendingRoms;

  auto flushMachine = [&]() {
    const QString gameName = !currentDescription.isEmpty() ? currentDescription : currentSetId;
    for (auto &r : pendingRoms) {
      r.gameName = gameName;
      r.cloneOf = currentCloneOf;
      // MAME: gameName is the <description>, but cloneof references the parent's
      // machine name= — so set-id must be the raw name=, not gameName (m6qsb.29).
      r.setId = currentSetId;
      // OR the machine-level MIA over any rom-level flag readRomElement already set.
      r.mia = r.mia || currentMia;
      out.append(std::move(r));
    }
    pendingRoms.clear();
    currentSetId.clear();
    currentDescription.clear();
    currentCloneOf.clear();
    currentMia = false;
  };

  // Truncation guard (Kartend-u4sdu): track the closing `</mame>` so a
  // well-formed-but-truncated listxml (cut at a tag boundary, reaching atEnd()
  // with no error) is rejected instead of returning a partial record set.
  bool sawClosingRoot = false;

  while (!reader.atEnd()) {
    const auto token = reader.readNext();
    if (reader.hasError()) break;
    if (token == QXmlStreamReader::StartElement) {
      const QStringView name = reader.name();
      if (name == QLatin1String("machine") || name == QLatin1String("game")) {
        // Some old MAME builds emit `<game>` here; treat it the same.
        currentSetId = reader.attributes().value(QLatin1String("name")).toString();
        currentCloneOf = cloneOfFromAttrs(reader.attributes());
        currentMia = miaFromAttrs(reader.attributes());
        currentDescription.clear();
        pendingRoms.clear();
      } else if (name == QLatin1String("description")) {
        inDescription = true;
      } else if (name == QLatin1String("rom") && !currentSetId.isEmpty()) {
        if (auto r = readRomElement(reader.attributes(), QString())) {
          // gameName left blank here; set in flushMachine() below.
          pendingRoms.append(std::move(*r));
        }
      }
    } else if (token == QXmlStreamReader::Characters && inDescription) {
      currentDescription += reader.text().toString();
    } else if (token == QXmlStreamReader::EndElement) {
      const QStringView name = reader.name();
      if (name == QLatin1String("description")) {
        inDescription = false;
        currentDescription = currentDescription.trimmed();
      } else if (name == QLatin1String("machine") || name == QLatin1String("game")) {
        flushMachine();
      } else if (name == QLatin1String("mame")) {
        sawClosingRoot = true;
      }
    }
  }

  if (auto err = datParseTailError(reader, sawClosingRoot, "MAME listxml", "</mame>",
                                   "DatLookup::parseMameListXml")) {
    return *err;
  }
  return out;
}

ErrorUtils::Result<QList<DatRecord>> parseClrMameProDat(const QByteArray &xml) {
  const QList<CmpToken> toks = tokenizeClrMamePro(decodeDatText(xml));
  QList<DatRecord> out;
  const int n = toks.size();
  int i = 0;
  while (i < n) {
    if (toks.at(i).kind != CmpToken::Word) {
      ++i;
      continue;
    }
    const QString kind = toks.at(i).text.toLower();
    ++i;
    if (i >= n || toks.at(i).kind != CmpToken::Open) continue; // a bare word, not a block
    ++i;                                                       // consume '('
    if (kind == QLatin1String("game") || kind == QLatin1String("machine") ||
        kind == QLatin1String("set") || kind == QLatin1String("resource")) {
      cmpParseGame(toks, i, out);
    } else {
      cmpSkipBlock(toks, i); // header (clrmamepro/emulator) or anything else
    }
  }
  return out;
}

ErrorUtils::Result<QList<DatRecord>> parseDat(const QByteArray &xml) {
  return parseDat(xml, detectDialect(xml));
}

ErrorUtils::Result<QList<DatRecord>> parseDat(const QByteArray &xml, Dialect dialect) {
  switch (dialect) {
  case Dialect::Logiqx:
    return parseLogiqxDat(xml);
  case Dialect::Mame:
    return parseMameListXml(xml);
  case Dialect::ClrMamePro:
    return parseClrMameProDat(xml);
  case Dialect::Unknown:
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "Unrecognised DAT format — expected <datafile> (No-Intro / "
                               "Redump / TOSEC), <mame> (MAME listxml), or a clrmamepro "
                               "text DAT",
                               "DatLookup::parseDat");
  }
  Q_UNREACHABLE();
}

Store::Store(QList<DatRecord> records, Dialect dialect)
    : m_records(std::move(records)), m_dialect(dialect) {
  m_bySha1.reserve(m_records.size());
  m_byMd5.reserve(m_records.size());
  m_byCrc.reserve(m_records.size());
  for (int i = 0; i < m_records.size(); ++i) {
    const DatRecord &r = m_records[i];
    // First-seen wins on collision — stable across reloads of the
    // same DAT and matches what most DAT tools do when two
    // entries declare the same hash (typically only seen in
    // hand-edited DATs).
    if (!r.sha1.isEmpty() && !m_bySha1.contains(r.sha1)) m_bySha1.insert(r.sha1, i);
    if (!r.md5.isEmpty() && !m_byMd5.contains(r.md5)) m_byMd5.insert(r.md5, i);
    if (!r.crc.isEmpty() && !m_byCrc.contains(r.crc)) m_byCrc.insert(r.crc, i);
  }
}

const DatRecord *Store::lookup(const QString &md5, const QString &sha1, const QString &crc) const {
  // SHA-1 is the most reliable (collisions infeasible at this
  // scale); MD5 is next; CRC32 has real collision risk and is the
  // last resort. Caller passes whatever it has — an empty hash is
  // skipped silently.
  if (auto *r = lookupBySha1(sha1)) return r;
  if (auto *r = lookupByMd5(md5)) return r;
  if (auto *r = lookupByCrc(crc)) return r;
  return nullptr;
}

const DatRecord *Store::lookupBySha1(const QString &sha1) const {
  if (sha1.isEmpty()) return nullptr;
  const auto it = m_bySha1.constFind(sha1.toLower());
  if (it == m_bySha1.constEnd()) return nullptr;
  return &m_records[it.value()];
}

const DatRecord *Store::lookupByMd5(const QString &md5) const {
  if (md5.isEmpty()) return nullptr;
  const auto it = m_byMd5.constFind(md5.toLower());
  if (it == m_byMd5.constEnd()) return nullptr;
  return &m_records[it.value()];
}

const DatRecord *Store::lookupByCrc(const QString &crc) const {
  if (crc.isEmpty()) return nullptr;
  const auto it = m_byCrc.constFind(crc.toLower());
  if (it == m_byCrc.constEnd()) return nullptr;
  return &m_records[it.value()];
}

QByteArray readDatFile(const QString &path, qint64 maxBytes) {
  if (path.isEmpty() || !QFileInfo(path).isFile()) {
    return {};
  }
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    return {};
  }
  // PK\x03\x04 (zip local-file-header magic) → a zip-packed DAT. Extract its
  // first .dat member into a temp dir and read that instead. Needs an archive
  // tool on PATH (via NoIntroDownload::extractDatsTo); without one, or with no
  // .dat inside, we return empty and the caller reports it as unreadable.
  if (f.peek(4) == QByteArrayLiteral("PK\x03\x04")) {
    f.close();
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
      return {};
    }
    auto ex = NoIntroDownload::extractDatsTo(path, tmp.path(), {});
    if (ex.isError() || ex.value().isEmpty()) {
      return {};
    }
    QFile member(ex.value().constFirst());
    if (!member.open(QIODevice::ReadOnly)) {
      return {};
    }
    // Bound the extracted member too (Kartend-4bouw): a zip can decompress to a
    // hostile multi-gigabyte member. Fail loud (empty) rather than slurp it.
    if (member.size() > kMaxDatBytes) {
      qWarning("DatLookup: extracted DAT member exceeds %lld-byte cap (%lld) — rejecting %s",
               kMaxDatBytes, member.size(), qPrintable(path));
      return {};
    }
    return member.readAll();
  }
  if (maxBytes > 0) {
    return f.read(maxBytes);
  }
  // Full read: cap at kMaxDatBytes (Kartend-4bouw) so a multi-gigabyte file
  // mislabelled `.dat` can't be slurped whole into memory. Reject loudly rather
  // than truncate into a partial parse.
  if (f.size() > kMaxDatBytes) {
    qWarning("DatLookup: DAT file exceeds %lld-byte cap (%lld) — rejecting %s", kMaxDatBytes,
             f.size(), qPrintable(path));
    return {};
  }
  return f.readAll();
}

ErrorUtils::Result<Store> loadStoreFromFile(const QString &path) {
  if (path.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Empty DAT path",
                               "DatLookup::loadStoreFromFile");
  }
  if (!QFileInfo(path).isFile()) {
    return ErrorContext::error(ErrorCode::FileNotFound, "DAT file does not exist",
                               "DatLookup::loadStoreFromFile")
        .withDetails(path);
  }
  const QByteArray bytes = readDatFile(path);
  if (bytes.isEmpty()) {
    return ErrorContext::error(ErrorCode::FileNotFound,
                               "Failed to read DAT file (unreadable, empty, or a .zip with no "
                               ".dat member / no archive tool)",
                               "DatLookup::loadStoreFromFile")
        .withDetails(path);
  }
  const Dialect dialect = detectDialect(bytes);
  auto parsed = parseDat(bytes);
  if (parsed.isError()) return parsed.error();
  return Store(parsed.value(), dialect);
}

} // namespace DatLookup
