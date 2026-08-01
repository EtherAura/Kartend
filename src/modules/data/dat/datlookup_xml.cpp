// Sibling TU: the two XML DAT dialect parsers — parseLogiqxDat (Logiqx
// <datafile>: No-Intro / Redump / TOSEC) and parseMameListXml (MAME <mame>
// listxml) — plus their shared element/attribute helpers (readRomElement,
// miaFromAttrs, cloneOfFromAttrs) and parse-error builders (makeParseError,
// datParseTailError). The clrmamepro text dialect lives in datlookup_cmp.cpp;
// the record types, Store, dispatch, and file reading stay in datlookup.cpp.
#include "datlookup.h"

#include <optional>
#include <QStringView>
#include <QXmlStreamReader>

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace DatLookup {

namespace {

// Defensive ingest bounds for untrusted DAT input (Kartend-4bouw), applied at
// parse time before a field reaches SQLite. Duplicated verbatim in
// datlookup_cmp.cpp — both dialect families enforce the same caps, and the
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

/// Lowercase + strip surrounding whitespace, leaving a clean hex
/// digest ready for hash-map lookup. Defensive about input source —
/// some DAT producers emit upper-case hashes, some have stray
/// whitespace inside attribute values.
/// (Duplicated verbatim in datlookup_cmp.cpp — see the constants note above.)
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

} // namespace

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

} // namespace DatLookup
