// Main application window that owns ApplicationManager and orchestrates UI
// setup.
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCoreApplication>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPixmapCache>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QToolButton>

#include "applicationmanager.h"
#include "artworkmanager.h"
#include "attractmanager.h"
#include "cachemanager.h"
#include "collectionutils.h"
#include "databasemanager.h"
#include "detailpagemanager.h"
#include "detailpageoverlay.h"
#include "interactionmanager.h"
#include "kartmanager.h"
#include "kartreader.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QMimeData>
#include <QUrl>
#include "itemwidget.h"
#include "keyboardmanager.h"
#include "launchmanager.h"
#include "loadingoverlay.h"
#include "mainwindow.h"
#include "menucontroller.h"
#include "detailspane.h"
#include "navigationmanager.h"
#include "nowplayingoverlay.h"
#include "pathutils.h"
#include "playlistmanager.h"
#include "propertyutils.h"
#include "scrollmanager.h"
#include "sessionmanager.h"
#include "settingsdialog.h"
#include "settingsmanager.h"
#include "settingsutils.h"
#include "shortcutsdialog.h"
#include "detailspanemanager.h"
#include "splashoverlay.h"
#include "startupvideooverlay.h"
#include "stringutils.h"
#include "textzoomhud.h"
#include "timerutils.h"
#include "videopreviewwidget.h"
#include "ui_mainwindow.h"
#include "uiconstants.h"

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcMainWindow, "kartend.mainwindow")

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), stackedWidget(nullptr), itemsPage(nullptr),
      gridContainer(nullptr), m_mainContentWidget(nullptr), itemGrid(nullptr),
      m_mainHorizontalLayout(nullptr), searchBar(nullptr), loadingLabel(nullptr),
      currentCollectionIndex(-1), m_MetadataSidebar(nullptr) {
  m_appManager = std::make_unique<ApplicationManager>(this);
  m_appManager->initialize();

  ui->setupUi(this);
  setupUI();
}

MainWindow::~MainWindow() {
  delete ui;
}

bool MainWindow::event(QEvent *event) {
  if (event && !m_isShuttingDown && !QApplication::closingDown()) {
    switch (event->type()) {
    case QEvent::WindowDeactivate:
      if (!QApplication::activeModalWidget() && !QApplication::activePopupWidget()) {
        m_windowWasInactive = true;
      }
      break;
    case QEvent::WindowActivate:
      if (m_windowWasInactive) {
        m_windowWasInactive = false;
        if (m_startupSplashHandled && !QApplication::activeModalWidget() &&
            !QApplication::activePopupWidget()) {
          showFocusReturnSplash();
        }
      }
      break;
    default:
      break;
    }
  }

  return QMainWindow::event(event);
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
  if ((getInteractionManager()) && getInteractionManager()->handleGlobalKeyPress(event)) {
    return;
  }
  QMainWindow::keyPressEvent(event);
}

void MainWindow::showStartupSplash() {
  m_startupSplashHandled = true;
  // Kartend-y3ke: startup video plays first when enabled. The splash is
  // chained onto the video's dismissed signal so it still appears after
  // the user skips or the clip ends — keeping users with both features
  // configured from losing the splash.
  if (m_generalSettings.startupVideoEnabled &&
      !m_generalSettings.startupVideoPath.isEmpty()) {
    auto *videoOverlay = new StartupVideoOverlay(this);
    videoOverlay->setGeometry(rect());
    videoOverlay->raise();
    if (videoOverlay->playVideo(m_generalSettings.startupVideoPath)) {
      videoOverlay->show();
      connect(videoOverlay, &StartupVideoOverlay::dismissed, this, [this]() {
        if (m_generalSettings.bootSplashEnabled && m_splashOverlay) {
          m_splashOverlay->showSplash(SplashOverlay::Reason::Startup);
        }
      });
      return;
    }
    // playVideo failed (missing/unreadable path): fall through to the
    // normal splash path so the user still gets a startup indicator.
    videoOverlay->deleteLater();
  }
  if (m_generalSettings.bootSplashEnabled && m_splashOverlay) {
    m_splashOverlay->showSplash(SplashOverlay::Reason::Startup);
  }
}

