#ifndef LAUNCHERIMPORTCONTROLLER_H
#define LAUNCHERIMPORTCONTROLLER_H

#include <functional>

#include <QFutureWatcher>
#include <QList>
#include <QObject>
#include <QString>

#include "launcherimportservice.h"

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

struct CollectionConfig;
struct ApplicationContext;
class IDatabaseManager;
namespace Scraper {
class BatchScrapeRunner;
}

/// Owns the launcher-import flows (Kartend-wuq2c): the "Import from
/// Launcher…" dialog that creates .kartlink stub collections for Steam /
/// Flatpak / Lutris, the deferred startup re-sync of every collection
/// carrying an importSource marker, and the manual File → Import → "Sync
/// Launcher Collections" action. Follows the DatAuditController
/// closure-context shape; the sync work itself is LauncherImportService
/// (stateless file/SQLite reads + atomic stub writes) run on the global
/// pool for the background passes and inline (wait cursor) for the
/// dialog-driven initial import.
struct LauncherImportControllerContext {
  std::function<QWidget *()> getParentWindow;

  /// The live collection list, borrowed from MainWindow — read to find
  /// importSource-marked collections and to keep new names unique.
  std::function<QList<CollectionConfig> *()> getCollections;

  /// MainWindow::appendCollectionAndPersist — append + saveCollections +
  /// rebuildHierarchyCache (+ optional navigate).
  std::function<void(const CollectionConfig &config, bool navigate)> appendCollectionAndPersist;

  /// Save half of the above, for collections already in the list: re-syncing
  /// an existing source at a different ImportScope edits that collection's
  /// importScope in place, and without a save the next startup would re-sync
  /// at the old tier and undo it (Kartend-el5st).
  std::function<void()> persistCollections;

  /// NavigationManager::safeReloadCollection — refresh the grid after a sync
  /// changed a collection's stub folder. Deliberately NOT
  /// forceRescanCollection: that path clears the collection's item rows
  /// wholesale (losing play counts / ratings), where a reload lets
  /// ensureCollectionScanned's upsert + prune pick up the changed folder
  /// (the sync bumped its mtime) with per-item stats preserved. The helper
  /// itself redirects to the currently-viewed collection, which is exactly
  /// the only case needing an immediate visual refresh — every other
  /// collection re-scans on its next navigation.
  std::function<void(int collectionIndex)> refreshCollection;

  /// Non-modal outcome surfacing (status bar).
  std::function<void(const QString &message)> showStatusMessage;

  /// IDatabaseManager access for the Steam metadata pass (Kartend-11elw):
  /// databaseFilePath() feeds the worker-side throwaway connection, and
  /// invalidateMetadataCacheItem() runs on the GUI thread after writes.
  std::function<IDatabaseManager *()> getDatabaseManager;

  /// ApplicationContext for the post-import store enrichment
  /// (BatchScrapeRunner takes ctx, not a DB snapshot).
  std::function<const ApplicationContext *()> getApplicationContext;
};

class LauncherImportController : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(LauncherImportController)
public:
  explicit LauncherImportController(QObject *parent = nullptr);
  ~LauncherImportController() override;

  void setContext(const LauncherImportControllerContext &context);

public slots:
  /// Modal source picker; creates a pre-configured stub collection per
  /// selected source (running its initial sync inline under a wait cursor),
  /// or re-syncs a source that already has a collection.
  void runImportDialog();

  /// Deferred startup pass: re-sync every importSource collection on the
  /// global pool, then force-rescan the ones whose stub folders changed.
  /// Silent when everything is already up to date.
  void startupSync();

  /// Manual re-sync — same pass as startupSync but always reports an
  /// outcome via the status bar.
  void syncLauncherCollections();

