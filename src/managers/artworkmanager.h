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
#include <mutex>

class QScrollArea;
class QStackedWidget;
class QWidget;
class MediaItemWidget;
class QTimer;

namespace TimerUtils {
class Coordinator;
}

struct CollectionConfig;
class QJsonObject;

struct ArtworkInfo {
  QPointer<MediaItemWidget> mediaItem;
  QString artworkPath;

  struct Result {
    QPointer<MediaItemWidget> widget;
    QString artworkPath;
    QImage image;
  };
};

struct ArtworkManagerSetup {
  QStackedWidget *stackedWidget = nullptr;
  QWidget *itemsPage = nullptr;
  QWidget *gridContainer = nullptr;
  QScrollArea *itemScrollArea = nullptr;
  QList<CollectionConfig> *collections = nullptr;
  int *currentCollectionIndex = nullptr;
};

struct UIReferences {
  QScrollArea *itemScrollArea;
};

class ArtworkManager : public QObject {
  Q_OBJECT

public:
  static ArtworkManager &instance();

  void setupReferences(const ArtworkManagerSetup &setup);
  void loadArtworkParallel(const QList<ArtworkInfo> &items, bool highPriority,
                           int customBatchSize = 0);
  void cancelAllArtworkLoading();
  void addPendingArtwork(MediaItemWidget *widget, const QString &artworkPath);
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
  bool isUserIdle() const;
  void updateViewportArtwork();
  void buildArtworkPathsList();
  void
  addSubcollectionArtworkPathsWithDedup(int parentIndex,
                                        QSet<QString> &processedDirectories);
  static void initializeCache();
  void clearLoadedArtworkState();
  TimerUtils::Coordinator *getTimerCoordinator() const;

  static QPixmap createProcessedArtwork(const QPixmap &originalPixmap);
  static QPixmap getCachedPixmap(const QString &artworkPath);
  static QPixmap loadArtworkFromFile(const QString &artworkPath);

  static std::atomic<ArtworkManager *> s_instance;
  static std::atomic<bool> s_shuttingDown;
  static std::mutex s_mutex;

  void shutdown();
  ~ArtworkManager() override;
  static void cleanup();

private:
  explicit ArtworkManager(QObject *parent = nullptr);
  void trackWidget(MediaItemWidget *widget);
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
  UIReferences ui;

  TimerUtils::Coordinator *m_timerCoordinator;
  QTimer *m_silentLoadTimer;
  QTimer *m_persistentLoadTimer;
  QTimer *m_cacheTimer;

  QSet<MediaItemWidget *> loadedArtwork;
  QHash<MediaItemWidget *, QString> widgetToArtworkPath;
  QList<ArtworkInfo> pendingArtwork;
  QSet<QString> m_silentlyCachedPaths;
  QStringList m_allArtworkPaths;

  bool m_silentLoadingActive;
  int m_silentLoadBatchSize;
  std::atomic<qint64> m_lastUserActivity;
  bool m_continuousSilentLoad;
  int m_silentLoadIndex;
  bool m_persistentSilentLoad;

  QMutex m_dataMutex;
  QMutex m_futureMutex;
  QList<QFuture<void>> m_futures;
  void appendArtworkFromDir(const QString &dirPath,
                            QSet<QString> &processedDirectories);
  /// Checks if artwork loading should be skipped due to shutdown or invalid
  /// state.
  bool shouldSkipArtworkLoading();
  void clearArtworkWidgetState();
  bool isArtworkSuppressed() const;
};

#endif