void MainWindow::showFocusReturnSplash() {
  if (m_generalSettings.resumeFocusSplashEnabled && m_splashOverlay) {
    m_splashOverlay->showSplash(SplashOverlay::Reason::FocusReturn);
  }
}

void MainWindow::applyGlobalUiFont(const GeneralSettings &settings) {
  // Compose the new application font from the persisted settings, falling
  // back to whatever Qt picked at startup (read once into s_baseline so we
  // can still restore it after the user clears the override).
  static const QFont s_baseline = QApplication::font();
  QFont font = s_baseline;
  const QString family = settings.globalUiFontFamily.trimmed();
  if (!family.isEmpty()) {
    font.setFamily(family);
  }
  if (settings.globalUiFontPointSize > 0) {
    font.setPointSize(settings.globalUiFontPointSize);
  }
  // Kartend-7eff: layer the runtime text zoom on top so menus/dialogs/
  // toolbar scale at the same rate as item titles. Computed against the
  // size that was already chosen above (user override or baseline) so the
  // override stays the authoritative "100 %" reference.
  font.setPointSize(std::max(1, font.pointSize() * settings.uiTextZoomPercent / 100));
  // setFont propagates to every widget that hasn't had setFont() called on
  // it explicitly, so menus, dialogs, and toolbar text all pick this up
  // without us walking the widget tree.
  QApplication::setFont(font);
}

namespace {
// Kartend-7eff: lives outside the class so the static initializer runs once
// at first translation unit load. Defaulting to 100 (unscaled) means any
// code that calls textZoomPercent() before MainWindow has set it from
// settings still gets a sane value.
int g_textZoomPercent = 100;
} // namespace

int MainWindow::textZoomPercent() {
  return g_textZoomPercent;
}

void MainWindow::primeTextZoomFromSettings(int percent) {
  g_textZoomPercent = std::clamp(percent, 50, 300);
}

int MainWindow::zoomedFontSize(int baseSize) {
  if (baseSize <= 0 || g_textZoomPercent == 100) {
    return baseSize;
  }
  // Floor at 1pt: a fontSize of 0 confuses Qt's font system on some
  // platforms, and the user can always reset zoom rather than relying on
  // an undisplayable size as a feature.
  return std::max(1, baseSize * g_textZoomPercent / 100);
}

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
  // Kartend-iue: keep the toolbar layout-picker visually in lockstep with the
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
  m_searchModeAction =
      searchBar->addAction(UIConstants::Icons::fromTheme(UIConstants::Icons::SEARCH),
                           QLineEdit::LeadingPosition);
  if (m_searchModeAction) {
    m_searchModeAction->setToolTip(tr("Toggle search scope"));
  }
}

void MainWindow::setupPreviewVolumeSlider() {
  // Kartend-3m01: bind the toolbar volume slider to the static volume hook
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
  if (clamped == g_textZoomPercent && clamped == m_generalSettings.uiTextZoomPercent) {
    return;
  }
  g_textZoomPercent = clamped;
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

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
  if (!event || !event->mimeData() || !event->mimeData()->hasUrls()) {
    return;
  }
  for (const QUrl &url : event->mimeData()->urls()) {
    if (!url.isLocalFile()) continue;
    if (url.toLocalFile().endsWith(".kart", Qt::CaseInsensitive)) {
      event->acceptProposedAction();
      return;
    }
  }
}

void MainWindow::dropEvent(QDropEvent *event) {
  if (!event || !event->mimeData() || !event->mimeData()->hasUrls()) return;
  auto *km = getKartManager();
  if (!km) return;
  for (const QUrl &url : event->mimeData()->urls()) {
    if (!url.isLocalFile()) continue;
    const QString path = url.toLocalFile();
    if (!path.endsWith(".kart", Qt::CaseInsensitive)) continue;
    auto peeked = KartReader::peekManifest(path);
    if (peeked.isError()) {
      QMessageBox::warning(this, tr("Import Kart"), peeked.error().message);
      continue;
    }
    const QString suggested =
        QDir::homePath() + "/" +
        (peeked.value().name.isEmpty() ? QString("kart") : peeked.value().name);
    const QString destDir = QFileDialog::getExistingDirectory(
        this, tr("Import %1 to...").arg(peeked.value().name), suggested);
    if (destDir.isEmpty()) continue;
    auto res = km->importKart(path, destDir, true);
    if (res.isError()) {
      QMessageBox::warning(this, tr("Import Kart"), res.error().message);
    }
  }
  event->acceptProposedAction();
}

