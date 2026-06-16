// Main application window that owns ApplicationManager and orchestrates UI
// setup.
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPalette>
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
#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "collection/typehelpers.h"
#include "collectionfilesystemwatcher.h"
#include "detailpagemanager.h"
#include "detailpageoverlay.h"
#include "dialogcontroller.h"
#include "eventmanager.h"
#include "gridwidthdebouncer.h"
#include "icachemanager.h"
#include "idatabasemanager.h"
#include "interactionmanager.h"
#include "kartmanager.h"
#include "kartreader.h"
#include "titlecountshelpers.h"

#include "datauditcontroller.h"
#include "dbeventscontroller.h"
#include "detailspane.h"
#include "detailspanemanager.h"
#include "isettingsmanager.h"
#include "itemwidget.h"
#include "keyboardmanager.h"
#include "launchmanager.h"
#include "loadingoverlay.h"
#include "mainwindow.h"
#include "marqueecontroller.h"
#include "menucontroller.h"
#include "navigationmanager.h"
#include "nowplayingoverlay.h"
#include "overlaylayermanager.h"
#include "pathutils.h"
#include "playlistmanager.h"
#include "propertyutils.h"
#include "scrapercontroller.h"
#include "scraperservice.h"
#include "scrolleventscontroller.h"
#include "scrollmanager.h"
#include "searchmanager.h"
#include "sessionmanager.h"
#include "settingsutils.h"
#include "splashoverlay.h"
#include "startupvideooverlay.h"
#include "stringutils.h"
#include "systemthemewatcher.h"
#include "textzoom.h"
#include "textzoomhud.h"
#include "timerutils.h"
#include "ui_mainwindow.h"
#include "uiconstants/timing.h"
#include "videopreviewwidget.h"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QMimeData>
#include <QUrl>

#include <algorithm>
#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcMainWindow, "kartend.mainwindow")

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(std::make_unique<Ui::MainWindow>()), stackedWidget(nullptr),
      itemsPage(nullptr), gridContainer(nullptr), m_mainContentWidget(nullptr), itemGrid(nullptr),
      m_mainHorizontalLayout(nullptr), searchBar(nullptr), loadingLabel(nullptr),
      currentCollectionIndex(-1), m_MetadataSidebar(nullptr) {
  // unique_ptr is the sole owner; QObject parent stays null (Kartend-d70s,
  // re-attempted after Kartend-3v92 replaced NavigationManager's parent()
  // lifetime guards with the appNotShuttingDown() helper). Destruction is driven
  // purely by member order.
  m_appManager = std::make_unique<ApplicationManager>(nullptr);
  m_appManager->initialize(&m_appContext);
  // Kartend-hzef step 3: scraper service ownership moved to ScraperController.
  m_marqueeController = std::make_unique<MarqueeController>(nullptr);
  m_scrollEventsController = std::make_unique<ScrollEventsController>(nullptr);
  m_dbEventsController = std::make_unique<DbEventsController>(nullptr);
  m_scraperController = std::make_unique<ScraperController>(nullptr);
  m_datAuditController = std::make_unique<DatAuditController>(nullptr);
  m_dialogController = std::make_unique<DialogController>(this);
  // Constructed before setupUI() so each overlay's setLayerManager() call
  // inside setupUI() / setupArtworkManager() / setupSidebar() can register
  // against a live instance. The manager owns no widgets — overlays remain
  // parented to centralwidget as before.
  m_overlayLayerManager = std::make_unique<OverlayZOrderRegistry>(nullptr);

  ui->setupUi(this);
  setupUI();
}

MainWindow::~MainWindow() {
  // Kartend-nbfgs: no explicit `delete ui` — `ui` is a unique_ptr member that
  // destructs AFTER the manager/controller members (it is declared above them,
  // so it tears down later in reverse-declaration order), keeping the Ui struct
  // and ctx.ui valid while those teardown paths run. Defined out-of-line here so
  // the unique_ptr<Ui_MainWindow> destructor sees the complete type.
}

