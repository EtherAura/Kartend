#ifndef DATAUDITCATALOGUE_H
#define DATAUDITCATALOGUE_H

#include <QHash>
#include <QList>
#include <QString>

#include "datlookup.h"

namespace DatAudit {

/// The union of catalogue entries an audit compares against, with the hash and
/// name indexes the classifier needs. Built from one or more DATs (see
/// buildCatalogue() in datauditrunner.h). Unlike DatLookup/DatCache lookup
/// (which only answers "do I hold this hash?"), the catalogue keeps every
/// record by value so the classifier can report which entries the collection
/// is *missing*.
class Catalogue {
public:
  /// Add one record, updating the sha1/md5/crc and romName indexes. Empty
  /// hashes are skipped for that index; first-seen wins on collision (matches
  /// DatLookup::Store). Records with no hashes are still stored (so they can
  /// surface as Missing) but are unmatchable by content.
  void addRecord(const DatLookup::DatRecord &r);

  /// Index of the first record matching any supplied hash (sha1 -> md5 -> crc
  /// priority), or -1 on miss. Empty hashes are skipped; inputs are matched
  /// case-insensitively (digests are stored lowercase).
  [[nodiscard]] int matchByHash(const QString &crc, const QString &md5, const QString &sha1) const;

  /// Index of the first record whose canonical romName equals `name` (exact),
  /// or -1. Lets the classifier flag WrongHash — a file with the right name
  /// but the wrong content.
  [[nodiscard]] int matchByName(const QString &name) const;

  [[nodiscard]] const DatLookup::DatRecord &record(int i) const { return m_records.at(i); }
  [[nodiscard]] int size() const { return static_cast<int>(m_records.size()); }
  [[nodiscard]] bool isEmpty() const { return m_records.isEmpty(); }

private:
  QList<DatLookup::DatRecord> m_records;
  QHash<QString, int> m_bySha1; ///< lowercase sha1 -> record index
  QHash<QString, int> m_byMd5;
  QHash<QString, int> m_byCrc;
  QHash<QString, int> m_byName; ///< romName -> record index
};

} // namespace DatAudit

#endif // DATAUDITCATALOGUE_H
