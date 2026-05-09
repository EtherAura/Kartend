#ifndef ARTWORKMANAGER_H
#define ARTWORKMANAGER_H

#include <atomic>
#include <memory>
#include <QFuture>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QPair>
#include <QPixmap>
#include <QPointer>
#include <QSet>
#include <QThreadPool>

#include "adaptivebatcher.h"
#include "artworkpathcatalog.h"
#include "itemwidget.h"
#include "setuputils.h"

class ArtworkWidgetRegistry;

class QScrollArea;
class QStackedWidget;
class QWidget;
class QTimer;
class CacheManager;
class InteractionStateHolder;

namespace TimerUtils {
class Coordinator;
}

struct CollectionConfig;
class QJsonObject;

struct ArtworkInfo {
  QPointer<ItemWidget> mediaItem;
  QString artworkPath;

  struct Result {
    QPointer<ItemWidget> widget;
    QString artworkPath;
    QImage image;
    bool loadedFromDiskCache = false;
  };
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
class ArtworkManager : public QObject {
  Q_OBJECT

public:
  explicit ArtworkManager(CacheManager *cacheManager, QObject *parent = nullptr);

  void setupReferences(const ArtworkManagerSetup &setup);
  void loadArtworkParallel(const QList<ArtworkInfo> &items, bool highPriority,
                           int customBatchSize = 0);
  void cancelAllArtworkLoading();
  void addPendingArtwork(ItemWidget *widget, const QString &artworkPath);
  void clearPendingArtworkForWidget(ItemWidget *widget);
  void clearWidgetReferences();
  static QString findArtworkForFile(const QString &fileName, const QString &artworkDirectory);
  void scheduleViewportUpdate();
  void startSilentLoading();
  void startEarlyDentryPrewarm(int collectionIndex);
  void preloadArtworkForCollection();
  void stopSilentLoading();
  void processPersistentSilentLoad();
  void processContinuousSilentLoad();
  void updateUserActivity();
  [[nodiscard]] bool isUserIdle() const;
  [[nodiscard]] bool isSilentLoadingActive() const { return m_silentLoadingActive; }
  [[nodiscard]] bool hasArtworkForWidget(ItemWidget *widget) const;
  void updateViewportArtwork();
  void initializeCache();
  void clearLoadedArtworkState();

  // ─── Per-item artwork-type override ─────────────────────────
  // The shift+middle-click gesture cycles the displayed artwork through the
  // item's available types. The chosen type id is stashed in
  // `m_artworkTypeOverrides` keyed on the absolute file path, so widget
  // recycling re-applies the override when the same item scrolls back into
  // view. Empty string == "the legacy flat-directory artwork (primary)".
  void cycleArtworkType(ItemWidget *widget, const QString &fullPath, int collectionIndex);
  [[nodiscard]] QString artworkTypeOverrideFor(const QString &fullPath) const;
  void clearArtworkTypeOverrides();
  [[nodiscard]] TimerUtils::Coordinator *getTimerCoordinator() const;

  [[nodiscard]] static QPixmap createProcessedArtwork(const QPixmap &originalPixmap);
  [[nodiscard]] QPixmap getCachedPixmap(const QString &artworkPath);
  [[nodiscard]] QPixmap loadArtworkFromFile(const QString &artworkPath);

  ~ArtworkManager() override;

private:
  CacheManager *m_cacheManager;
  /// Applies processed artwork results to UI widgets on the GUI thread.
  void applyResultsToUi(const QList<ArtworkInfo::Result> &batchResults);
  void collectUncachedAndApplyCached(const QList<ArtworkInfo> &items,
                                     QList<ArtworkInfo> &uncachedItems);
  void dispatchAndTrackBatch(const QList<ArtworkInfo> &batch, bool highPriority);
  void dispatchAndTrackPrecacheBatch(const QStringList &artworkPaths);
  void pruneFinishedFutures();

  QList<CollectionConfig> *collections;
  int *currentCollectionIndex;

  QStackedWidget *stackedWidget;
  QWidget *itemsPage;
  QWidget *gridContainer;
  InteractionStateHolder *m_state;
  UIReferences ui;

  TimerUtils::Coordinator *m_timerCoordinator;
  QTimer *m_silentLoadTimer;
  QTimer *m_persistentLoadTimer;
  QTimer *m_cacheTimer;

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

  bool m_silentLoadingActive;
  int m_silentLoadBatchSize;
  std::atomic<qint64> m_lastUserActivity;
  std::atomic<qint64> m_lastBatchCompletionTime;              // For silent load cooldown
  std::shared_ptr<std::atomic<bool>> m_cancellationRequested; // For cooperative cancellation
  bool m_continuousSilentLoad;
  bool m_persistentSilentLoad;

  // Adaptive batching for performance-based batch sizing
  AdaptiveBatcher m_adaptiveBatcher;

  // Dedicated pool for artwork processing to avoid contention with other
  // QtConcurrent users (e.g., directory scans).
  // Raw pointer so we can abandon it on shutdown without waiting.
  QThreadPool *m_artworkThreadPool = nullptr;

  QMutex m_futureMutex;
  QList<QFuture<void>> m_futures;
  /// Checks if artwork loading should be skipped due to shutdown or invalid
  /// state.
  [[nodiscard]] bool shouldSkipArtworkLoading();
  void clearArtworkWidgetState();
  [[nodiscard]] bool isArtworkSuppressed() const;
};

#endif