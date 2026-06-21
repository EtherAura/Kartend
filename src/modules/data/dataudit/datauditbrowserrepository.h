#ifndef DATAUDITBROWSERREPOSITORY_H
#define DATAUDITBROWSERREPOSITORY_H

#include <optional>

#include <QList>
#include <QString>

#include "datauditprofile.h" // DatAuditProfile::{Profile,RollupRow,GameRollupRow,ResultRow}
#include "datcache.h"        // DatCache::CachedSource
#include "datlookup.h"       // DatLookup::DatRecord

namespace DatAudit {

/// Data-access boundary for the DAT-audit browser (Kartend-ma608). The browser
/// page (src/ui/dialogs/dataudit/datauditbrowserpage) is read-only — it renders
/// persisted audit snapshots and reuses the DAT cache for ROM hashes — but it
/// used to issue the schema queries inline from its view slots, blocking the UI
/// thread on SQLite and duplicating data-access knowledge with DatAuditDialog.
///
/// This repository owns the query origin: each method opens a short-lived
/// UI-thread connection (the page's old `withDb` lambda, now a private impl
/// detail) or a read-only DatCache::Store, runs one query body, and returns
/// plain structs. The page feeds those into its already-model-backed views
/// (AuditTreeModel / GameListModel / RomFileModel) — what is queried and how the
/// item widgets render are unchanged.
///
/// Sits in src/modules/data (below src/ui in the layering DAG), so the UI may
/// depend on it. Free of QObject / widgets; testable without a widget.
class DatAuditBrowserRepository {
public:
  DatAuditBrowserRepository() = default;

  /// All audit profiles ordered by name (each with its DAT refs). Empty on a
  /// DB failure (the error is logged); mirrors the page's old refresh() read.
  [[nodiscard]] QList<DatAuditProfile::Profile> profiles() const;

  /// Every profile's per-(source, status, mia) result counts in one query, for
  /// the global tree's rollups. Empty on a DB failure (logged).
  [[nodiscard]] QList<DatAuditProfile::RollupRow> rollups() const;

  /// Per-(game, status, mia) counts for one source within one profile, for the
  /// game-list pane. Empty on a DB failure (logged).
  [[nodiscard]] QList<DatAuditProfile::GameRollupRow> gamesFor(qint64 profileId,
                                                               const QString &sourceName) const;

  /// The raw persisted result rows for one game (profile + source + game), for
  /// the ROM-detail pane. Empty when nothing matches / on a DB failure.
  [[nodiscard]] QList<DatAuditProfile::ResultRow>
  romsFor(qint64 profileId, const QString &sourceName, const QString &gameName) const;

  /// All persisted result rows for one source (every game), for the
  /// folder-as-item view which regroups them by containing item-folder. Empty
  /// when the source has no rows / on a DB failure.
  [[nodiscard]] QList<DatAuditProfile::ResultRow> sourceRows(qint64 profileId,
                                                             const QString &sourceName) const;

  /// The DAT records of one game from the cache (no app-DB access), for joining
  /// hashes/sizes/clone info into the ROM-detail pane. Empty when the cache has
  /// never ingested `datPath` or the game is unknown.
  [[nodiscard]] QList<DatLookup::DatRecord> datRecordsFor(const QString &datPath,
                                                          const QString &gameName) const;

  /// The cached DAT header row for `datPath` (name/description/version), for the
  /// DAT-info header. nullopt when the cache has never ingested it.
  [[nodiscard]] std::optional<DatCache::CachedSource> datHeader(const QString &datPath) const;
};

} // namespace DatAudit

#endif // DATAUDITBROWSERREPOSITORY_H
