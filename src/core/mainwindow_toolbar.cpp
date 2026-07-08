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
#include <QTimer>

#include <algorithm>

#include "applicationmanager.h"
#include "detailspane.h"
#include "detailspanemanager.h"
#include "errorpresentation.h"
#include "isettingsmanager.h"
#include "mainwindow.h"
#include "scrollmanager.h"
#include "textzoom.h"
#include "textzoomhud.h"
#include "toolbarcontroller.h"
#include "ui_mainwindow.h"
#include "uiconstants/timing.h"
#include "videopreviewwidget.h"

void MainWindow::setupTextZoomShortcuts() {
  // Three application-context QActions: zoom in (Ctrl+=), zoom out
  // (Ctrl+-), reset (Ctrl+0). The lambdas capture `this` so applyTextZoom
  // can dispatch the cascade refresh — the shortcuts stay live regardless
  // of which child widget has focus.
  static constexpr int kStep = 10;
  auto *zoomIn = new QAction(tr("Zoom Text In"), this);
  // Bind only Ctrl+= (NOT Ctrl++ / Qt::Key_Plus): on a US keyboard '+' is
  // Shift+'=', so Ctrl+Plus is the very same physical chord as the grid
  // add-column shortcut Ctrl+Shift+= (MenuController::setupGridWidthActions).
  // Binding both made that chord ambiguous, so Qt fired neither and grid
  // add-column silently broke. Ctrl+= handles zoom-in; Ctrl+Shift+= is grid.
  zoomIn->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal));
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

void MainWindow::setupCommandPaletteShortcut() {
  auto *paletteAction = new QAction(tr("Command Palette"), this);
  // Ctrl+Shift+P is the Spotlight / VSCode convention so users don't
  // have to learn a new chord. Application-context so the palette is
  // reachable from any focused widget without an explicit Tab-out.
  paletteAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P));
  paletteAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(paletteAction);
  connect(paletteAction, &QAction::triggered, this, &MainWindow::openCommandPalette);
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
    ui->previewVolumeSlider->setValue(m_generalSettings.media.previewVideoVolume);
  }
  VideoPreviewWidget::setGlobalVolume(m_generalSettings.media.previewVideoVolume);

  connect(ui->previewVolumeSlider, &QSlider::valueChanged, this, [this](int value) {
    if (value == m_generalSettings.media.previewVideoVolume) {
      return;
    }
    m_generalSettings.media.previewVideoVolume = value;
    VideoPreviewWidget::setGlobalVolume(value);
    // The audible volume above applies per tick; the full-INI persist is
    // debounced — a slider drag emits valueChanged once per pixel and used
    // to pay a QSettings write + sync for each one.
    scheduleGeneralSettingsSave();
  });
}

void MainWindow::scheduleGeneralSettingsSave() {
  // Lazily constructed: many sessions never touch a debounced-save path.
  if (!m_generalSettingsSaveTimer) {
    // Single-shot restarted per edit (trailing edge): the burst's last edit
    // wins the window and only one INI write lands once input settles.
    m_generalSettingsSaveTimer = new QTimer(this);
    m_generalSettingsSaveTimer->setSingleShot(true);
    m_generalSettingsSaveTimer->setInterval(UIConstants::Timing::SETTINGS_LIVE_SAVE_DEBOUNCE_MS);
    connect(m_generalSettingsSaveTimer, &QTimer::timeout, this, [this]() {
      if (m_isShuttingDown || !m_appManager->getSettingsManager()) {
        return;
      }
      // The edit sites already mirrored their state into m_generalSettings;
      // this flush only pays the disk write. userInitiated=true so a failed
      // write surfaces once per settled burst instead of once per tick.
      ErrorPresentation::reportSaveResult(
          m_appManager->getSettingsManager()->saveGeneralSettings(m_generalSettings),
          "general settings", true);
    });
  }
  m_generalSettingsSaveTimer->start();
}

void MainWindow::flushPendingGeneralSettingsSave() {
  if (!m_generalSettingsSaveTimer || !m_generalSettingsSaveTimer->isActive()) {
    return;
  }
  m_generalSettingsSaveTimer->stop();
  if (m_appManager->getSettingsManager()) {
    // Shutdown path: no dialogs — log-only reporting for a failed write.
    ErrorPresentation::reportSaveResult(
        m_appManager->getSettingsManager()->saveGeneralSettings(m_generalSettings),
        "general settings", false);
  }
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
  if (clamped == TextZoom::percent() && clamped == m_generalSettings.appearance.uiTextZoomPercent) {
    return;
  }
  TextZoom::setPercent(clamped);
  m_generalSettings.appearance.uiTextZoomPercent = clamped;
  // Holding Ctrl+= auto-repeats this slot; the INI write is debounced so a
  // repeat burst persists once after it settles instead of once per step.
  scheduleGeneralSettingsSave();
  // Re-push the global font with the new multiplier baked in.
  applyGlobalUiFont(m_generalSettings);
  // Re-run sidebar appearance so its font baselines pick up the new zoom.
  if (m_appManager->getDetailsPaneManager()) {
    m_appManager->getDetailsPaneManager()->applySidebarStateForCollection(m_currentCollectionIndex);
  }
  // Tear down + rebuild the virtual scroll content so item widgets are
  // re-instantiated with the new scaled fontSize. Coverflow uses the same
  // scroll module entry point, so this covers grid, list, and 3D modes.
  // Debounced: the rebuild is the expensive part of a zoom step, and a
  // key-repeat burst only needs the final zoom level's layout. The HUD and
  // the font push above stay immediate for per-step feedback.
  if (!m_textZoomRebuildTimer) {
    // Single-shot restarted per step (trailing edge), same shape as the
    // grid-width debouncer's precalc stage.
    m_textZoomRebuildTimer = new QTimer(this);
    m_textZoomRebuildTimer->setSingleShot(true);
    m_textZoomRebuildTimer->setInterval(UIConstants::Timing::LONG_DELAY_MS);
    connect(m_textZoomRebuildTimer, &QTimer::timeout, this, [this]() {
      if (m_isShuttingDown || !m_appManager->getScrollManager()) {
        return;
      }
      m_appManager->getScrollManager()->preCalculateLayout();
      m_appManager->getScrollManager()->forceVirtualViewUpdate();
    });
  }
  m_textZoomRebuildTimer->start();
}

void MainWindow::applyToolbarCustomization() {
  if (m_toolbarController) {
    m_toolbarController->applyToolbarCustomization(m_generalSettings);
  }
}
