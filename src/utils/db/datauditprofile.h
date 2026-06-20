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
  QStringList regionPrefs; ///< 1G1R region priority order, e.g. {"USA", "World"}.
  bool onePerGame = false; ///< 1G1R toggle.
  /// Clone/parent expected-file model (Kartend-m6qsb.29). A DatAudit::
  /// mergeModeToken value ('' / unknown -> "split", the no-op default). Kept as
  /// the raw token because this header sits below the dataudit module that owns
  /// the MergeMode enum (mirrors detectedLayout).
  QString mergeMode;
  QStringList ignoreRules; ///< Glob patterns excluded from scan/audit.
  FixMode fixMode = FixMode::InPlace;
  QString managedOutputRoot; ///< Destination when fixMode == ManagedOutput.
  /// Per-collection quarantine folder for the Fix dialog (unknown + wrong-content
  /// files). Empty falls back to the global default quarantine folder. Remembered
  /// here so each collection can keep its own without re-typing per run.
  QString quarantineRoot;
  /// Optional grouping label for the audit browser's category tree level
  /// (Kartend-7iqhl.5, schema v25). Empty falls back to the linked collection's
  /// name, then to "(Ungrouped)". User-editable in the profile dialog.
  QString category;
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
  /// Source DAT + game this row belongs to and its MIA flag (Kartend-34lab,
  /// schema v22). Let the browser's global tree and game list be simple grouped
  /// queries; the entry_key carries no game/source identity. Empty/false on
  /// pre-v22 snapshots until the profile is next re-audited.
  QString sourceName;
  QString gameName;
  bool mia = false;
  /// Archive members only (Kartend-7iqhl.4, schema v24): the member's index
  /// among its container's regular-file members, in central-directory order, for
  /// the browser's ZipIndex column. -1 for whole-file rows and pre-v24 snapshots
  /// (until the profile is re-audited).
  int zipIndex = -1;
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

/// One grouped rollup bucket for the audit browser (Kartend-34lab). A single
/// `loadAllRollups` read returns one of these per (profile, source, status,
/// mia) combination — enough to build the whole global tree's per-source
/// counts without re-scanning. `status` is the int form of DatAudit::Status.
struct RollupRow {
  qint64 profileId = -1;
  QString sourceName;
  int status = 0;
  bool mia = false;
  int count = 0;
};

/// Every profile's per-(source, status, mia) result counts in one query, for
/// the browser's global tree. Pre-v22 snapshots surface with an empty
/// sourceName (their rows predate source attribution).
[[nodiscard]] ErrorUtils::Result<QList<RollupRow>> loadAllRollups(QSqlDatabase &db);

/// One grouped rollup bucket for a single game within one (profile, source),
/// for the browser's game list. `status` is the int form of DatAudit::Status.
struct GameRollupRow {
  QString gameName;
  int status = 0;
  bool mia = false;
  int count = 0;
};

/// Per-(game, status, mia) counts for one source within one profile, for the
/// browser's game-list pane. One row per distinct (gameName, status, mia).
[[nodiscard]] ErrorUtils::Result<QList<GameRollupRow>>
loadGameRollups(QSqlDatabase &db, qint64 profileId, const QString &sourceName);

/// The raw persisted result rows for one game (profile + source + game), for
/// the browser's ROM-detail pane — joined by romName against the DAT cache to
/// fill in hashes/sizes/clone info. Empty when nothing matches.
[[nodiscard]] ErrorUtils::Result<QList<ResultRow>> loadGameResultRows(QSqlDatabase &db,
                                                                      qint64 profileId,
                                                                      const QString &sourceName,
                                                                      const QString &gameName);

/// All persisted result rows for one source (every game), for the audit
/// browser's folder-as-item view (Kartend-m6qsb.30) which regroups them by the
/// containing item-folder. Empty when the source has no rows.
[[nodiscard]] ErrorUtils::Result<QList<ResultRow>>
loadSourceResultRows(QSqlDatabase &db, qint64 profileId, const QString &sourceName);

/// Every persisted result row for a profile, across all of its sources, for the
/// browser's profile-scoped Fix action (Kartend-7iqhl.2) which reconstructs
/// AuditRows from this snapshot. Empty when the profile has never been audited.
[[nodiscard]] ErrorUtils::Result<QList<ResultRow>> loadProfileResultRows(QSqlDatabase &db,
                                                                         qint64 profileId);

/// Enum <-> column-text mapping. Exposed for the config UI and tests; an
/// unknown string falls back to the safe default (InPlace).
[[nodiscard]] QString fixModeToString(FixMode m);
[[nodiscard]] FixMode fixModeFromString(const QString &s);

} // namespace DatAuditProfile

#endif // DATAUDITPROFILE_H
