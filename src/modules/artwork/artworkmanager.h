#ifndef ARTWORKMANAGER_H
#define ARTWORKMANAGER_H

#include <qcontainerfwd.h>
#include <qlist.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include <QFuture>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QPair>
#include <QPixmap>
#include <QPointer>
#include <QSet>
#include <atomic>

#include "adaptivebatcher.h"
#include "setuputils.h"

class QScrollArea;
class QStackedWidget;
class QWidget;
class ItemWidget;
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
  };
};

struct ApplicationContext;

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
  
  SETUP_GETTER_DECL(QStackedWidget*, StackedWidget)
  SETUP_GETTER_DECL(QWidget*, ItemsPage)
  SETUP_GETTER_DECL(QWidget*, GridContainer)
  SETUP_GETTER_DECL(QScrollArea*, ItemScrollArea)
  SETUP_GETTER_DECL(QList<CollectionConfig>*, Collections)
  SETUP_GETTER_DECL(int*, CurrentCollectionIndex)
  SETUP_GETTER_DECL_CTX_ONLY(InteractionStateHolder*, InteractionState)
};

struct UIReferences {
  QScrollArea *itemScrollArea;
};

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
  static QString findArtworkForFile(const QString &fileName,
                                    const QString &artworkDirectory);
  void scheduleViewportUpdate();
  void startSilentLoading();
  void preloadArtworkForCollection();
  void stopSilentLoading();
  void processPersistentSilentLoad();
  void processContinuousSilentLoad();
  void updateUserActivity();
  [[nodiscard]] bool isUserIdle() const;
  [[nodiscard]] bool isSilentLoadingActive() const { return m_silentLoadingActive; }
  void updateViewportArtwork();
  void buildArtworkPathsList();
  void
  addSubcollectionArtworkPathsWithDedup(int parentIndex,
                                        QSet<QString> &processedDirectories);
  void initializeCache();
  void clearLoadedArtworkState();
  [[nodiscard]] TimerUtils::Coordinator *getTimerCoordinator() const;

  [[nodiscard]] static QPixmap createProcessedArtwork(const QPixmap &originalPixmap);
  [[nodiscard]] QPixmap getCachedPixmap(const QString &artworkPath);
  [[nodiscard]] QPixmap loadArtworkFromFile(const QString &artworkPath);

  ~ArtworkManager() override;

private:
  CacheManager *m_cacheManager;
  void trackWidget(ItemWidget *widget);
  /// Applies processed artwork results to UI widgets on the GUI thread.
  void applyResultsToUi(const QList<ArtworkInfo::Result> &batchResults,
                        bool highPriority);
  void collectUncachedAndApplyCached(const QList<ArtworkInfo> &items,
                                     QList<ArtworkInfo> &uncachedItems);
  void dispatchAndTrackBatch(const QList<ArtworkInfo> &batch,
                             bool highPriority);
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

  QSet<ItemWidget *> loadedArtwork;
  QHash<ItemWidget *, QString> widgetToArtworkPath;
  QList<ArtworkInfo> pendingArtwork;
  QSet<QString> m_silentlyCachedPaths;
  QStringList m_allArtworkPaths;

  bool m_silentLoadingActive;
  int m_silentLoadBatchSize;
  std::atomic<qint64> m_lastUserActivity;
  std::atomic<bool> m_cancellationRequested{false};  // For cooperative cancellation
  bool m_continuousSilentLoad;
  int m_silentLoadIndex;
  bool m_persistentSilentLoad;

  // Adaptive batching for performance-based batch sizing
  AdaptiveBatcher m_adaptiveBatcher;

  QMutex m_dataMutex;
  QMutex m_futureMutex;
  QList<QFuture<void>> m_futures;
  void appendArtworkFromDir(const QString &dirPath,
                            QSet<QString> &processedDirectories);
  /// Checks if artwork loading should be skipped due to shutdown or invalid
  /// state.
  [[nodiscard]] bool shouldSkipArtworkLoading();
  void clearArtworkWidgetState();
  [[nodiscard]] bool isArtworkSuppressed() const;
};

#endif