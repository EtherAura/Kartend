// Main application window that owns ApplicationManager and orchestrates UI
// setup.
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCoreApplication>
#include <QDesktopServices>
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
#include "collectionutils.h"
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
#include "sessionmanager.h"
#include "settingsutils.h"
#include "splashoverlay.h"
#include "startupvideooverlay.h"
#include "stringutils.h"
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

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcMainWindow, "kartend.mainwindow")

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), stackedWidget(nullptr), itemsPage(nullptr),
      gridContainer(nullptr), m_mainContentWidget(nullptr), itemGrid(nullptr),
      m_mainHorizontalLayout(nullptr), searchBar(nullptr), loadingLabel(nullptr),
      currentCollectionIndex(-1), m_MetadataSidebar(nullptr) {
  // unique_ptr is the sole owner; QObject parent stays null (Kartend-d70s,
  // re-attempted after Kartend-3v92 replaced NavigationManager's parent()
  // lifetime guards with the isAlive() helper). Destruction is driven
  // purely by member order.
  m_appManager = std::make_unique<ApplicationManager>(nullptr);
  m_appManager->initialize(&m_appContext);
  // Kartend-hzef step 3: scraper service ownership moved to ScraperController.
  m_marqueeController = std::make_unique<MarqueeController>(nullptr);
  m_scrollEventsController = std::make_unique<ScrollEventsController>(nullptr);
  m_dbEventsController = std::make_unique<DbEventsController>(nullptr);
  m_scraperController = std::make_unique<ScraperController>(nullptr);
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
  delete ui;
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
    default:
      break;
    }
  }

  return QMainWindow::event(event);
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
  if ((m_appManager->getInteractionManager()) &&
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
  // startup video plays first when enabled. The splash is
  // chained onto the video's dismissed signal so it still appears after
  // the user skips or the clip ends — keeping users with both features
  // configured from losing the splash.
  if (m_generalSettings.startupVideoEnabled && !m_generalSettings.startupVideoPath.isEmpty()) {
    auto *videoOverlay = new StartupVideoOverlay(this);
    videoOverlay->setGeometry(rect());
    videoOverlay->raise();
    if (videoOverlay->playVideo(m_generalSettings.startupVideoPath)) {
      videoOverlay->show();
      connect(videoOverlay, &StartupVideoOverlay::dismissed, this, [this]() {
        if (m_generalSettings.bootSplashEnabled && m_splashOverlay) {
          m_splashOverlay->showSplash(SplashOverlay::Reason::Startup,
                                      m_generalSettings.bootSplashTitle,
                                      m_generalSettings.bootSplashSubtitle);
        }
      });
      return;
    }
    // playVideo failed (missing/unreadable path): fall through to the
    // normal splash path so the user still gets a startup indicator.
    videoOverlay->deleteLater();
  }
  if (m_generalSettings.bootSplashEnabled && m_splashOverlay) {
    m_splashOverlay->showSplash(SplashOverlay::Reason::Startup, m_generalSettings.bootSplashTitle,
                                m_generalSettings.bootSplashSubtitle);
  }
}

void MainWindow::showFocusReturnSplash() {
  if (m_generalSettings.resumeFocusSplashEnabled && m_splashOverlay) {
    m_splashOverlay->showSplash(SplashOverlay::Reason::FocusReturn,
                                m_generalSettings.resumeFocusSplashTitle,
                                m_generalSettings.resumeFocusSplashSubtitle);
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
  // layer the runtime text zoom on top so menus/dialogs/
  // toolbar scale at the same rate as item titles. Computed against the
  // size that was already chosen above (user override or baseline) so the
  // override stays the authoritative "100 %" reference.
  font.setPointSize(std::max(1, font.pointSize() * settings.uiTextZoomPercent / 100));
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
  auto *km = m_appManager->getKartManager();
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

  if (m_appManager->getArtworkManager()) {
    if (auto *timerCoordinator = m_appManager->getArtworkManager()->getTimerCoordinator()) {
      timerCoordinator->scheduleLayoutUpdate();
    }
  }

  // Re-center on current selection after resize completes
  // Defer re-centering until after resize animation completes -
  // prevents visual jump during resize drag
  QTimer::singleShot(UIConstants::Timing::RESIZE_RECENTER_DELAY_MS, this, [this]() {
    if (!QApplication::closingDown() && m_appManager->getInteractionManager()) {
      m_appManager->getInteractionManager()->recenterCurrentSelection();
    }
  });
}

auto MainWindow::eventFilter(QObject *watched, QEvent *event) -> bool {
  return (m_appManager->getInteractionManager())
             ? m_appManager->getInteractionManager()->eventFilter(watched, event)
             : QMainWindow::eventFilter(watched, event);
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

  // Flush any pending grid-width persistence before shutdown so the final
  // user-adjusted width is not lost when closing immediately after changes.
  if (m_gridWidthDebouncer && m_gridWidthDebouncer->hasPendingSave() &&
      m_appManager->getSettingsManager()) {
    m_gridWidthDebouncer->flushPendingSave();
  }

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

// Manager accessors removed — callers go through m_appManager (internal) or
// mainWindow->applicationManager()->getXxxManager() (external).
// The three accessors below are kept because they implement IMainWindow's
// pure-virtuals — that interface is the data/ui-facing handle and won't be
// removed in this pass. Names dropped the "get" prefix per Kartend-5wuk.1
// so a grep for 'mainWindow->getXxxManager' outside this file is clean.
ISettingsManager *MainWindow::settingsManager() const {
  return m_appManager->getSettingsManager();
}
ScrollManager *MainWindow::scrollManager() const {
  return m_appManager->getScrollManager();
}
InteractionManager *MainWindow::interactionManager() const {
  return m_appManager->getInteractionManager();
}
