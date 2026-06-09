#ifndef DATAUDITTYPES_H
#define DATAUDITTYPES_H

#include <QList>
#include <QString>

/// Core value types for DAT auditing: the per-entry status taxonomy, the row
/// the result model renders, and the rollup summary the glance bar shows.
///
/// The taxonomy is the two-axis model distilled from the prior-art tools
/// (RomVault / igir / clrmamepro): some statuses describe a *catalogue
/// entry's* satisfaction (Have / WrongName / WrongHash / Missing), others a
/// *file's* disposition (Unknown / Duplicate / Corrupt). A single audit row
/// carries one status plus whichever of {catalogue entry, on-disk file} apply.
namespace DatAudit {

/// The status of one audited unit. Ordering groups the "you have it and it's
/// fine" case first, then the fixable cases, then the problems — the result
/// model leans on this for default sort.
enum class Status {
  Have,      ///< File present, hash matches a catalogue entry, name is canonical.
  WrongName, ///< Content matches an entry (hash) but the on-disk name differs — rename candidate.
  WrongHash, ///< A file's name matches an entry but its content (hash) does not — bad/!= revision.
  Duplicate, ///< File matches an entry already satisfied by another file.
  Unknown,   ///< File matches no catalogue entry at all.
  Corrupt,   ///< File could not be read/hashed.
  Missing,   ///< Catalogue entry with no matching file anywhere in the scan.
  Unscanned, ///< Reserved: not yet hashed / permission denied (runner does not emit this yet).
  BadDump,   ///< Reserved: entry flagged bad in the DAT (runner does not emit this yet).
};

/// Stable lowercase token for a status — used by CSV export, the results DB
/// snapshot, and tests. Kept separate from any localized display label.
[[nodiscard]] QString statusToken(Status s);

/// True when the status means "the catalogue entry's content is present"
/// (Have or WrongName) — i.e. the entry is satisfied even if mislabeled.
[[nodiscard]] bool isPresent(Status s);

/// One audited unit. Entry-centric rows (Missing) carry the catalogue fields
/// with an empty filePath; file-centric rows (Unknown / Corrupt) carry the
/// file fields with an empty gameName; matched rows (Have / WrongName /
/// WrongHash / Duplicate) carry both.
struct AuditRow {
  Status status = Status::Missing;
  QString gameName;     ///< Canonical title from the DAT entry, when matched/known.
  QString expectedName; ///< DAT romName — the canonical filename / rename target, when known.
  QString filePath;     ///< Absolute on-disk path, when a file is involved.
  QString actualName;   ///< Basename of filePath, when present.
  qint64 size = -1;     ///< File size in bytes (on-disk for file rows, DAT-declared for Missing).
  QString crc;          ///< Lowercase hex digests of the on-disk file, when hashed.
  QString md5;
  QString sha1;
};

/// Rollup counts for the summary bar. `matched` and `total*` are derived.
struct AuditSummary {
  int have = 0;
  int wrongName = 0;
  int wrongHash = 0;
  int duplicate = 0;
  int unknown = 0;
  int corrupt = 0;
  int missing = 0;
  int totalCatalogue = 0; ///< Distinct catalogue entries considered.
  int totalFiles = 0;     ///< Files scanned (across all roots).

  /// Catalogue entries whose content is present (Have + WrongName).
  [[nodiscard]] int present() const { return have + wrongName; }
};

/// Tally a row list into a summary (totalCatalogue/totalFiles are filled by
/// the runner since they include Missing entries and scanned files that may
/// not all map 1:1 to rows). Exposed for tests + the model.
[[nodiscard]] AuditSummary summarize(const QList<AuditRow> &rows);

} // namespace DatAudit

#endif // DATAUDITTYPES_H