private:
  /// UI-thread snapshot handed to the worker: indices resolved, paths
  /// expanded, and the uuid + db path captured up front so the worker never
  /// touches the live collection list or DatabaseManager.
  struct SyncJob {
    int collectionIndex = -1;
    QString sourceId;
    QString stubDir;
    QString artworkDir;
    QString collectionUuid;
    QString dbPath;
    /// The tier the collection was imported with, carried so a re-sync
    /// re-lists the same breadth — see CollectionConfig::importScope.
    LauncherImportService::ImportScope scope = LauncherImportService::ImportScope::Installed;
    /// Which slice of the source this collection holds — see
    /// CollectionConfig::importSourceKey. Empty for every source but ES-DE.
    /// Carried for the same reason as the scope: re-syncing an ES-DE
    /// collection WITHOUT it would list the whole library and write every
    /// system's games into that one collection (Kartend-ilkne).
    QString sourceKey;
  };
  struct SyncOutcome {
    int collectionIndex = -1;
    QString sourceId;
    QString collectionUuid;
    LauncherImportService::SyncResult result;
    LauncherImportService::MetadataApplyResult metadata;
  };

  void startBackgroundSync(bool announce);
  void onSyncFinished();
  [[nodiscard]] QList<SyncJob> snapshotJobs() const;
  /// GUI-thread Steam metadata pass for the import-dialog flow: applies
  /// appinfo metadata for the synced stubs, invalidates the metadata cache
  /// per written path, logs errors. Returns rows written (0 for non-Steam
  /// sources — the service ignores their targets).
  int applyMetadataForCollection(const CollectionConfig &config,
                                 const QList<LauncherImportService::SyncedStub> &stubs);

  /// Fetches everything the local caches can't supply — descriptions,
  /// screenshots, store backgrounds, trailers — for a freshly imported or
  /// re-synced Steam collection, so importing is a single step with no
  /// separate scrape (user request 2026-08-04). Drives the ordinary
  /// BatchScrapeRunner with the Steam provider in FillMissing mode, so it
  /// reuses the throttling / retry / media-write / merge machinery and
  /// never overwrites art or fields already present. Runs asynchronously on
  /// the GUI thread's event loop; progress and outcome go to the status
  /// bar. No-op for non-Steam sources and when the stub list is empty.
  void enrichFromStore(const CollectionConfig &config,
                       const QList<LauncherImportService::SyncedStub> &stubs);

  /// Downloads the covers that exist only as URLs (Heroic, itch.io) into the
  /// collection's artwork/front/ (Kartend-g1g30).
  ///
  /// Lives HERE rather than in LauncherImportService because that service is
  /// offline by contract — file and SQLite reads only, runnable on a worker
  /// thread with no manager dependencies. The sync therefore decides WHICH
  /// covers are missing (SyncedStub::pendingCoverUrl) and this pass performs
  /// the network, exactly as enrichFromStore already splits that work.
  ///
  /// Fire-and-forget through the shared rate-limited HttpClient: an import is
  /// usable the moment the stubs exist, so a slow CDN must never hold up the
  /// dialog. Failures are logged, never surfaced as import errors — a missing
  /// cover is not a failed import.
  void fetchRemoteCovers(const CollectionConfig &config,
                         const QList<LauncherImportService::SyncedStub> &stubs);

  /// Enrichment runs one collection at a time; queued requests wait so a
  /// multi-source import doesn't open two store-throttled batches at once.
  struct PendingEnrichment {
    QString collectionName;
    QString collectionUuid;
    QString artworkDir;
    QStringList paths;
    /// LauncherImportService::kSourceSteam or kSourceFlatpak — selects the
    /// store provider (Steam storefront vs Flathub) and, with it, whether
    /// the run fetches media or is metadata-only (Kartend-2bzbu).
    QString sourceId;
  };
  void startNextEnrichment();

  QList<PendingEnrichment> m_enrichQueue;
  /// Owned by `this` via Qt parent; null when idle.
  Scraper::BatchScrapeRunner *m_enrichRunner = nullptr;
  /// Last `done` count pushed to the status bar for the running enrichment,
  /// so the repeated ticks a concurrent batch emits for one completed item
  /// don't rewrite the bar with an identical string. Seeded to 0 per job so
  /// the opening "this can take a few minutes" line is not immediately
  /// overwritten by the done == 0 ticks the first items emit on starting.
  int m_enrichReportedDone = 0;

  LauncherImportControllerContext m_ctx;
  QFutureWatcher<QList<SyncOutcome>> m_syncWatcher;
  /// True while the in-flight background sync came from the manual action —
  /// it then always reports, where the startup pass stays quiet unless
  /// something changed.
  bool m_announceSync = false;
};

#endif // LAUNCHERIMPORTCONTROLLER_H
