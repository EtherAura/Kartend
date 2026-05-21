#ifndef SCROLLEVENTSCONTROLLER_H
#define SCROLLEVENTSCONTROLLER_H

// Controller extracted from MainWindow (Kartend-hzef step 1).
// Owns MainWindow's reactions to ScrollManager view-mode / column-resize /
// CoverFlow activation signals — the slot bodies previously living in
// mainwindow_scrollevents.cpp. The host (MainWindow) constructs the
// controller in its ctor, calls setContext() once managers exist, and
// wires ScrollManager's signals straight to the controller's slots in
// mainwindow_wiring.cpp instead of MainWindow's own slots.

#include <functional>
#include <QObject>

#include "collectiontypes.h"

class DetailsPaneManager;
class IDatabaseManager;
class InteractionManager;
class ISettingsManager;
class NavigationManager;
class ScrollManager;
struct GeneralSettings;

struct ScrollEventsControllerContext {
  std::function<NavigationManager *()> getNavigationManager;
  std::function<InteractionManager *()> getInteractionManager;
  std::function<ScrollManager *()> getScrollManager;
  std::function<ISettingsManager *()> getSettingsManager;
  std::function<DetailsPaneManager *()> getDetailsPaneManager;
  std::function<IDatabaseManager *()> getDatabaseManager;

  /// Mutable handle to MainWindow's GeneralSettings — the sort-mode and
  /// list-column-width slots write through it before asking
  /// SettingsManager to persist.
  std::function<GeneralSettings *()> getGeneralSettings;
  /// Current collection index. Used for safeReloadCollection on sort
  /// change and as a fallback owner when CoverFlow item-activate can't
  /// resolve the owning collection from DatabaseManager.
  std::function<int()> getCurrentCollectionIndex;
};

class ScrollEventsController : public QObject {
  Q_OBJECT
public:
  explicit ScrollEventsController(QObject *parent = nullptr);
  ~ScrollEventsController() override;

  void setContext(const ScrollEventsControllerContext &context);

public slots:
  void onSortModeChangeRequested(SortMode sortMode);
  void onSelectItemByIndex(int index);
  void onCoverFlowActiveChanged(bool active);
  void onArtworkPreviewVisibilityChanged(bool visible);
  void onCoverFlowItemActivated(int index);
  void onListColumnWidthChanged(int width);
  void onListArtworkColumnWidthChanged(int width);

private:
  ScrollEventsControllerContext m_ctx;
};

#endif // SCROLLEVENTSCONTROLLER_H
