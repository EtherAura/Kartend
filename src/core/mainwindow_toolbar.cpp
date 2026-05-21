// User-facing keyboard shortcuts and preview-volume binding that hang off the
// items-page toolbar (text zoom in/out/reset, Ctrl+K preview-video pause,
// volume slider). The toolbar's own stateful widgets — layout-picker, search-
// mode action, filter button + popup — live on ToolbarController; this file
// is the home for the cross-cutting shortcuts that aren't a toolbar widget
// of their own.

#include <QAction>
#include <QKeySequence>
#include <QSignalBlocker>
#include <QSlider>

#include <algorithm>

#include "detailspane.h"
#include "detailspanemanager.h"
#include "isettingsmanager.h"
#include "mainwindow.h"
#include "scrollmanager.h"
#include "textzoom.h"
#include "textzoomhud.h"
#include "toolbarcontroller.h"
#include "ui_mainwindow.h"
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

void MainWindow::syncViewModeButton(ViewType viewType) {
  if (m_toolbarController) {
    m_toolbarController->syncViewModeButton(viewType);
  }
}

void MainWindow::setViewTypeFromToolbar(ViewType viewType) {
  setViewType(viewType);
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
  if (m_toolbarController) {
    m_toolbarController->applyToolbarCustomization(m_generalSettings);
  }
}
