// Toolbar wiring + appearance and the user-facing keyboard shortcuts that
// drive it (text zoom, preview-video pause, view-mode picker, search-mode
// action, preview volume slider). Extracted from mainwindow.cpp so that file
// can stay focused on coordination + lifecycle.

#include <QAction>
#include <QActionGroup>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>

#include <algorithm>

#include "detailspane.h"
#include "detailspanemanager.h"
#include "mainwindow.h"
#include "scrollmanager.h"
#include "settingsmanager.h"
#include "textzoom.h"
#include "textzoomhud.h"
#include "ui_mainwindow.h"
#include "uiconstants.h"
#include "videopreviewwidget.h"

void MainWindow::setupTextZoomShortcuts() {
  // Three application-context QActions: zoom in (Ctrl++ / Ctrl+=), zoom out
  // (Ctrl+-), reset (Ctrl+0). The lambdas capture `this` so applyTextZoom
  // can dispatch the cascade refresh — the shortcuts stay live regardless
  // of which child widget has focus.
  static constexpr int kStep = 10;
  auto *zoomIn = new QAction(tr("Zoom Text In"), this);
  // The platform-default Ctrl++ shortcut comes through as Qt::Key_Plus on
  // some keyboards and Qt::Key_Equal on others; bind both so US/EU layouts
  // are equally happy.
  zoomIn->setShortcuts(
      {QKeySequence(Qt::CTRL | Qt::Key_Plus), QKeySequence(Qt::CTRL | Qt::Key_Equal)});
  zoomIn->setShortcutContext(Qt::ApplicationShortcut);
  addAction(zoomIn);
  connect(zoomIn, &QAction::triggered, this,
          [this]() { applyTextZoom(textZoomPercent() + kStep); });

  auto *zoomOut = new QAction(tr("Zoom Text Out"), this);
  zoomOut->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus));
  zoomOut->setShortcutContext(Qt::ApplicationShortcut);
  addAction(zoomOut);
  connect(zoomOut, &QAction::triggered, this,
          [this]() { applyTextZoom(textZoomPercent() - kStep); });

  auto *zoomReset = new QAction(tr("Reset Text Zoom"), this);
  zoomReset->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
  zoomReset->setShortcutContext(Qt::ApplicationShortcut);
  addAction(zoomReset);
  connect(zoomReset, &QAction::triggered, this, [this]() { applyTextZoom(100); });
}

void MainWindow::setupVideoPauseShortcut() {
  auto *pauseVideo = new QAction(tr("Pause/Resume Preview Video"), this);
  // Ctrl+K mirrors YouTube's universal pause shortcut. Plain K alone would
  // collide with character input in the search bar; Space is consumed by
  // coverflow navigation.
  pauseVideo->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
  pauseVideo->setShortcutContext(Qt::ApplicationShortcut);
  addAction(pauseVideo);
  connect(pauseVideo, &QAction::triggered, this, [this]() {
    if (m_MetadataSidebar) {
      m_MetadataSidebar->togglePreviewVideoPause();
    }
  });
}

void MainWindow::setupViewModeButton() {
  if (!m_viewModeButton) {
    return;
  }
  // keep the toolbar layout-picker visually in lockstep with the
  // View → Layout menu by mounting a popup of text-only entries (Grid / List /
  // Cover Flow / Horizontal). The four legacy individual icon buttons used to
  // do this — collapsed here into a single Breeze-icon button so the toolbar
  // doesn't carry one slot per layout.
  auto *menu = new QMenu(m_viewModeButton);
  auto *group = new QActionGroup(menu);
  group->setExclusive(true);

  auto addEntry = [&](const QString &text, ViewType type) {
    QAction *action = menu->addAction(text);
    action->setCheckable(true);
    group->addAction(action);
    connect(action, &QAction::triggered, this, [this, type]() { setViewType(type); });
    return action;
  };
  m_viewActionGrid = addEntry(tr("&Grid"), ViewType::Grid);
  m_viewActionList = addEntry(tr("&List"), ViewType::List);
  m_viewActionCoverFlow = addEntry(tr("&Cover Flow"), ViewType::CoverFlow);
  m_viewActionHorizontal = addEntry(tr("&Horizontal"), ViewType::Horizontal);

  m_viewModeButton->setMenu(menu);
  m_viewModeButton->setIcon(
      UIConstants::Icons::fromTheme({UIConstants::Icons::VIEW_PICKER, "view-list-icons"}));
  m_viewModeButton->setIconSize(QSize(18, 18));
}

