#ifndef DATAUDITCATALOGUE_H
#define DATAUDITCATALOGUE_H

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

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
  /// Register a source DAT (by display name, typically its filename) and return
  /// its id. buildCatalogue() calls this once per loaded DAT, then tags every
  /// record from that DAT via addRecord(r, sourceId), so per-DAT completeness
  /// can be attributed back to the catalogue it came from (Kartend-m6qsb.15).
  int addSource(const QString &name);

  /// Add one record, updating the sha1/md5/crc and romName indexes. Empty
  /// hashes are skipped for that index; first-seen wins on collision (matches
  /// DatLookup::Store). Records with no hashes are still stored (so they can
  /// surface as Missing) but are unmatchable by content. `sourceId` attributes
  /// the record to a source registered via addSource() (-1 = no/unknown source,
  /// e.g. a catalogue built by hand in a test).
  void addRecord(const DatLookup::DatRecord &r, int sourceId = -1);

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

  /// Source id attributed to record `i` (-1 when none). Pairs with sourceName().
  [[nodiscard]] int recordSource(int i) const { return m_recordSource.at(i); }
  /// Number of registered source DATs.
  [[nodiscard]] int sourceCount() const { return static_cast<int>(m_sourceNames.size()); }
  /// Display name of a registered source, or empty for an out-of-range id.
  [[nodiscard]] QString sourceName(int sourceId) const {
    return (sourceId >= 0 && sourceId < m_sourceNames.size()) ? m_sourceNames.at(sourceId)
                                                              : QString();
  }

  // --- clone relationships (Kartend-m6qsb.29) -------------------------------
  /// All record indices belonging to the set `setKey` (the set-id a clone's
  /// cloneof references — DatRecord::setId). Empty for an unknown set.
  [[nodiscard]] QList<int> recordsForSet(const QString &setKey) const;
  /// Record indices of EVERY ancestor set of record `i` — its parent set, that
  /// set's parent, … to the root — walking cloneof transitively with a cycle
  /// guard (Kartend-m6qsb.29). Empty when `i` is not a clone. Lets NonMerged
  /// expand a clone's full inherited-rom expectation across multi-level chains.
  [[nodiscard]] QList<int> ancestorRecords(int i) const;
  /// gameName of the ROOT set of record `i`: follow cloneof to the topmost
  /// resolvable ancestor, returning `i`'s own gameName when it is not a clone or
  /// its parent is absent. Lets Merged fold a chained clone under its root set
  /// rather than only its immediate parent.
  [[nodiscard]] QString rootSetGameName(int i) const;
  /// True when record `i` declares a cloneof — it is a clone of another set.
  [[nodiscard]] bool isClone(int i) const { return !m_records.at(i).cloneOf.isEmpty(); }

private:
  QList<DatLookup::DatRecord> m_records;
  QList<int> m_recordSource;    ///< parallel to m_records; source id or -1
  QStringList m_sourceNames;    ///< source id -> display name (DAT filename)
  QHash<QString, int> m_bySha1; ///< lowercase sha1 -> record index
  QHash<QString, int> m_byMd5;
  QHash<QString, int> m_byCrc;
  QHash<QString, int> m_byName;          ///< romName -> record index
  QHash<QString, QList<int>> m_bySetKey; ///< set-id -> all record indices of that set
};

} // namespace DatAudit

#endif // DATAUDITCATALOGUE_H
