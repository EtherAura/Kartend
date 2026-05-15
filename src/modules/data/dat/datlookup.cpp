// DAT-file parsers (Logiqx + MAME listxml dialects) + indexed lookup
// store. Streaming XML via QXmlStreamReader so multi-MB DATs don't pin
// memory beyond the parsed-record list itself.
#include "datlookup.h"

#include <optional>
#include <QFile>
#include <QFileInfo>
#include <QStringView>
#include <QXmlStreamReader>

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace DatLookup {

namespace {

/// Lowercase + strip surrounding whitespace, leaving a clean hex
/// digest ready for hash-map lookup. Defensive about input source —
/// some DAT producers emit upper-case hashes, some have stray
/// whitespace inside attribute values.
QString normaliseHex(QStringView raw) {
  return raw.trimmed().toString().toLower();
}

/// Build a DatRecord from a `<rom>` element's attributes. Returns
/// nullopt when the entry has no usable hash (parse-time drop — an
/// entry without any hash can't be reached from a lookup query).
std::optional<DatRecord> readRomElement(const QXmlStreamAttributes &attrs,
                                        const QString &gameName) {
  // MAME marks placeholder roms with `status="nodump"` (the chip is
  // known to exist but no good dump is available). These entries
  // carry zeroed-out hashes that would otherwise collide with every
  // legitimately-zero-hash file — drop them at parse time.
  const QStringView status = attrs.value(QLatin1String("status"));
  if (status == QLatin1String("nodump")) return std::nullopt;

  DatRecord r;
  r.gameName = gameName;
  r.romName = attrs.value(QLatin1String("name")).toString();
  const QStringView sizeStr = attrs.value(QLatin1String("size"));
  if (!sizeStr.isEmpty()) {
    bool ok = false;
    const qint64 sz = sizeStr.toString().toLongLong(&ok);
    if (ok) r.size = sz;
  }
  r.crc = normaliseHex(attrs.value(QLatin1String("crc")));
  r.md5 = normaliseHex(attrs.value(QLatin1String("md5")));
  r.sha1 = normaliseHex(attrs.value(QLatin1String("sha1")));
  if (r.crc.isEmpty() && r.md5.isEmpty() && r.sha1.isEmpty()) {
    return std::nullopt;
  }
  return r;
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

} // namespace

Dialect detectDialect(const QByteArray &xml) {
  // Stop at the first start element — we just need the root tag.
  // Cheap even on a 100MB MAME listxml because QXmlStreamReader
  // streams from the byte buffer and doesn't materialise the tree.
  QXmlStreamReader reader(xml);
  while (!reader.atEnd()) {
    const auto token = reader.readNext();
    if (reader.hasError()) return Dialect::Unknown;
    if (token != QXmlStreamReader::StartElement) continue;
    const QStringView name = reader.name();
    if (name == QLatin1String("datafile")) return Dialect::Logiqx;
    if (name == QLatin1String("mame")) return Dialect::Mame;
    return Dialect::Unknown;
  }
  return Dialect::Unknown;
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

  while (!reader.atEnd()) {
    const auto token = reader.readNext();
    if (reader.hasError()) break;
    if (token != QXmlStreamReader::StartElement) continue;
    const QStringView name = reader.name();

    if (name == QLatin1String("game") || name == QLatin1String("machine")) {
      currentGameName = reader.attributes().value(QLatin1String("name")).toString();
    } else if (name == QLatin1String("rom") && !currentGameName.isEmpty()) {
      if (auto r = readRomElement(reader.attributes(), currentGameName)) {
        out.append(std::move(*r));
      }
    }
    // Note: TOSEC's optional `<release>` child of `<game>` is
    // ignored on purpose — it carries region/language/date metadata
    // and no hashes, so it has nothing to contribute to a lookup
    // index. The default-branch fall-through here is the entire
    // handling needed.
  }

  if (reader.hasError()) {
    return makeParseError(reader, "DatLookup::parseLogiqxDat");
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
  bool inDescription = false;
  // Buffer roms seen so far for the current machine; flushed with
  // the resolved gameName at `</machine>` so any rom that appears
  // before `<description>` still gets the nice name.
  QList<DatRecord> pendingRoms;

  auto flushMachine = [&]() {
    const QString gameName = !currentDescription.isEmpty() ? currentDescription : currentSetId;
    for (auto &r : pendingRoms) {
      r.gameName = gameName;
      out.append(std::move(r));
    }
    pendingRoms.clear();
    currentSetId.clear();
    currentDescription.clear();
  };

  while (!reader.atEnd()) {
    const auto token = reader.readNext();
    if (reader.hasError()) break;
    if (token == QXmlStreamReader::StartElement) {
      const QStringView name = reader.name();
      if (name == QLatin1String("machine") || name == QLatin1String("game")) {
        // Some old MAME builds emit `<game>` here; treat it the same.
        currentSetId = reader.attributes().value(QLatin1String("name")).toString();
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
      }
    }
  }

  if (reader.hasError()) {
    return makeParseError(reader, "DatLookup::parseMameListXml");
  }
  return out;
}

ErrorUtils::Result<QList<DatRecord>> parseDat(const QByteArray &xml) {
  switch (detectDialect(xml)) {
  case Dialect::Logiqx:
    return parseLogiqxDat(xml);
  case Dialect::Mame:
    return parseMameListXml(xml);
  case Dialect::Unknown:
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "Unrecognised DAT format — expected <datafile> (No-Intro / "
                               "Redump / TOSEC) or <mame> (MAME listxml) root element",
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
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    return ErrorContext::error(ErrorCode::FileNotFound, "Failed to open DAT file",
                               "DatLookup::loadStoreFromFile")
        .withDetails(f.errorString());
  }
  const QByteArray bytes = f.readAll();
  f.close();
  const Dialect dialect = detectDialect(bytes);
  auto parsed = parseDat(bytes);
  if (parsed.isError()) return parsed.error();
  return Store(parsed.value(), dialect);
}

} // namespace DatLookup
