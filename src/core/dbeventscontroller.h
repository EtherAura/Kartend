#ifndef DBEVENTSCONTROLLER_H
#define DBEVENTSCONTROLLER_H

// Controller extracted from MainWindow (Kartend-hzef step 2).
// Owns MainWindow's reactions to DatabaseManager scan/count signals — the
// slot bodies previously living in mainwindow_dbevents.cpp. The host
// (MainWindow) constructs the controller in its ctor, calls setContext()
// once managers exist, and wires DatabaseManager's signals straight to
// the controller's slots in mainwindow_wiring.cpp instead of MainWindow's.
//
// Owns the scan-counter state too — m_activeScanCount,
// m_startupActiveScanCount, m_suppressStartupScanOverlays — which the
// dbevents slots are the only readers/writers of, except for one
// external writer: mainwindow_timers.cpp's loadInitialCollection sets
// "startup suppression" when it kicks off the first round of scans.
// That goes through setSuppressStartupScanOverlays(true) on the
// controller.

#include <functional>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

class DetailsPaneManager;
class LoadingOverlay;
class NavigationManager;
struct CollectionConfig;

template <typename T> class QList;

struct DbEventsControllerContext {
  /// Loading-overlay handle. Read across 4 of the 8 slots; MainWindow still
  /// owns the widget so we route through a getter rather than a raw cached
  /// pointer.
  std::function<LoadingOverlay *()> getLoadingOverlay;
  std::function<NavigationManager *()> getNavigationManager;
  std::function<DetailsPaneManager *()> getDetailsPaneManager;
  std::function<int()> getCurrentCollectionIndex;
  std::function<const QList<CollectionConfig> *()> getCollections;

  /// Callbacks back into MainWindow for surfaces the controller doesn't own.
  /// refreshTitleCounts updates the window-title item counts after a cached
  /// counts refresh; refreshFilterToolbar repopulates the consolidated filter
  /// popup post-itemsLoaded; setWindowTitle is the QMainWindow API the scan-
  /// starting slot uses to show "Scanning..." mid-flight.
  std::function<void()> refreshTitleCounts;
  std::function<void()> refreshFilterToolbar;
  std::function<void(const QString &)> setWindowTitle;
};

class DbEventsController : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(DbEventsController)
public:
  explicit DbEventsController(QObject *parent = nullptr);
  ~DbEventsController() override;

  void setContext(const DbEventsControllerContext &context);

  /// Read by mainwindow_timers.cpp's loadInitialCollection to enable
  /// the startup-suppression mode before the first round of scans
  /// kicks off. The controller's own slots clear it as scans
  /// complete.
  void setSuppressStartupScanOverlays(bool suppress) { m_suppressStartupScanOverlays = suppress; }
  [[nodiscard]] bool suppressStartupScanOverlays() const { return m_suppressStartupScanOverlays; }

public slots:
  void releaseStartupOverlaySuppressionIfIdle(int count);
  void refreshTitleCountsIfActive();
  void refreshFilterToolbarOnItemsLoaded(const QStringList &paths,
                                         const QHash<QString, QString> &names);
  void onScanProgress(int current, int total, const QString &name);
  void onScanStarting(const QString &name, int estimatedItems);
  void onCollectionScanCompletedStartup(const QString &uuid);
  void onCollectionScanCompletedOverlay(const QString &uuid);
  void onScanItemsProgress(int itemsProcessed, int totalItems);
  void refreshCollectionSummaryOnScanCompleted(const QString &uuid);

private:
  DbEventsControllerContext m_ctx;

  bool m_suppressStartupScanOverlays = false;
  // Counter of in-flight collectionScanStarting calls whose
  // collectionScanCompleted hasn't fired yet while suppression is on. When
  // the count reaches zero, suppression releases.
  int m_startupActiveScanCount = 0;
  // Counter of in-flight scans regardless of suppression mode — used by
  // onCollectionScanCompletedOverlay to detect when the last batch finishes
  // and the loading overlay should hide.
  int m_activeScanCount = 0;
};

#endif // DBEVENTSCONTROLLER_H
