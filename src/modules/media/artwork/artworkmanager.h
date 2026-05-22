#ifndef ARTWORKMANAGER_H
#define ARTWORKMANAGER_H

#include <atomic>
#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QPointer>

#include "adaptivebatcher.h"
#include "artworkpathcatalog.h"
#include "iartworkmanager.h"
#include "itemwidget.h"
#include "setuputils.h"

class ArtworkLoadDispatcher;
class ArtworkWidgetRegistry;

class QScrollArea;
class QStackedWidget;
class QWidget;
class QTimer;
class ICacheManager;
class InteractionStateHolder;

namespace TimerUtils {
class Coordinator;
}

struct CollectionConfig;
class QJsonObject;

struct ArtworkInfo {
  QPointer<ItemWidget> mediaItem;
  QString artworkPath;
  /// Identity snapshot of the widget at dispatch time. For item widgets this
  /// is the absolute media file path; for subcollection / virtual-folder
  /// widgets it's the item name. Captured here so applyResultsToUi can detect
  /// cross-collection recycling that the basename check alone misses — two
  /// collections each holding an item named "Tetris" would share a basename
  /// but never share an absolute path (Kartend-j0lb.8).
  QString widgetIdentity;

  struct Result {
    QPointer<ItemWidget> widget;
    QString artworkPath;
    // Kartend-gro2: precomputed off the GUI thread so applyResultsToUi's
    // stale-identity check doesn't construct a QFileInfo per result inside
    // its per-tick loop.
    QString artworkBaseName;
    /// Echoed from the dispatched ArtworkInfo so applyResultsToUi can compare
    /// against the widget's *current* identity to detect mid-flight recycling
    /// across collections that share a basename (Kartend-j0lb.8).
    QString widgetIdentity;
    QImage image;
    bool loadedFromDiskCache = false;
  };
};

/// Decoded record from a silent-load precache batch. Lives at namespace scope
/// so the dispatcher and ArtworkManager's main-thread handler can share it
/// without creating a header cycle.
struct ArtworkPrecacheResult {
  QString artworkPath;
  QImage image;
  bool loadedFromDiskCache = false;
};

struct ApplicationContext;

/**
 * @brief Setup struct for ArtworkManager dependencies.
 */

/**
 * @brief Setup struct for ArtworkManager dependencies.
 */
struct ArtworkManagerSetup {
  const ApplicationContext *ctx = nullptr;
  QStackedWidget *stackedWidget = nullptr;
  QWidget *itemsPage = nullptr;
  QWidget *gridContainer = nullptr;
  QScrollArea *itemScrollArea = nullptr;
  QList<CollectionConfig> *collections = nullptr;
  int *currentCollectionIndex = nullptr;

  SETUP_GETTER_DECL(QStackedWidget *, StackedWidget)
  SETUP_GETTER_DECL(QWidget *, ItemsPage)
  SETUP_GETTER_DECL(QWidget *, GridContainer)
  SETUP_GETTER_DECL(QScrollArea *, ItemScrollArea)
  SETUP_GETTER_DECL(QList<CollectionConfig> *, Collections)
  SETUP_GETTER_DECL(int *, CurrentCollectionIndex)
  SETUP_GETTER_DECL_CTX_ONLY(InteractionStateHolder *, InteractionState)
};

struct UIReferences {
  QScrollArea *itemScrollArea;
};

/**
 * @brief Manages async artwork loading with viewport prioritization and
 * caching.
 *
 * Threading Model:
 * - Main thread: All public API calls, signal emissions, widget updates
 * - Worker threads: QtConcurrent tasks for image loading/scaling
 *
 * Thread-safe operations:
 * - findArtworkForFile() - static, no shared state
 * - loadArtworkParallel() - dispatches work to thread pool safely
 * - m_cancelFlag (std::atomic) - signals cancellation to worker threads
 *
 * NOT thread-safe (main thread only):
 * - addPendingArtwork(), clearPendingArtwork(), clearWidgetReferences()
 * - All widget-related operations
 * - setupReferences() and configuration methods
 *
 * Results are delivered back to main thread via queued signal connections.
 */
// QObject must be the first base; IArtworkManager is a plain (non-QObject)
// role interface — single-QObject-base multiple inheritance.
class ArtworkManager : public QObject, public IArtworkManager {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ArtworkManager)

