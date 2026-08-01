// DAT-file lookup module: shared record types, the indexed lookup Store,
// the parseDat dialect dispatch, and (zip-transparent) DAT file reading.
// Sibling TUs (partial-split pattern, same public header):
//   datlookup_xml.cpp — the Logiqx <datafile> + MAME listxml parsers
//                       (streaming XML via QXmlStreamReader so multi-MB
//                       DATs don't pin memory beyond the record list)
//   datlookup_cmp.cpp — the clrmamepro text dialect (tokeniser + parser),
//                       plus detectDialect / probeHeader, whose text-dialect
//                       sniff rides that tokeniser
#include "datlookup.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

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

} // namespace

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