void MainWindow::syncViewModeButton(ViewType viewType) {
  const auto setChecked = [](QAction *action, bool on) {
    if (action) {
      QSignalBlocker blocker(action);
      action->setChecked(on);
    }
  };
  setChecked(m_viewActionGrid, viewType == ViewType::Grid);
  setChecked(m_viewActionList, viewType == ViewType::List);
  setChecked(m_viewActionCoverFlow, viewType == ViewType::CoverFlow);
  setChecked(m_viewActionHorizontal, viewType == ViewType::Horizontal);
  if (m_viewModeButton) {
    QString tip;
    switch (viewType) {
    case ViewType::Grid:
      tip = tr("Layout: Grid (click to change)");
      break;
    case ViewType::List:
      tip = tr("Layout: List (click to change)");
      break;
    case ViewType::CoverFlow:
      tip = tr("Layout: Cover Flow (click to change)");
      break;
    case ViewType::Horizontal:
      tip = tr("Layout: Horizontal (click to change)");
      break;
    }
    m_viewModeButton->setToolTip(tip);
  }
}

void MainWindow::setupSearchModeAction() {
  if (!searchBar) {
    return;
  }
  // The search-mode toggle lives inside the QLineEdit (LeadingPosition) rather
  // than as a sibling QPushButton — keeps the toolbar tighter and gives the
  // search field a familiar magnifier-glass affordance. The triggered() signal
  // is wired later in connectSearchComponents() once InteractionManager is
  // alive.
  m_searchModeAction = searchBar->addAction(
      UIConstants::Icons::fromTheme(UIConstants::Icons::SEARCH), QLineEdit::LeadingPosition);
  if (m_searchModeAction) {
    m_searchModeAction->setToolTip(tr("Toggle search scope"));
  }
}

void MainWindow::setupPreviewVolumeSlider() {
  // bind the toolbar volume slider to the static volume hook
  // on VideoPreviewWidget. The slider's initial value is set from persisted
  // settings; subsequent moves push to the hook AND back into settings so
  // the value survives restarts.
  if (!ui->previewVolumeSlider) {
    return;
  }
  {
    QSignalBlocker blocker(ui->previewVolumeSlider);
    ui->previewVolumeSlider->setValue(m_generalSettings.previewVideoVolume);
  }
  VideoPreviewWidget::setGlobalVolume(m_generalSettings.previewVideoVolume);

  connect(ui->previewVolumeSlider, &QSlider::valueChanged, this, [this](int value) {
    if (value == m_generalSettings.previewVideoVolume) {
      return;
    }
    m_generalSettings.previewVideoVolume = value;
    VideoPreviewWidget::setGlobalVolume(value);
    if (getSettingsManager()) {
      getSettingsManager()->saveGeneralSettings(m_generalSettings);
    }
  });
}

void MainWindow::applyTextZoom(int percent) {
  const int clamped = std::clamp(percent, 50, 300);
  // Always surface the HUD, even when the value didn't change — that's the
  // signal to the user that the keypress was received and they're already at
  // the floor/ceiling. Without this, pressing Ctrl+- at 50% would feel like
  // the shortcut wasn't registered.
  if (m_textZoomHud) {
    m_textZoomHud->showZoom(clamped);
  }
  if (clamped == TextZoom::percent() && clamped == m_generalSettings.uiTextZoomPercent) {
    return;
  }
  TextZoom::setPercent(clamped);
  m_generalSettings.uiTextZoomPercent = clamped;
  if (getSettingsManager()) {
    getSettingsManager()->saveGeneralSettings(m_generalSettings);
  }
  // Re-push the global font with the new multiplier baked in.
  applyGlobalUiFont(m_generalSettings);
  // Re-run sidebar appearance so its font baselines pick up the new zoom.
  if (getDetailsPaneManager()) {
    getDetailsPaneManager()->applySidebarStateForCollection(currentCollectionIndex);
  }
  // Tear down + rebuild the virtual scroll content so item widgets are
  // re-instantiated with the new scaled fontSize. Coverflow uses the same
  // scroll module entry point, so this covers grid, list, and 3D modes.
  if (getScrollManager()) {
    getScrollManager()->preCalculateLayout();
    getScrollManager()->forceVirtualViewUpdate();
  }
}

void MainWindow::applyToolbarCustomization() {
  if (!ui) {
    return;
  }
  const auto &gs = m_generalSettings;

  // The legacy per-view-button visibility flags (toolbarShowGridViewButton et
  // al.) and the hide-subcollections / search-mode flags are kept in
  // GeneralSettings for backward compat, but the underlying buttons have been
  // removed (single viewModeButton, in-field search action, single
  // filterButton). Only the consolidated filter button and the search bar
  // remain user-toggleable from this codepath; the filter button stays on
  // when *either* legacy flag (type or title) is on so existing settings
  // don't accidentally hide it.
  if (ui->filterButton) {
    ui->filterButton->setVisible(gs.toolbarShowTypeFilter || gs.toolbarShowTitleFilter);
  }
  if (ui->searchBar) {
    ui->searchBar->setVisible(gs.toolbarShowSearchBar);
  }
}