void MainWindow::resizeEvent(QResizeEvent *event) {
  if (!updatesEnabled()) {
    return;
  }

  QMainWindow::resizeEvent(event);

  if (getArtworkManager()) {
    if (auto *timerCoordinator = getArtworkManager()->getTimerCoordinator()) {
      timerCoordinator->scheduleLayoutUpdate();
    }
  }

  // Re-center on current selection after resize completes
  // Defer re-centering until after resize animation completes -
  // prevents visual jump during resize drag
  QTimer::singleShot(UIConstants::Timing::RESIZE_RECENTER_DELAY_MS, this, [this]() {
    if (!QApplication::closingDown() && getInteractionManager()) {
      getInteractionManager()->recenterCurrentSelection();
    }
  });
}

auto MainWindow::eventFilter(QObject *watched, QEvent *event) -> bool {
  return (getInteractionManager()) ? getInteractionManager()->eventFilter(watched, event)
                                   : QMainWindow::eventFilter(watched, event);
}

void MainWindow::refreshTitleCounts() {
  if (!getDatabaseManager()) {
    return;
  }

  // Don't update title bar with counts while a scan is in progress
  // (the scan progress handler sets the title instead)
  if (m_loadingOverlay && m_loadingOverlay->isActive()) {
    return;
  }

  int cur = currentCollectionIndex;
  if (cur < 0 || cur >= m_collections.size()) {
    setWindowTitle(qApp->applicationName());
    return;
  }

  auto cachedRecursiveCountForIndex = [this](int collectionIndex) -> qint64 {
    if (!getSessionManager()) {
      return -1;
    }
    if (collectionIndex < 0 || collectionIndex >= m_collections.size()) {
      return -1;
    }
    qint64 direct = -1;
    qint64 recursive = -1;
    if (!getSessionManager()->getCollectionCounts(m_collections[collectionIndex], m_collections,
                                                  direct, recursive)) {
      return -1;
    }
    return recursive;
  };

  // Check if we're in a subfolder
  const QString &subfolder = m_collections[cur].currentSubfolder;
  if (!subfolder.isEmpty() && getScrollManager()) {
    // In a subfolder: show "SubfolderName (subfolderCount/collectionCount
    // Items)"
    QString subfolderName = subfolder;
    int lastSlash = subfolder.lastIndexOf('/');
    if (lastSlash >= 0) {
      subfolderName = subfolder.mid(lastSlash + 1);
    }

    int subfolderItemCount = getScrollManager()->getTotalItems();

    const qint64 collectionCount = cachedRecursiveCountForIndex(cur);
    QString counts;
    if (collectionCount >= 0) {
      counts = QString("(%1/%2 Items)")
                   .arg(StringUtils::formatCountNumber(subfolderItemCount))
                   .arg(StringUtils::formatCountNumber(collectionCount));
    } else {
      counts = QString("(%1 Items)").arg(StringUtils::formatCountNumber(subfolderItemCount));
    }

    int directSubfolderCount = CollectionUtils::countVirtualFolders(m_collections[cur]);
    int directSubcollectionCount = CollectionUtils::directChildrenOf(cur, m_collections).size();

    QString title = QString("%1 %2").arg(subfolderName, counts);
    QStringList childParts;
    if (directSubfolderCount > 0) {
      childParts << QString("%1 subfolders").arg(directSubfolderCount);
    }
    if (directSubcollectionCount > 0) {
      childParts << QString("%1 subcollections").arg(directSubcollectionCount);
    }
    if (!childParts.isEmpty()) {
      title += QString(" — %1").arg(childParts.join(", "));
    }
    setWindowTitle(title);
    return;
  }

  // Not in subfolder: show collection hierarchy counts
  QVector<int> chain;
  int walk = cur;
  while (walk >= 0 && walk < m_collections.size()) {
    chain.append(walk);
    int parentIndex = m_collections[walk].parentCollectionIndex;
    if (parentIndex < 0) {
      break;
    }
    walk = parentIndex;
  }

  // When showAllSubcollectionItems is enabled, the displayed items include all
  // descendant items. Use the actual view count for the current collection
  // rather than the cached recursive count (which may not include flattened
  // items).
  const bool showAllItems = m_collections[cur].showAllSubcollectionItems;
  const int viewTotalItems = getScrollManager() ? getScrollManager()->getTotalItems() : -1;

  QStringList parts;
  bool anyKnown = false;
  for (int i = 0; i < chain.size(); ++i) {
    int idx = chain[i];
    qint64 countVal = -1;

    // For the current collection (first in chain) with
    // showAllSubcollectionItems, use the actual view count which includes
    // flattened descendant items
    if (i == 0 && showAllItems && viewTotalItems >= 0) {
      countVal = viewTotalItems;
    } else {
      countVal = cachedRecursiveCountForIndex(idx);
    }

    if (countVal >= 0) {
      anyKnown = true;
      parts << StringUtils::formatCountNumber(countVal);
    } else {
      parts << QStringLiteral("…");
    }
  }

  QString base = m_collections[cur].name;
  QString counts;
  if (anyKnown) {
    if (parts.size() == 1) {
      counts = QString("(%1 Items)").arg(parts.first());
    } else {
      counts = QString("(%1 Items)").arg(parts.join('/'));
    }
  }

  int directSubfolderCount = CollectionUtils::countVirtualFolders(m_collections[cur]);
  int directSubcollectionCount = CollectionUtils::directChildrenOf(cur, m_collections).size();

  QString title = counts.isEmpty() ? base : QString("%1 %2").arg(base, counts);
  QStringList childParts;
  if (directSubfolderCount > 0) {
    childParts << QString("%1 subfolders").arg(directSubfolderCount);
  }
  if (directSubcollectionCount > 0) {
    childParts << QString("%1 subcollections").arg(directSubcollectionCount);
  }
  if (!childParts.isEmpty()) {
    title += QString(" — %1").arg(childParts.join(", "));
  }
  setWindowTitle(title);
}