bool MainWindow::event(QEvent *event) {
  if (event && !m_isShuttingDown && !QApplication::closingDown()) {
    switch (event->type()) {
    case QEvent::WindowDeactivate:
      // Skip tracking deactivation that was caused by our own scraper
      // or settings dialog taking focus — re-entering the main window
      // afterwards is not a "welcome back" moment.
      if (!QApplication::activeModalWidget() && !QApplication::activePopupWidget() &&
          !DialogController::anyScrapeResultDialogVisible() &&
          !DialogController::anySettingsDialogVisible()) {
        m_windowWasInactive = true;
      }
      break;
    case QEvent::WindowActivate:
      if (m_windowWasInactive) {
        m_windowWasInactive = false;
        if (m_startupSplashHandled && !QApplication::activeModalWidget() &&
            !QApplication::activePopupWidget() &&
            !DialogController::anyScrapeResultDialogVisible() &&
            !DialogController::anySettingsDialogVisible()) {
          showFocusReturnSplash();
        }
      }
      break;
    case QEvent::ApplicationPaletteChange:
    case QEvent::ThemeChange:
      // The system color scheme changed at runtime (e.g. a KDE
      // wallpaper-derived accent shifting on a Plasma activity switch). Qt has
      // already delivered the new QApplication palette, but every color we
      // cached / baked / pinned needs recomputing. KDE fires several of these
      // back-to-back during one scheme rewrite, so coalesce to a single
      // re-theme on the next event-loop turn — the singleShot(0) also lets the
      // new palette finish propagating to children before we read it (same
      // reason ItemWidget defers its artwork refresh).
      if (!m_paletteRetintPending) {
        m_paletteRetintPending = true;
        // singleShot(0): coalesce the burst of palette/theme events KDE fires
        // during one scheme rewrite into a single re-theme on the next
        // event-loop turn, by which point qApp->palette() has fully resolved
        // the new colours (reading it synchronously here can race that).
        QTimer::singleShot(0, this, [this]() {
          m_paletteRetintPending = false;
          reapplyDerivedThemingFromSystemPalette();
        });
      }
      break;
    default:
      break;
    }
  }

  return QMainWindow::event(event);
}

void MainWindow::reapplyDerivedThemingFromSystemPalette() {
  if (m_isShuttingDown || QApplication::closingDown() || !m_appManager) {
    return;
  }
  // Re-run the per-collection appearance pipeline for the active collection.
  // This re-resolves the toolbar/menubar/search-bar primary-color stylesheets,
  // the ItemWidget colour statics, the details-pane content palette + bubble
  // stylesheets, and rebuilds the breadcrumb + metadata rows whose accent is
  // baked into HTML. Skipped on the root/home view (no active collection);
  // its surfaces are palette-driven and update on repaint.
  if (auto *nav = m_appManager->getNavigationManager()) {
    if (currentCollectionIndex >= 0 && currentCollectionIndex < m_collections.size()) {
      nav->reapplyActiveCollectionTheming(currentCollectionIndex);
    }
  }
  // The search-bar placeholder tint is pinned via an explicit setPalette on
  // the QLineEdit (resolve-mask), so Qt's automatic propagation can't refresh
  // it — re-run the recompute against the fresh application palette.
  if (auto *im = m_appManager->getInteractionManager()) {
    if (auto *sm = im->searchManager()) {
      sm->updateSearchBarPlaceholder();
    }
  }
}

