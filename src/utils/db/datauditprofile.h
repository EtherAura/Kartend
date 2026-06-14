#ifndef DATAUDITPROFILE_H
#define DATAUDITPROFILE_H

#include <optional>

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include "errorutils.h"

class QSqlDatabase;

/// Persistence for DAT-audit profiles (schema v17). A profile is a durable,
/// first-class binding of one-or-more DAT catalogues to one-or-more scan
/// roots, plus the audit/fix settings — the "what am I auditing against"
/// record. It is independent of collections: `collectionUuid` is an optional
/// link ('' = none), so a profile can audit an arbitrary folder that is not a
/// Kartend collection at all.
///
/// Stored across three tables:
///   - `dat_audit_profile`     — the Profile row below; list-valued fields
///                               (scanRoots, regionPrefs, ignoreRules) are
///                               serialized as compact JSON arrays.
///   - `dat_audit_profile_dat` — the ordered DatRef child rows.
///   - `dat_audit_result`      — the per-entry status snapshot of the last
///                               completed audit, written via replaceResults()
///                               below and summarised by loadResultSummary().
///
/// Free functions taking `QSqlDatabase &` first, matching the other
/// src/utils/db stores (UsageStatsStore, ItemMetadata). Not a QObject.
namespace DatAuditProfile {

/// Where a fix writes its results, chosen per profile.
enum class FixMode {
  InPlace,       ///< Rename/reorganize the user's media dirs directly.
  ManagedOutput, ///< Build a clean sorted set into managedOutputRoot; originals untouched.
};

/// Set-merge interpretation for DATs that carry parent/clone relations (MAME
/// listxml). Inert for flat No-Intro/Redump catalogues.
enum class MergeMode { Split, Merged, NonMerged };

/// One DAT catalogue referenced by a profile. mtime/dialect/recordCount are
/// cached at attach time for UI labels and staleness hints; the auditor
/// re-reads each DAT through DatCache regardless. `dialect` is the
/// DatLookup::Dialect value as an int so this header stays free of the dat
/// module include.
struct DatRef {
  QString path;
  qint64 mtimeMs = 0;
  int dialect = 0;
  int recordCount = 0;
};

/// A profile row plus its ordered DAT refs. `id == -1` marks an unsaved
/// profile; insert() fills it in.
struct Profile {
  qint64 id = -1;
  QString name;
  QString collectionUuid; ///< '' = not linked to a collection.
  QStringList scanRoots;
  QList<DatRef> dats;
  MergeMode mergeMode = MergeMode::Split;
  QStringList regionPrefs; ///< 1G1R region priority order, e.g. {"USA", "World"}.
  bool onePerGame = false; ///< 1G1R toggle.
  QStringList ignoreRules; ///< Glob patterns excluded from scan/audit.
  FixMode fixMode = FixMode::InPlace;
  QString managedOutputRoot; ///< Destination when fixMode == ManagedOutput.
  /// Folder-structure probe result (Kartend-m6qsb.6). A DatAudit::layoutToken
  /// value ('' = never detected) — kept as the raw token here because this
  /// header sits below the dataudit module that owns the enum. Only a
  /// CONFIRMED layout may change scan semantics; an unconfirmed detection is
  /// just the banner suggestion waiting for the user.
  QString detectedLayout;
  bool layoutConfirmed = false;
  qint64 lastScanAtMs = 0; ///< Unix ms of the last completed audit; 0 = never.
  qint64 createdAtMs = 0;
  qint64 updatedAtMs = 0;
};

/// Insert a new profile and its DAT child rows in one transaction. Stamps
/// created_at/updated_at to now; returns the new row id. Rejects an empty
/// name.
[[nodiscard]] ErrorUtils::Result<qint64> insert(QSqlDatabase &db, const Profile &p);

/// Update an existing profile by `p.id`, rewriting its DAT child rows. Stamps
/// updated_at. Returns an error when the id is invalid or absent.
[[nodiscard]] ErrorUtils::Result<bool> update(QSqlDatabase &db, const Profile &p);

/// Delete a profile and all its child DAT + result rows. The children are
/// deleted explicitly (inside the transaction) so correctness does not depend
/// on the foreign_keys pragma being on for this connection.
[[nodiscard]] ErrorUtils::Result<bool> remove(QSqlDatabase &db, qint64 id);

/// Load one profile (with its DAT refs) by id. The optional is empty when no
/// such row exists; an error is returned only on a real DB failure.
[[nodiscard]] ErrorUtils::Result<std::optional<Profile>> load(QSqlDatabase &db, qint64 id);

/// All profiles ordered by name (each with its DAT refs). Empty list when
/// none exist.
[[nodiscard]] ErrorUtils::Result<QList<Profile>> listAll(QSqlDatabase &db);

/// The profile linked to `collectionUuid`, or empty when none is. Several
/// profiles may legitimately claim one collection (no unique constraint);
/// the most recently updated one wins so the collection-seeded audit launch
/// is deterministic. An empty uuid is rejected — '' means "not linked", and
/// matching it would hand back an arbitrary unlinked profile.
[[nodiscard]] ErrorUtils::Result<std::optional<Profile>>
loadByCollectionUuid(QSqlDatabase &db, const QString &collectionUuid);

/// Stamp `last_scan_at` (called by the runner after a completed audit). Pass 0
/// to clear it back to "never".
[[nodiscard]] ErrorUtils::Result<bool> touchLastScan(QSqlDatabase &db, qint64 id, qint64 whenMs);

/// One persisted audit-result row. `status` carries DatAudit::Status as a
/// plain int — this header sits below the dataudit module in the layering
/// DAG, so the enum can't appear here; the dialog maps it on write and the
/// numeric values are pinned by the dataudit tests. `entryKey` is the
/// caller-chosen primary-key suffix (unique per profile snapshot).
struct ResultRow {
  QString entryKey;
  int status = 0;
  QString filePath;
  QString detail;
};

/// Replace profile `id`'s entire result snapshot with `rows`, in one
/// transaction. An audit is a full re-statement of the world, so the previous
/// snapshot is deleted wholesale — there is no row-level merge. INSERT OR
/// REPLACE absorbs duplicate entryKeys (merged catalogues can repeat a name);
/// last write wins within one snapshot.
[[nodiscard]] ErrorUtils::Result<bool> replaceResults(QSqlDatabase &db, qint64 id,
                                                      const QList<ResultRow> &rows);

/// Rollup of a profile's persisted snapshot: per-status row counts (keyed by
/// the int form of DatAudit::Status) plus the profile's last-scan stamp.
/// `hasResults` distinguishes "audited, zero rows" from "never audited".
struct ResultSummary {
  qint64 lastScanAtMs = 0;
  bool hasResults = false;
  QHash<int, int> countsByStatus;

  [[nodiscard]] int count(int status) const { return countsByStatus.value(status, 0); }
};

/// Summary for one profile. The optional is empty when the profile row itself
/// does not exist; an error is returned only on a real DB failure.
[[nodiscard]] ErrorUtils::Result<std::optional<ResultSummary>> loadResultSummary(QSqlDatabase &db,
                                                                                 qint64 id);

/// Enum <-> column-text mapping. Exposed for the config UI and tests; an
/// unknown string falls back to the safe default (InPlace / Split).
[[nodiscard]] QString fixModeToString(FixMode m);
[[nodiscard]] FixMode fixModeFromString(const QString &s);
[[nodiscard]] QString mergeModeToString(MergeMode m);
[[nodiscard]] MergeMode mergeModeFromString(const QString &s);

} // namespace DatAuditProfile

#endif // DATAUDITPROFILE_H