// Wires managers and signals; ensures sidebar metadata is refreshed when the
// sidebar becomes visible or its layout changes
void MainWindow::setupManagerConnections() {
  InteractionManagerSetup setup;
  setup.ctx = &m_appContext; // Managers and UI elements from shared context

  loadingLabel = ui->loadingLabel;

  // CRITICAL: Set interactionState BEFORE setupReferences so sub-managers
  // can access it via ctx during their own setupReferences calls
  m_appContext.managers.interactionState = &getInteractionManager()->state();

  // Set up InteractionManager (its sub-managers will now get valid
  // interactionState)
  getInteractionManager()->setupReferences(setup);

  // Register InteractionManager's owned sub-managers in ApplicationContext
  // This enables sub-managers to access siblings directly via ctx
  m_appContext.managers.animationManager = getInteractionManager()->animationManager();
  m_appContext.managers.selectionManager = getInteractionManager()->selectionManager();
  m_appContext.managers.viewportManager = getInteractionManager()->viewportManager();
  m_appContext.managers.mouseManager = getInteractionManager()->mouseManager();
  m_appContext.managers.keyboardManager = getInteractionManager()->keyboardManager();
  m_appContext.managers.eventManager = getInteractionManager()->eventManager();
  m_appContext.managers.searchManager = getInteractionManager()->searchManager();
  m_appContext.managers.launchManager = getInteractionManager()->launchManager();

  // Kartend-qxv: when runtime detection is enabled, the LaunchManager spawns
  // a tracked QProcess and emits started/finished signals. Show a "Now
  // Playing" overlay while the child runs and raise the window when it exits.
  if (auto *launch = getInteractionManager()->launchManager()) {
    connect(launch, &LaunchManager::runtimeStarted, this,
            [this](const QString & /*filePath*/, const QString &displayName) {
              if (m_nowPlayingOverlay) {
                m_nowPlayingOverlay->showOverlay(displayName);
              }
              // Kartend-2pzf: stop the sidebar's preview video so its audio
              // doesn't compete with the launched application's audio.
              if (m_MetadataSidebar) {
                m_MetadataSidebar->pausePreviewVideo();
              }
              // Kartend-gs1g: suspend attract mode for the duration of the
              // launch — the idle timer would otherwise fire under the
              // launched app and start scrolling unseen.
              if (auto *interaction = getInteractionManager()) {
                if (auto *attract = interaction->attractManager()) {
                  attract->setSuspended(true);
                }
              }
            });
    connect(launch, &LaunchManager::runtimeFinished, this, [this](const QString & /*filePath*/) {
      if (m_nowPlayingOverlay) {
        m_nowPlayingOverlay->hideOverlay();
      }
      // Kartend-gs1g: lift the attract suspension. setSuspended(false)
      // re-arms a fresh idle countdown so attract waits the full timeout
      // instead of kicking in the instant the launch ends.
      if (auto *interaction = getInteractionManager()) {
        if (auto *attract = interaction->attractManager()) {
          attract->setSuspended(false);
        }
      }
      // Bring Kartend back to the foreground when the tracked child
      // exits — the user expects "return on close" behavior.
      raise();
      activateWindow();
    });
  }

  // Now set up NavigationManager with fully populated context
  NavigationManagerSetup navSetup;
  navSetup.ctx = &m_appContext; // Managers and UI elements from shared context

  // Callbacks (not in context)
  navSetup.isShuttingDown = [this]() { return isShuttingDown(); };
  navSetup.refreshTitleCounts = [this]() { refreshTitleCounts(); };

  getNavigationManager()->setupReferences(navSetup);

  connectDatabaseManager();
  connectScrollManager();
  connectSidebarManager();
  connectSearchComponents();
  connectScrollBars();
  connectFilterToolbar();

  // Kartend-uve: detail page wiring. The overlay was created in
  // mainwindowsetup.cpp and parented to ui->centralwidget so it can cover
  // the entire window. Hand it to DetailPageManager along with the sidebar
  // and DB so the manager can build payloads. Keyboard signal lives on
  // InteractionManager's owned KeyboardManager.
  if (auto *detail = getDetailPageManager()) {
    DetailPageManagerSetup detailSetup;
    detailSetup.ctx = &m_appContext;
    detailSetup.overlay = m_detailPageOverlay;
    detail->setupReferences(detailSetup);

    if (auto *kb = getInteractionManager() ? getInteractionManager()->keyboardManager() : nullptr) {
      connect(kb, &KeyboardManager::requestItemDetails, detail,
              &DetailPageManager::showForCurrentSelection);
    }

    // Toolbar entry — clicking the ℹ button opens the detail page for the
    // current selection (same code path as the keyboard shortcut). No-op
    // when nothing is selected; DetailPageManager guards on context validity.
    if (ui->detailPageButton) {
      connect(ui->detailPageButton, &QPushButton::clicked, detail,
              &DetailPageManager::showForCurrentSelection);
    }

    // Lower the sidebar while the detail page is up — same rationale as
    // Kartend-63e bug #7 for the artwork preview overlay (without this,
    // raise() calls during the overlay's lifetime would re-stack the
    // sidebar on top).
    if (m_detailPageOverlay) {
      connect(m_detailPageOverlay, &DetailPageOverlay::visibilityChanged, this, [this](bool v) {
        if (auto *sb = getDetailsPaneManager()) {
          sb->setOverlayActive(v);
        }
      });
    }
  }

  if (auto *km = getKartManager()) {
    kart::KartManagerSetup kartSetup;
    kartSetup.settingsManager = getSettingsManager();
    kartSetup.getCollections = [this]() { return &m_collections; };
    kartSetup.getLauncherPresets = [this]() { return m_generalSettings.launcherPresets; };
    kartSetup.getParentWindow = [this]() -> QWidget * { return this; };
    km->setupReferences(kartSetup);

    connect(km, &kart::KartManager::collectionImported, this, [this](const QString &) {
      if (auto *sm = getSettingsManager()) {
        sm->saveCollections(m_collections);
      }
    });
  }
}