void MainWindow::onSystemThemeChanged() const {
  if (m_isShuttingDown || QApplication::closingDown()) {
    return;
  }
  // KDE has already updated QApplication::palette() with the new accent, but on
  // an accent-only change Qt does not dispatch the palette-change to our
  // widgets — so neither the style-drawn chrome (toolbar/menubar/scrollbars)
  // nor ItemWidget's own PaletteChange handler ever fire, and colours stay
  // stale until a manual reload. Synthesize the broadcast Qt skipped: re-assign
  // the application palette so every widget re-resolves the fresh colours and
  // repaints. Re-assigning the *same* palette is a no-op in Qt, so cycle
  // through a throwaway palette first to force propagation; both setPalette
  // calls run synchronously, so no frame is ever painted with the intermediate.
  const QPalette fresh = QApplication::palette();
  QApplication::setPalette(QPalette());
  QApplication::setPalette(fresh);
  // KDE itself pushes accent updates via QApplication::setPalette, so the
  // assignment above does not strand us in an app-owned palette — KDE's next
  // change re-applies the same way.
  //
  // With a global application stylesheet installed (the QToolTip rule in
  // main.cpp), the QStyleSheetStyle proxy caches resolved palette() refs and
  // won't re-resolve them on a palette change alone — re-applying the sheet
  // forces a full re-polish so style-drawn chrome (toolbar / menubar /
  // scrollbars) picks up the new colours.
  qApp->setStyleSheet(qApp->styleSheet());
  // setPalette(fresh) lands an ApplicationPaletteChange on us, which event()
  // coalesces into a single deferred reapplyDerivedThemingFromSystemPalette()
  // for the cached/HTML/stylesheet chrome — so we deliberately do NOT call it a
  // second time here (doing so re-ran the whole appearance pipeline twice,
  // including the off-thread sidebar/background image reloads — the main source
  // of the perceived lag).
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
  // A late key event between m_isShuttingDown and InteractionManager teardown
  // would otherwise reach a just-cleared manager (closeEvent clears its
  // selection + blocks its signals); fall through to the base handler then,
  // matching the shutdown short-circuit on the other event paths (Kartend-0j6o).
  if (!m_isShuttingDown && !QApplication::closingDown() && m_appManager->getInteractionManager() &&
      m_appManager->getInteractionManager()->handleGlobalKeyPress(event)) {
    return;
  }
  QMainWindow::keyPressEvent(event);
}

void MainWindow::showStartupSplash() {
  m_startupSplashHandled = true;
  // Pending-scrape resume prompt fires once on first startup paint —
  // queued so the modal lands on top of an already-shown main window
  // and not racing the startup splash. The ScraperService's pending
  // file is checked here; if one exists, the user is prompted to
  // resume or discard.
  QTimer::singleShot(0, this, [this]() { promptResumePendingScrapeIfAny(); });
  // DAT-library startup scan (Kartend-m6qsb.5): header-probe the watched
  // folder for new catalogues off-thread; surfaces only a status-bar hint
  // when something matched. Deferred a few seconds so first paint, splash,
  // and the initial collection scan keep the disk to themselves.
  QTimer::singleShot(5000, this, [this]() {
    if (m_datAuditController) {
      m_datAuditController->startupLibraryScan();
    }
  });
  // startup video plays first when enabled. The splash is
  // chained onto the video's dismissed signal so it still appears after
  // the user skips or the clip ends — keeping users with both features
  // configured from losing the splash.
  if (m_generalSettings.startup.startupVideoEnabled &&
      !m_generalSettings.startup.startupVideoPath.isEmpty()) {
    auto *videoOverlay = new StartupVideoOverlay(this);
    videoOverlay->setGeometry(rect());
    // Kartend-1ha73: stack via the z-order registry (the StartupVideo layer
    // slot already existed but the overlay was never registered) — a raw
    // raise() here shared MainWindow's stacking context with the registered
    // splash / text-zoom HUD, so any registry restack after this point
    // could bury the playing video. registerOverlay() records the layer;
    // restack() applies it now. Fallback raise() covers the (test-only)
    // case where the registry isn't constructed yet.
    if (m_overlayLayerManager) {
      m_overlayLayerManager->registerOverlay(videoOverlay,
                                             OverlayZOrderRegistry::Layer::StartupVideo);
      m_overlayLayerManager->restack();
    } else {
      videoOverlay->raise();
    }
    if (videoOverlay->playVideo(m_generalSettings.startup.startupVideoPath)) {
      videoOverlay->show();
      connect(videoOverlay, &StartupVideoOverlay::dismissed, this, [this]() {
        // The video can be dismissed while the window is tearing down; don't
        // touch splash/settings state during shutdown (Kartend-jtic).
        if (m_isShuttingDown || QApplication::closingDown()) {
          return;
        }
        if (m_generalSettings.splash.bootSplashEnabled && m_splashOverlay) {
          m_splashOverlay->showSplash(SplashOverlay::Reason::Startup,
                                      m_generalSettings.splash.bootSplashTitle,
                                      m_generalSettings.splash.bootSplashSubtitle);
        }
      });
      return;
    }
    // playVideo failed (missing/unreadable path): fall through to the
    // normal splash path so the user still gets a startup indicator.
    videoOverlay->deleteLater();
  }
  if (m_generalSettings.splash.bootSplashEnabled && m_splashOverlay) {
    m_splashOverlay->showSplash(SplashOverlay::Reason::Startup,
                                m_generalSettings.splash.bootSplashTitle,
                                m_generalSettings.splash.bootSplashSubtitle);
  }
}

