// Sibling TU: the clrmamepro text dialect — decode + tokeniser (CmpToken /
// tokenizeClrMamePro), the block parsers (cmpParseRom / cmpParseGame /
// parseClrMameProDat), and the looksLikeClrMamePro signature sniff. Because
// detectDialect and the probeHeader overloads branch into that sniff and the
// tokeniser (all file-local for the partial split, which keeps datlookup.h
// unchanged), the dialect detection and header probing live here too; their
// XML branches are self-contained QXmlStreamReader walks. The XML record
// parsers live in datlookup_xml.cpp; the record types, Store, dispatch, and
// file reading stay in datlookup.cpp.
#include "datlookup.h"

#include <optional>
#include <QStringDecoder>
#include <QStringView>
#include <QXmlStreamReader>

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace DatLookup {

namespace {

// Defensive ingest bounds for untrusted DAT input (Kartend-4bouw), applied at
// parse time before a field reaches SQLite. Duplicated verbatim in
// datlookup_xml.cpp — both dialect families enforce the same caps, and the
// partial split keeps datlookup.h unchanged, so the file-local constants are
// repeated per TU rather than exported.
//
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
/// (Duplicated verbatim in datlookup_xml.cpp — see the constants note above.)
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

ErrorUtils::Result<QList<DatRecord>> parseClrMameProDat(const QByteArray &xml) {
  const QList<CmpToken> toks = tokenizeClrMamePro(decodeDatText(xml));
  // Truncation guard, mirroring the XML dialects' closing-root-tag check
  // (Kartend-u4sdu): the tokeniser simply stops at EOF, so a partial download
  // cut mid-file would otherwise "parse" to a partial record set — which
  // DatCache then commits sticky on (path, mtime). Parens inside quoted
  // strings are consumed as string payload by the tokeniser, so token-level
  // paren balance IS the block structure: a nonzero depth (or a stray close)
  // at EOF means the stream ended inside a block.
  {
    int depth = 0;
    bool unbalanced = false;
    for (const CmpToken &t : toks) {
      if (t.kind == CmpToken::Open) {
        ++depth;
      } else if (t.kind == CmpToken::Close) {
        if (depth == 0) {
          unbalanced = true;
          break;
        }
        --depth;
      }
    }
    if (unbalanced || depth != 0) {
      return ErrorContext::error(ErrorCode::InvalidArgument,
                                 "clrmamepro DAT has unbalanced block parentheses — file is "
                                 "truncated or incomplete",
                                 "DatLookup::parseClrMameProDat");
    }
  }
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

} // namespace DatLookup