void MainWindow::updateWindowTitleWithFilter(int visible, int total) {
  if (currentCollectionIndex >= 0 && currentCollectionIndex < m_collections.size()) {
    QString base = m_collections[currentCollectionIndex].name;
    if (visible < total) {
      setWindowTitle(QString("%1 (%2/%3 items)").arg(base).arg(visible).arg(total));
    } else {
      setWindowTitle(QString("%1 (%2 items)").arg(base).arg(total));
    }
  }
  // Keep the top-bar position label in sync with the denominator.
  updateItemPositionLabel();
}

void MainWindow::updateItemPositionLabel() {
  // Kartend-tof: show "<pos> / <total>" next to the view-mode buttons.
  // Hidden until we have a concrete total. `currentSelectedIndex()` is a
  // visual index (includes subcollection tiles + virtual folders + media
  // files), which matches ScrollManager::getTotalItems() — so presenting
  // them as a fraction is coherent without further translation.
  if (!ui->itemPositionLabel) {
    return;
  }
  const int total = getScrollManager() ? getScrollManager()->getTotalItems() : 0;
  if (total <= 0) {
    ui->itemPositionLabel->clear();
    ui->itemPositionLabel->setVisible(false);
    return;
  }
  const int sel = getInteractionManager() ? getInteractionManager()->currentSelectedIndex() : -1;
  if (sel < 0) {
    ui->itemPositionLabel->setText(QString("%1").arg(total));
  } else {
    // User-facing positions are 1-based.
    ui->itemPositionLabel->setText(QString("%1 / %2").arg(sel + 1).arg(total));
  }
  ui->itemPositionLabel->setVisible(true);
}