void MainWindow::showFocusReturnSplash() {
  if (m_generalSettings.splash.resumeFocusSplashEnabled && m_splashOverlay) {
    m_splashOverlay->showSplash(SplashOverlay::Reason::FocusReturn,
                                m_generalSettings.splash.resumeFocusSplashTitle,
                                m_generalSettings.splash.resumeFocusSplashSubtitle);
  }
}

void MainWindow::applyGlobalUiFont(const GeneralSettings &settings) {
  // Compose the new application font from the persisted settings, falling back
  // to whatever Qt picked at startup, captured per-instance on first use so we
  // can still restore it after the user clears the override (Kartend-r2722: a
  // process-wide static here captured whatever the previous window/test left).
  if (!m_uiFontBaselineCaptured) {
    m_uiFontBaseline = QApplication::font();
    m_uiFontBaselineCaptured = true;
  }
  QFont font = m_uiFontBaseline;
  const QString family = settings.appearance.globalUiFontFamily.trimmed();
  if (!family.isEmpty()) {
    font.setFamily(family);
  }
  if (settings.appearance.globalUiFontPointSize > 0) {
    font.setPointSize(settings.appearance.globalUiFontPointSize);
  }
  // layer the runtime text zoom on top so menus/dialogs/
  // toolbar scale at the same rate as item titles. Computed against the
  // size that was already chosen above (user override or baseline) so the
  // override stays the authoritative "100 %" reference.
  font.setPointSize(std::max(1, font.pointSize() * settings.appearance.uiTextZoomPercent / 100));
  // setFont propagates to every widget that hasn't had setFont() called on
  // it explicitly, so menus, dialogs, and toolbar text all pick this up
  // without us walking the widget tree.
  QApplication::setFont(font);
}

// Text-zoom state lives in src/utils/textzoom.{h,cpp} so widget modules can
// use it without including this header. MainWindow's static methods stay
// as thin forwarders to keep the existing call surface stable for callers
// that already have a MainWindow header in scope.

int MainWindow::textZoomPercent() {
  return TextZoom::percent();
}

void MainWindow::primeTextZoomFromSettings(int percent) {
  TextZoom::primeFromSettings(percent);
}

int MainWindow::zoomedFontSize(int baseSize) {
  return TextZoom::zoomedFontSize(baseSize);
}

// Toolbar / shortcut setup methods (setupTextZoomShortcuts, setupVideoPauseShortcut,
// setupViewModeButton, syncViewModeButton, setupSearchModeAction,
// setupPreviewVolumeSlider, applyTextZoom, applyToolbarCustomization) live in
// mainwindow_toolbar.cpp.

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
  // Collect the dropped .kart paths and accept the drop immediately. The
  // destination prompt + import are deferred (processPendingKartImports) so the
  // modal file dialog and the blocking import don't run inside the DnD handler
  // — running them here keeps the drag source blocked until we return and lets
  // a second drop re-enter dropEvent through the modal's nested loop
  // (Kartend-tubnr).
  for (const QUrl &url : event->mimeData()->urls()) {
    if (!url.isLocalFile()) continue;
    const QString path = url.toLocalFile();
    if (path.endsWith(".kart", Qt::CaseInsensitive)) {
      m_pendingKartImports.append(path);
    }
  }
  event->acceptProposedAction();
  // Kick off a drain on the next loop turn unless one is already running — a
  // running drain picks up newly-appended paths before it exits.
  if (!m_pendingKartImports.isEmpty() && !m_kartImportInProgress) {
    // Defer to the next event-loop turn so this drop handler returns first —
    // releasing the OS drag-and-drop session and the source app's cursor —
    // before the import drain (which can open modal progress UI) takes over.
    QTimer::singleShot(0, this, &MainWindow::processPendingKartImports);
  }
}