public:
  // Kartend-davi: constructor no longer takes CacheManager*; the ctx
  // supplied via setupReferences holds the cache pointer authoritatively.
  explicit ArtworkManager(QObject *parent = nullptr);

  void setupReferences(const ArtworkManagerSetup &setup);
  void loadArtworkParallel(const QList<ArtworkInfo> &items, bool highPriority,
                           int customBatchSize = 0);
  void cancelAllArtworkLoading() override;
  void addPendingArtwork(ItemWidget *widget, const QString &artworkPath) override;
  void clearPendingArtworkForWidget(ItemWidget *widget) override;
  void clearWidgetReferences() override;
  static QString findArtworkForFile(const QString &fileName, const QString &artworkDirectory);
  void scheduleViewportUpdate() override;
  void startSilentLoading();
  void startEarlyDentryPrewarm(int collectionIndex) override;
  void preloadArtworkForCollection();
  void stopSilentLoading() override;
  void processPersistentSilentLoad();
  void processContinuousSilentLoad();
  void updateUserActivity() override;
  [[nodiscard]] bool isUserIdle() const;
  [[nodiscard]] bool isSilentLoadingActive() const { return m_silentLoadingActive; }
  [[nodiscard]] bool hasArtworkForWidget(ItemWidget *widget) const override;
  void updateViewportArtwork() override;
  void initializeCache();
  void clearLoadedArtworkState() override;

  // ─── Per-item artwork-type override ─────────────────────────
  // The shift+middle-click gesture cycles the displayed artwork through the
  // item's available types. The chosen type id is stashed in
  // `m_artworkTypeOverrides` keyed on the absolute file path, so widget
  // recycling re-applies the override when the same item scrolls back into
  // view. Empty string == "the legacy flat-directory artwork (primary)".
  void cycleArtworkType(ItemWidget *widget, const QString &fullPath, int collectionIndex) override;
  [[nodiscard]] QString artworkTypeOverrideFor(const QString &fullPath) const override;
  void clearArtworkTypeOverrides();
  [[nodiscard]] TimerUtils::Coordinator *getTimerCoordinator() const override;

  [[nodiscard]] static QPixmap createProcessedArtwork(const QPixmap &originalPixmap);
  [[nodiscard]] QPixmap getCachedPixmap(const QString &artworkPath);
  [[nodiscard]] QPixmap loadArtworkFromFile(const QString &artworkPath);

  ~ArtworkManager() override;

private:
  // Kartend-davi: cacheManager is reached through the app context instead of
  // a cached field. m_ctx is set in setupReferences (which must precede
  // initializeCache — see MainWindow::setupArtworkManager). The accessor
  // returns ICacheManager*; every previously-CacheManager-typed call site
  // uses interface methods (cacheArtwork, scheduleSaveToDisk, etc.) so the
  // narrower role surface is sufficient.
  const ApplicationContext *m_ctx = nullptr;
  [[nodiscard]] ICacheManager *cacheMgr() const;

  /// Applies processed artwork results to UI widgets on the GUI thread.
  void applyResultsToUi(const QList<ArtworkInfo::Result> &batchResults);
  void collectUncachedAndApplyCached(const QList<ArtworkInfo> &items,
                                     QList<ArtworkInfo> &uncachedItems);
  /// Dispatcher-completion handler for silent-load precache batches. Runs on
  /// the main thread; marks paths cached, writes pixmaps to the in-memory
  /// cache, and stamps the last-batch completion timestamp.
  void onSilentPrecacheBatchComplete(const QStringList &requestedPaths,
                                     const QList<ArtworkPrecacheResult> &results);

  QList<CollectionConfig> *collections;
  int *currentCollectionIndex;

  QStackedWidget *stackedWidget;
  QWidget *itemsPage;
  QWidget *gridContainer;
  InteractionStateHolder *m_state;
  UIReferences ui;

  TimerUtils::Coordinator *m_timerCoordinator;
  QTimer m_silentLoadTimer; // Kartend-a911.6: value member
  QTimer m_persistentLoadTimer;
  bool m_persistentLoadTimerInited = false;
  QTimer m_cacheTimer;

  /// Catalog of artwork file paths discovered for the active
  /// collection plus the silent-load progression state (cursor + the
  /// silently-cached / silent-pending sets) that consumes that list.
  /// Internal mutex; safe to call from any thread.
  ArtworkPathCatalog m_pathCatalog;
  /// Per-widget artwork bookkeeping (loaded set, widget→path map,
  /// pending queue, per-item type overrides). Owns its own QMutex and
  /// the destroyed-cleanup connections installed by track(). Parented
  /// to this ArtworkManager so destruction order is well-defined.
  ArtworkWidgetRegistry *m_widgetRegistry = nullptr;
  /// Async batch dispatcher: owns the dedicated worker pool, the
  /// in-flight QFuture set, and the cooperative cancellation token.
  /// Parented to this ArtworkManager — its destructor drains the pool
  /// before this destructor proceeds, so no callback can fire after.
  ArtworkLoadDispatcher *m_dispatcher = nullptr;

  bool m_silentLoadingActive;
  int m_silentLoadBatchSize;
  std::atomic<qint64> m_lastUserActivity;
  std::atomic<qint64> m_lastBatchCompletionTime; // For silent load cooldown
  bool m_continuousSilentLoad;
  bool m_persistentSilentLoad;

  // Adaptive batching for performance-based batch sizing
  AdaptiveBatcher m_adaptiveBatcher;

  /// Checks if artwork loading should be skipped due to shutdown or invalid
  /// state.
  [[nodiscard]] bool shouldSkipArtworkLoading();
  void clearArtworkWidgetState();
  [[nodiscard]] bool isArtworkSuppressed() const;
};

#endif