void MainWindow::closeEvent(QCloseEvent *event) {
  if (m_isShuttingDown) {
    event->accept();
    return;
  }

  // Flush any pending grid-width persistence before shutdown so the final
  // user-adjusted width is not lost when closing immediately after changes.
  if (m_gridWidthSaveDebouncer && m_gridWidthSaveDebouncer->isPending() && getSettingsManager()) {
    m_gridWidthSaveDebouncer->triggerImmediate();
  }

  m_isShuttingDown = true;

  // Hide window immediately so user sees instant visual response
  hide();

  // Remove event filters first to prevent further processing
  if (ui->itemScrollArea) {
    ui->itemScrollArea->removeEventFilter(getInteractionManager());
    if (ui->itemScrollArea->viewport()) {
      ui->itemScrollArea->viewport()->removeEventFilter(getInteractionManager());
    }
  }
  if (gridContainer) {
    gridContainer->removeEventFilter(getInteractionManager());
  }

  // Persist current viewport/selection state before blocking signals.
  // This ensures the cached viewport is available for fast startup on next
  // launch.
  if (getNavigationManager()) {
    getNavigationManager()->prepareForShutdown();
  }

  // Block signals early to prevent cascading updates during shutdown
  if (getInteractionManager()) {
    getInteractionManager()->blockSignals(true);
    getInteractionManager()->clearSelection();
  }
  if (getScrollManager()) {
    getScrollManager()->blockSignals(true);
  }

  currentCollectionIndex = -1;

  // Delegate shutdown to ApplicationManager for coordinated cleanup
  if (m_appManager) {
    m_appManager->shutdown(m_collections);
  }

  event->accept();
}

void MainWindow::updateWindowTitleForCollection(int collectionIndex) {
  if (collectionIndex >= 0 && collectionIndex < m_collections.size()) {
    setWindowTitle(m_collections[collectionIndex].name);

    // Sync view type button states
    ViewType viewType = m_collections[collectionIndex].viewType;
    syncViewModeButton(viewType);
    // Kartend-iue: View → Layout submenu mirrors toolbar checked state on
    // collection switch.
    if (m_menuController) {
      m_menuController->syncLayoutActions(viewType);
      m_menuController->syncOrientationActions(m_collections[collectionIndex].sidebarPosition);
    }
  }
  // Sync the consolidated filter popup so the per-collection title-pattern
  // toggle and type radios reflect the active collection.
  refreshFilterToolbar();
}