void MainWindow::processPendingKartImports() {
  if (m_isShuttingDown || QApplication::closingDown()) {
    m_pendingKartImports.clear();
    return;
  }
  if (m_kartImportInProgress) {
    return; // a drain is already running (re-entrancy guard)
  }
  auto *km = m_appManager ? m_appManager->getKartManager() : nullptr;
  if (!km) {
    m_pendingKartImports.clear();
    return;
  }
  m_kartImportInProgress = true;
  // Drain the queue. Each getExistingDirectory spins a nested event loop, so a
  // drop arriving mid-prompt appends to m_pendingKartImports and is handled by
  // this same loop before it exits — no second drain starts (guard above).
  while (!m_pendingKartImports.isEmpty()) {
    const QString path = m_pendingKartImports.takeFirst();
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
  m_kartImportInProgress = false;
}

void MainWindow::resizeEvent(QResizeEvent *event) {
  if (!updatesEnabled()) {
    return;
  }

  QMainWindow::resizeEvent(event);

  if (m_appManager->getArtworkManager()) {
    if (auto *timerCoordinator = m_appManager->getArtworkManager()->getTimerCoordinator()) {
      timerCoordinator->scheduleLayoutUpdate();
    }
  }

  // Re-center on current selection after the resize settles. A drag fires many
  // resizeEvents; use one restartable timer so only the final resize recenters,
  // instead of queuing a fresh singleShot per tick (Kartend-20utj).
  if (!m_resizeRecenterTimer) {
    m_resizeRecenterTimer = new QTimer(this);
    m_resizeRecenterTimer->setSingleShot(true);
    connect(m_resizeRecenterTimer, &QTimer::timeout, this, [this]() {
      if (!QApplication::closingDown() && m_appManager->getInteractionManager()) {
        m_appManager->getInteractionManager()->recenterCurrentSelection();
      }
    });
  }
  m_resizeRecenterTimer->start(UIConstants::Timing::RESIZE_RECENTER_DELAY_MS);
}

auto MainWindow::eventFilter(QObject *watched, QEvent *event) -> bool {
  // Don't route filtered events into a tearing-down InteractionManager
  // (Kartend-0j6o) — hand them to the base filter during shutdown.
  if (m_isShuttingDown || QApplication::closingDown() || !m_appManager->getInteractionManager()) {
    return QMainWindow::eventFilter(watched, event);
  }
  return m_appManager->getInteractionManager()->eventFilter(watched, event);
}

void MainWindow::refreshTitleCounts() {
  TitleCountsHelpers::refreshTitleCounts(this, m_appContext, m_collections, currentCollectionIndex,
                                         m_loadingOverlay);
}

// Wires managers and signals; ensures sidebar metadata is refreshed when the
// sidebar becomes visible or its layout changes
void MainWindow::setupManagerConnections() {
  // Kartend-8y5z: this method used to be 250 LOC of inlined setup; it's
  // now a sequence of per-area wireXxx() calls. Order matters —
  // InteractionManager seeds the ApplicationContext that NavigationManager,
  // DetailPageManager, and KartManager all read through.
  wireInteractionManager();
  wireNavigationManager();

  connectDatabaseManager();
  connectScrollManager();
  connectSidebarManager();
  connectSearchComponents();
  connectScrollBars();
  connectFilterToolbar();

  wireDetailPageManager();
  wireKartManager();
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
  // show "<pos> / <total>" next to the view-mode buttons.
  // Hidden until we have a concrete total. `currentSelectedIndex()` is a
  // visual index (includes subcollection tiles + virtual folders + media
  // files), which matches ScrollManager::getTotalItems() — so presenting
  // them as a fraction is coherent without further translation.
  if (!ui->itemPositionLabel) {
    return;
  }
  const int total =
      m_appManager->getScrollManager() ? m_appManager->getScrollManager()->getTotalItems() : 0;
  if (total <= 0) {
    ui->itemPositionLabel->clear();
    ui->itemPositionLabel->setVisible(false);
    return;
  }
  const int sel = m_appManager->getInteractionManager()
                      ? m_appManager->getInteractionManager()->currentSelectedIndex()
                      : -1;
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

  // No explicit grid-width flush here: a width change is applied to m_collections
  // immediately and shutdown() below persists m_collections synchronously, so the
  // final width is saved without the debounced timer's separate INI write
  // (Kartend-rbkf6).
  m_isShuttingDown = true;

  // Hide window immediately so user sees instant visual response
  hide();

  // Remove event filters first to prevent further processing
  if (ui->itemScrollArea) {
    ui->itemScrollArea->removeEventFilter(m_appManager->getInteractionManager());
    if (ui->itemScrollArea->viewport()) {
      ui->itemScrollArea->viewport()->removeEventFilter(m_appManager->getInteractionManager());
    }
  }
  if (gridContainer) {
    gridContainer->removeEventFilter(m_appManager->getInteractionManager());
  }

  // Persist current viewport/selection state before blocking signals.
  // This ensures the cached viewport is available for fast startup on next
  // launch.
  if (m_appManager->getNavigationManager()) {
    m_appManager->getNavigationManager()->prepareForShutdown();
  }

  // Block signals early to prevent cascading updates during shutdown
  if (m_appManager->getInteractionManager()) {
    m_appManager->getInteractionManager()->blockSignals(true);
    m_appManager->getInteractionManager()->clearSelection();
  }
  if (m_appManager->getScrollManager()) {
    m_appManager->getScrollManager()->blockSignals(true);
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
    // View → Layout submenu mirrors toolbar checked state on
    // collection switch.
    if (m_menuController) {
      m_menuController->syncLayoutActions(viewType);
      m_menuController->syncOrientationActions(
          m_collections[collectionIndex].sidebar.sidebarPosition);
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
  // Kartend-3vkjc: while the first-run wizard modal is open, defer instead of
  // mutating m_collections under it (both the startup singleShot and the
  // playlistsChanged signal land here). runDeferredStartupTasks() runs this
  // once after the wizard returns.
  if (m_startupTasksGated) {
    m_pendingResync = true;
    return;
  }
  // Strip any prior playlist-backed configs so a rename/delete in PlaylistManager
  // doesn't leave stale entries behind. INI-backed configs (isPlaylist=false)
  // are preserved verbatim because they're the canonical state — only the
  // synthesized rows are owned by this routine.
  m_collections.erase(std::remove_if(m_collections.begin(), m_collections.end(),
                                     [](const CollectionConfig &c) { return c.isPlaylist; }),
                      m_collections.end());

  PlaylistManager *playlistManager = m_appManager->getPlaylistManager();
  if (!playlistManager) {
    rebuildHierarchyCache();
    return;
  }

  // guarantee the built-in favorites playlist exists. Doing it
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
    cfg.isSmartPlaylist = row.isSmart;
    cfg.playlistReservedKind = row.reservedKind;
    cfg.collectionIcon = row.icon;
    // Empty mediaDirectory keeps the scan / virtual-folder / archive paths
    // off — the QueryManager playlist branch reads items from
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

// Per-manager accessors removed in Kartend-qjtz — the settings dialog now
// reaches its siblings through an injected ApplicationContext, so the
// IMainWindow forwarders (settingsManager / scrollManager /
// interactionManager) are gone. Internal callers use m_appManager directly;
// external callers route through applicationManager()->getXxxManager().
