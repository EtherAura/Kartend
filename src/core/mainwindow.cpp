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
#include "createsmartplaylistdialog.h"
#include "customfieldsdialog.h"
#include "detailpagemanager.h"
#include "detailpageoverlay.h"
#include "eventmanager.h"
#include "gridwidthdebouncer.h"
#include "icachemanager.h"
#include "idatabasemanager.h"
#include "interactionmanager.h"
#include "itemartworklinksdialog.h"
#include "kartmanager.h"
#include "kartmergedialog.h"
#include "kartprogressdialog.h"
#include "kartreader.h"

#include "detailspane.h"
#include "detailspanemanager.h"
#include "isettingsmanager.h"
#include "itemwidget.h"
#include "keyboardmanager.h"
#include "launcherchooserdialog.h"
#include "launchmanager.h"
#include "loadingoverlay.h"
#include "mainwindow.h"
#include "marqueecontroller.h"
#include "menucontroller.h"
#include "navigationmanager.h"
#include "nowplayingoverlay.h"
#include "pathutils.h"
#include "playlistmanager.h"
#include "propertyutils.h"
#include "scraperesultdialog.h"
#include "scraperservice.h"
#include "scrollmanager.h"
#include "sessionmanager.h"
#include "settingsdialog.h"
#include "settingsutils.h"
#include "shortcutsdialog.h"
#include "splashoverlay.h"
#include "startupvideooverlay.h"
#include "stringutils.h"
#include "textzoom.h"
#include "textzoomhud.h"
#include "timerutils.h"
#include "ui_mainwindow.h"
#include "uiconstants.h"
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
  m_appManager = std::make_unique<ApplicationManager>(this);
  m_appManager->initialize(&m_appContext);
  m_scraperService = std::make_unique<Scraper::ScraperService>(this);
  m_marqueeController = std::make_unique<MarqueeController>(this);

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
          !ScrapeResultDialog::isAnyInstanceVisible() && !SettingsDialog::isAnyInstanceVisible()) {
        m_windowWasInactive = true;
      }
      break;
    case QEvent::WindowActivate:
      if (m_windowWasInactive) {
        m_windowWasInactive = false;
        if (m_startupSplashHandled && !QApplication::activeModalWidget() &&
            !QApplication::activePopupWidget() && !ScrapeResultDialog::isAnyInstanceVisible() &&
            !SettingsDialog::isAnyInstanceVisible()) {
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
// mainwindowtoolbar.cpp.

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

  // Appends " — N subfolders, M subcollections" to @p title when @p cur has
  // any direct children. Used by both the subfolder and the collection
  // branches below.
  auto appendChildPartsSuffix = [this, cur](QString &title) {
    const int directSubfolderCount = CollectionUtils::countVirtualFolders(m_collections[cur]);
    const int directSubcollectionCount =
        CollectionUtils::directChildrenOf(cur, m_collections).size();
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

    const int subfolderItemCount = getScrollManager()->getTotalItems();
    const qint64 collectionCount = cachedRecursiveCountForIndex(cur);
    QString counts;
    if (collectionCount >= 0) {
      counts = QString("(%1/%2 Items)")
                   .arg(StringUtils::formatCountNumber(subfolderItemCount))
                   .arg(StringUtils::formatCountNumber(collectionCount));
    } else {
      counts = QString("(%1 Items)").arg(StringUtils::formatCountNumber(subfolderItemCount));
    }

    QString title = QString("%1 %2").arg(subfolderName, counts);
    appendChildPartsSuffix(title);
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

  const QString base = m_collections[cur].name;
  QString counts;
  if (anyKnown) {
    counts = QString("(%1 Items)").arg(parts.size() == 1 ? parts.first() : parts.join('/'));
  }

  QString title = counts.isEmpty() ? base : QString("%1 %2").arg(base, counts);
  appendChildPartsSuffix(title);
  setWindowTitle(title);
}

// Wires managers and signals; ensures sidebar metadata is refreshed when the
// sidebar becomes visible or its layout changes
void MainWindow::setupManagerConnections() {
  InteractionManagerSetup setup;
  setup.ctx = &m_appContext; // Managers and UI elements from shared context

  // Kartend-n8kh: the input layer's right-click menu used to construct
  // CreateSmartPlaylistDialog / CustomFieldsDialog directly, taking an
  // upward input -> ui edge. The dialogs now run via these closures
  // supplied here in the UI layer; InteractionManager just invokes
  // them and reads the result.
  setup.runSmartPlaylistDialog =
      [this](const QString &initialName,
             const std::optional<SmartFilter::Filter> &initialFilter)
          -> std::optional<SmartPlaylistEdit> {
    CreateSmartPlaylistDialog dialog(this);
    if (!initialName.isEmpty()) {
      dialog.setInitialName(initialName);
    }
    if (initialFilter.has_value()) {
      dialog.setInitialFilter(*initialFilter);
    }
    if (dialog.exec() != QDialog::Accepted) {
      return std::nullopt;
    }
    SmartPlaylistEdit out;
    out.name = dialog.name();
    out.filter = dialog.filter();
    return out;
  };
  setup.runCustomFieldsDialog =
      [this](const QString &itemTitle,
             const ItemMetadataStore::CustomFieldList &initial)
          -> std::optional<ItemMetadataStore::CustomFieldList> {
    CustomFieldsDialog dialog(this);
    dialog.setItemTitle(itemTitle);
    dialog.setFields(initial);
    if (dialog.exec() != QDialog::Accepted) {
      return std::nullopt;
    }
    return dialog.fields();
  };

  loadingLabel = ui->loadingLabel;

  // ctx is already fully populated by initializeAppContext() — InteractionManager
  // and its owned sub-managers were registered eagerly so every setupReferences()
  // call below can resolve siblings exclusively through ctx.
  getInteractionManager()->setupReferences(setup);

  // when runtime detection is enabled, the LaunchManager spawns
  // a tracked QProcess and emits started/finished signals. Show a "Now
  // Playing" overlay while the child runs and raise the window when it exits.
  if (auto *launch = getInteractionManager()->launchManager()) {
    connect(launch, &LaunchManager::runtimeStarted, this,
            [this](const QString & /*filePath*/, const QString &displayName) {
              if (m_nowPlayingOverlay) {
                m_nowPlayingOverlay->showOverlay(displayName);
              }
              // stop the sidebar's preview video so its audio
              // doesn't compete with the launched application's audio.
              if (m_MetadataSidebar) {
                m_MetadataSidebar->pausePreviewVideo();
              }
              // suspend attract mode for the duration of the
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
      // lift the attract suspension. setSuspended(false)
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

  // EventManager gates item-grid input while a modal scrape dialog is up,
  // and LaunchManager prompts a launcher chooser for multi-launcher
  // collections. Both dialog types live in the UI layer, so MainWindow
  // injects the callbacks rather than the input module including the
  // dialog headers.
  if (auto *interaction = getInteractionManager()) {
    if (auto *events = interaction->eventManager()) {
      events->setModalScrapeDialogVisiblePredicate(
          []() { return ScrapeResultDialog::isAnyInstanceVisible(); });
    }
    if (auto *launch = interaction->launchManager()) {
      launch->setChooseLauncherCallback([this](const QString &collectionName,
                                               const QStringList &launcherNames, int defaultIndex) {
        return LauncherChooserDialog::choose(window(), collectionName, launcherNames, defaultIndex);
      });
    }
  }

  // detail page wiring. The overlay was created in
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
    // bug #7 for the artwork preview overlay (without this,
    // raise() calls during the overlay's lifetime would re-stack the
    // sidebar on top).
    if (m_detailPageOverlay) {
      connect(m_detailPageOverlay, &DetailPageOverlay::visibilityChanged, this, [this](bool v) {
        if (auto *sb = getDetailsPaneManager()) {
          sb->setOverlayActive(v);
        }
      });

      // Kartend-n8kh: this connect previously lived in DetailPageManager
      // (data/media layer), which required #including the concrete
      // DetailPageOverlay header for the signal symbol. Moved here so the
      // manager sees only IDetailPageOverlay. QDesktopServices is the
      // same hand-off the sidebar uses for manuals so both surfaces
      // respect the user's xdg-open / Finder default handler.
      connect(m_detailPageOverlay, &DetailPageOverlay::manualRequested, this,
              [](const QString &manualPath) {
                if (!manualPath.isEmpty()) {
                  QDesktopServices::openUrl(QUrl::fromLocalFile(manualPath));
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
    // Kartend-a3ir: the interactive merge dialog lives in the UI layer
    // and is constructed here. KartManager (data layer) just invokes the
    // closure with the conflicting metadata and uses the returned
    // ConflictResolution. The dialog parents to `this` so it modals
    // correctly over the main window.
    kartSetup.mergeResolver = [this](const QString &itemPath,
                                     const ItemMetadataStore::ItemMetadata &existing,
                                     const ItemMetadataStore::ItemMetadata &incoming) {
      KartMergeDialog dlg(itemPath, existing, incoming, this);
      kart::ConflictResolution r;
      if (dlg.exec() == QDialog::Accepted) {
        r.choice = dlg.choice();
        r.policy = dlg.policy();
        r.applyToAll = dlg.applyToAll();
      } else {
        r.choice = kart::MergeChoice::Skip;
      }
      return r;
    };
    km->setupReferences(kartSetup);

    connect(km, &kart::KartManager::collectionImported, this, [this](const QString &) {
      if (auto *sm = getSettingsManager()) {
        sm->saveCollections(m_collections);
      }
    });

    // KartManager is a data-layer manager and no longer #includes the
    // KartProgressDialog (a ui/ type). It emits a kartProgressStarted signal
    // when a long import/export begins; the owner builds the dialog here and
    // wires the remaining progress/lifecycle signals to it. The dialog is the
    // connection context for those onward hops, so they tear down when it
    // closes (WA_DeleteOnClose). cancelRequested routes back to KartManager.
    connect(km, &kart::KartManager::kartProgressStarted, this, [this, km](const QString &title) {
      auto *dlg = new KartProgressDialog(title, this);
      dlg->setAttribute(Qt::WA_DeleteOnClose);
      connect(km, &kart::KartManager::kartProgressFraction, dlg, &KartProgressDialog::setFraction);
      connect(km, &kart::KartManager::kartProgressEntry, dlg, &KartProgressDialog::setEntryName);
      connect(km, &kart::KartManager::kartProgressFinished, dlg, &KartProgressDialog::markFinished);
      connect(km, &kart::KartManager::kartProgressFailed, dlg, &QDialog::reject);
      connect(dlg, &KartProgressDialog::cancelRequested, km,
              &kart::KartManager::cancelActiveKartOperation);
      dlg->show();
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
  // show "<pos> / <total>" next to the view-mode buttons.
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
  if (m_gridWidthDebouncer && m_gridWidthDebouncer->hasPendingSave() && getSettingsManager()) {
    m_gridWidthDebouncer->flushPendingSave();
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
    // View → Layout submenu mirrors toolbar checked state on
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

// Delegated Getters
DetailsPaneManager *MainWindow::getDetailsPaneManager() const {
  return m_appManager->getDetailsPaneManager();
}
ISettingsManager *MainWindow::getSettingsManager() const {
  return m_appManager->getSettingsManager();
}
IDatabaseManager *MainWindow::getDatabaseManager() const {
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