void MainWindow::rebuildHierarchyCache() {
  m_hierarchyCache.rebuild(m_collections);
  // Collection list may have gained/lost type tags after settings edits —
  // keep the toolbar filter popup in sync so retagged collections show up
  // immediately and orphaned filters self-clear.
  refreshFilterToolbar();
}

void MainWindow::resyncPlaylistCollections() {
  // Strip any prior playlist-backed configs so a rename/delete in PlaylistManager
  // doesn't leave stale entries behind. INI-backed configs (isPlaylist=false)
  // are preserved verbatim because they're the canonical state — only the
  // synthesized rows are owned by this routine.
  m_collections.erase(std::remove_if(m_collections.begin(), m_collections.end(),
                                     [](const CollectionConfig &c) { return c.isPlaylist; }),
                      m_collections.end());

  PlaylistManager *playlistManager = getPlaylistManager();
  if (!playlistManager) {
    rebuildHierarchyCache();
    return;
  }

  // Kartend-5mg8: guarantee the built-in favorites playlist exists. Doing it
  // here (rather than once at startup) means the row is restored on the next
  // resync if the DB has been wiped between launches, without forcing a
  // separate "first run" code path.
  playlistManager->ensureFavoritesPlaylist();

  // Build a uuid → index map over the surviving (real) collections so each
  // playlist row's parent_collection_uuid resolves to the right
  // parentCollectionIndex. Any orphaned playlist (parent uuid no longer
  // matches a collection) falls back to root level.
  QHash<QString, int> uuidToIndex;
  for (int i = 0; i < m_collections.size(); ++i) {
    const CollectionConfig &c = m_collections[i];
    const QString expandedMediaDir = PathUtils::validateAndExpandPath(c.mediaDirectory, c.name);
    const QString uuid = CollectionUtils::computeCollectionUuid(c.name, expandedMediaDir);
    if (!uuid.isEmpty()) {
      uuidToIndex.insert(uuid, i);
    }
  }

  const QList<PlaylistRow> rows = playlistManager->loadAll();
  for (const PlaylistRow &row : rows) {
    CollectionConfig cfg;
    cfg.name = row.name;
    cfg.isPlaylist = true;
    cfg.playlistId = row.id;
    cfg.playlistReservedKind = row.reservedKind; // Kartend-5mg8
    cfg.collectionIcon = row.icon;
    // Empty mediaDirectory keeps the scan / virtual-folder / archive paths
    // off — the QueryManager playlist branch (Kartend-vlm7) reads items from
    // playlist_items via source uuid+path instead.
    if (!row.parentCollectionUuid.isEmpty()) {
      const int parentIdx = uuidToIndex.value(row.parentCollectionUuid, -1);
      if (parentIdx >= 0) {
        cfg.parentCollectionIndex = parentIdx;
        cfg.isSubcollection = true;
      }
    }
    m_collections.append(cfg);
  }

  rebuildHierarchyCache();
}

// Delegated Getters
DetailsPaneManager *MainWindow::getDetailsPaneManager() const {
  return m_appManager->getDetailsPaneManager();
}
SettingsManager *MainWindow::getSettingsManager() const {
  return m_appManager->getSettingsManager();
}
DatabaseManager *MainWindow::getDatabaseManager() const {
  return m_appManager->getDatabaseManager();
}
ScrollManager *MainWindow::getScrollManager() const {
  return m_appManager->getScrollManager();
}
NavigationManager *MainWindow::getNavigationManager() const {
  return m_appManager->getNavigationManager();
}
InteractionManager *MainWindow::getInteractionManager() const {
  return m_appManager->getInteractionManager();
}
kart::KartManager *MainWindow::getKartManager() const {
  return m_appManager->getKartManager();
}
SessionManager *MainWindow::getSessionManager() const {
  return m_appManager->getSessionManager();
}
ArtworkManager *MainWindow::getArtworkManager() const {
  return m_appManager->getArtworkManager();
}
CacheManager *MainWindow::getCacheManager() const {
  return m_appManager->getCacheManager();
}
PlaylistManager *MainWindow::getPlaylistManager() const {
  return m_appManager->getPlaylistManager();
}
DetailPageManager *MainWindow::getDetailPageManager() const {
  return m_appManager->getDetailPageManager();
}
