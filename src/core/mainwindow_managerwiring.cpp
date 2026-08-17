// MainWindow's manager-setup wiring (Kartend-dk2c.5).
//
// The wireXxxManager() member functions live here instead of mainwindow.cpp
// so the core file stays under the audit-2026-05-22 LOC budget. Each one
// builds the manager-specific Setup struct, wires owner-supplied UI-layer
// closures (dialog runners, gating predicates), calls setupReferences, then
// connects any post-setup signals the manager needs.
//
// These run once during MainWindow construction, after initializeAppContext()
// has populated ctx with the live manager pointers — that's why the bodies
// here can read managers via m_appManager without worrying about ctx
// ordering. The actual signal/slot graph for high-frequency runtime
// communication lives in mainwindow_wiring.cpp; the split is "first-time
// setup" here vs "ongoing signal routing" there.
#include "mainwindow.h"

#include "applicationmanager.h"
#include "attractmanager.h"
#include "detailpagemanager.h"
#include "detailpageoverlay.h"
#include "detailspanemanager.h"
#include "dialogcontroller.h"
#include "dialogrunners.h"
#include "errorpresentation.h"
#include "eventmanager.h"
#include "gamepadmanager.h"
#include "interactionmanager.h"
#include "iplaylistmanager.h"
#include "isettingsmanager.h"
#include "kartmanager.h"
#include "kartpreflight.h"
#include "kartpreflightdialog.h"
#include "keyboardmanager.h"
#include "launchmanager.h"
#include "navigationmanager.h"
#include "nowplayingoverlay.h"
#include "playlistmanager.h"
#include "smartfilter.h"
#include "toolbarcontroller.h"
#include "ui_mainwindow.h"

#include <QDesktopServices>
#include <QFileDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QTimer>
#include <QUrl>

DialogRunners MainWindow::makeDialogRunners() {
  // Kartend-sqoq0: the generic stock-Qt-modal runners. Modules-layer code
  // used to construct QMessageBox / QInputDialog / QFileDialog directly;
  // these closures move the construction to the UI layer (parented on the
  // main window) while the modules invoke them through their setup structs.
  // Headless tests supply stubs instead and never see a modal; an unset
  // runner falls back to the module's original direct construction.
  DialogRunners r;
  r.confirm = [this](const QString &title, const QString &text) {
    return QMessageBox::question(this, title, text, QMessageBox::Yes | QMessageBox::No,
                                 QMessageBox::No) == QMessageBox::Yes;
  };
  r.warn = [this](const QString &title, const QString &text) {
    QMessageBox::warning(this, title, text);
  };
  r.info = [this](const QString &title, const QString &text) {
    QMessageBox::information(this, title, text);
  };
  r.getText = [this](const QString &title, const QString &label,
                     const QString &initialText) -> std::optional<QString> {
    bool ok = false;
    const QString out =
        QInputDialog::getText(this, title, label, QLineEdit::Normal, initialText, &ok);
    if (!ok) {
      return std::nullopt;
    }
    return out;
  };
  r.getOpenFileName = [this](const QString &caption, const QString &startPath,
                             const QString &filter) {
    return QFileDialog::getOpenFileName(this, caption, startPath, filter);
  };
  r.getSaveFileName = [this](const QString &caption, const QString &startPath,
                             const QString &filter) {
    return QFileDialog::getSaveFileName(this, caption, startPath, filter);
  };
  r.getExistingDirectory = [this](const QString &caption, const QString &startDir) {
    return QFileDialog::getExistingDirectory(
        this, caption, startDir, QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
  };
  return r;
}

void MainWindow::wireInteractionManager() {
  InteractionManagerSetup setup;
  setup.ctx = &m_appContext; // Managers and UI elements from shared context

  // The input layer's right-click menu used to construct
  // CreateSmartPlaylistDialog / EditMetadataDialog directly, taking an
  // upward input -> ui edge. The dialogs now run via these closures
  // supplied here in the UI layer; InteractionManager just invokes them
  // and reads the result. The actual dialog construction lives in
  // DialogController so this TU doesn't need the dialog headers.
  setup.runSmartPlaylistDialog = [this](const QString &initialName,
                                        const std::optional<SmartFilter::Filter> &initialFilter,
                                        const SmartPlaylistCollectionEntries &collections) {
    return m_dialogController->runSmartPlaylistDialog(initialName, initialFilter, collections);
  };
  setup.runEditMetadataDialog = [this](const QString &itemTitle,
                                       const EditMetadataPayload &initial) {
    return m_dialogController->runEditMetadataDialog(itemTitle, initial);
  };
  setup.runLaunchPreviewDialog = [this](const QString &itemTitle, const QString &launcherName,
                                        const QString &filePath, const LaunchPreview &preview) {
    m_dialogController->runLaunchPreviewDialog(itemTitle, launcherName, filePath, preview);
  };
  // Kartend-sqoq0: generic stock-Qt-modal runners for the warn / file-picker
  // / text-prompt sites in the interaction module (launcher-unavailable
  // warning, manual-file picker, playlist menu actions). Threaded onward to
  // PlaylistMenuController by InteractionManager's setup.
  setup.dialogs = makeDialogRunners();

  m_loadingLabel = ui->loadingLabel;

  // ctx is already fully populated by initializeAppContext() — InteractionManager
  // and its owned sub-managers were registered eagerly so every setupReferences()
  // call below can resolve siblings exclusively through ctx.
  m_appManager->getInteractionManager()->setupReferences(setup);

  // when runtime detection is enabled, the LaunchManager spawns
  // a tracked QProcess and emits started/finished signals. Show a "Now
  // Playing" overlay while the child runs and raise the window when it exits.
  if (auto *launch = m_appManager->getInteractionManager()->launchManager()) {
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
              if (auto *interaction = m_appManager->getInteractionManager()) {
                if (auto *attract = interaction->attractManager()) {
                  attract->setSuspended(true);
                }
                // Stop routing gamepad presses/directions to the frontend while
                // the launched program is foregrounded (Kartend-5rpt).
                if (auto *gamepad = interaction->gamepadManager()) {
                  gamepad->setSuspended(true);
                }
              }
            });
    connect(launch, &LaunchManager::runtimeFinished, this, [this](const QString & /*filePath*/) {
      // A tracked child can exit during/after shutdown; don't re-arm
      // attract/gamepad or re-raise the closing window then (Kartend-42n7u).
      if (m_isShuttingDown) {
        return;
      }
      if (m_nowPlayingOverlay) {
        m_nowPlayingOverlay->hideOverlay();
      }
      // lift the attract suspension. setSuspended(false)
      // re-arms a fresh idle countdown so attract waits the full timeout
      // instead of kicking in the instant the launch ends.
      if (auto *interaction = m_appManager->getInteractionManager()) {
        if (auto *attract = interaction->attractManager()) {
          attract->setSuspended(false);
        }
        if (auto *gamepad = interaction->gamepadManager()) {
          gamepad->setSuspended(false);
        }
      }
      // Bring Kartend back to the foreground when the tracked child
      // exits — the user expects "return on close" behavior.
      raise();
      activateWindow();
    });

    // Kartend-mkcak: archive extraction now runs on a worker thread; show
    // the same overlay as a busy state so a slow extraction doesn't look
    // like a dead launch. extractionFinished always fires before the launch
    // continues, so on success the overlay flips back to "Now Playing" when
    // the tracked child starts.
    connect(launch, &LaunchManager::extractionStarted, this,
            [this](const QString & /*filePath*/, const QString &displayName) {
              if (m_nowPlayingOverlay) {
                m_nowPlayingOverlay->showOverlay(displayName, tr("Extracting Archive"),
                                                 tr("Preparing to launch..."));
              }
            });
    connect(launch, &LaunchManager::extractionFinished, this, [this](const QString & /*filePath*/) {
      if (m_isShuttingDown) {
        return;
      }
      if (m_nowPlayingOverlay) {
        m_nowPlayingOverlay->hideOverlay();
      }
    });

    // Kartend-3232r.1: detached (fire-and-forget) launches — the default,
    // runtime detection off — get the same attract/gamepad suspension as
    // tracked ones, via their own signal pair: no "Now Playing" overlay (the
    // child is unsupervised) and no raise()/activateWindow() on the ended
    // side (a double-forking launcher's QProcess exits seconds in while the
    // program keeps running; raising would steal its focus).
    connect(launch, &LaunchManager::detachedSessionStarted, this,
            [this](const QString & /*filePath*/, const QString & /*displayName*/) {
              if (m_MetadataSidebar) {
                m_MetadataSidebar->pausePreviewVideo();
              }
              if (auto *interaction = m_appManager->getInteractionManager()) {
                if (auto *attract = interaction->attractManager()) {
                  attract->setSuspended(true);
                }
                if (auto *gamepad = interaction->gamepadManager()) {
                  gamepad->setSuspended(true);
                }
              }
              m_detachedSuspendActive = true;
              m_detachedSessionSawDeactivate = false;
              m_detachedSuspendSince.start();
              // singleShot: a detached child carries no focus guarantee — a
              // launcher with no window of its own (audio player, CLI tool)
              // never deactivates us, so the WindowActivate resume backstop
              // could never fire and the gamepad would stay dead under a
              // fully focused frontend. Probe once after the program has had
              // time to take focus and resume if it never did; `this` scopes
              // the probe to the window's lifetime, and the elapsed check
              // makes a stale probe from an already-ended session a no-op
              // for a newer one (the newer start restarted the timer, so its
              // own probe is the one that counts).
              QTimer::singleShot(kDetachedFocusProbeMs, this, [this]() {
                if (m_detachedSuspendActive && !m_detachedSessionSawDeactivate &&
                    m_detachedSuspendSince.isValid() &&
                    m_detachedSuspendSince.elapsed() >= kDetachedFocusProbeMs) {
                  resumeAfterDetachedSession();
                }
              });
            });
    connect(launch, &LaunchManager::detachedSessionEnded, this,
            [this](const QString & /*filePath*/) {
              // A detached child's final finished can land during shutdown
              // teardown; don't re-arm attract/gamepad then (same guard as
              // runtimeFinished above, Kartend-42n7u).
              if (m_isShuttingDown) {
                return;
              }
              resumeAfterDetachedSession();
            });
  }

  // EventManager gates item-grid input while a modal scrape dialog is up,
  // and LaunchManager prompts a launcher chooser for multi-launcher
  // collections. Both dialog types live in the UI layer, so MainWindow
  // injects the callbacks rather than the input module including the
  // dialog headers.
  if (auto *interaction = m_appManager->getInteractionManager()) {
    if (auto *events = interaction->eventManager()) {
      events->setModalScrapeDialogVisiblePredicate(
          []() { return DialogController::anyScrapeResultDialogVisible(); });
    }
    if (auto *launch = interaction->launchManager()) {
      launch->setChooseLauncherCallback([this](const QString &collectionName,
                                               const QStringList &launcherNames, int defaultIndex) {
        return m_dialogController->chooseLauncher(collectionName, launcherNames, defaultIndex);
      });
    }
  }
}

void MainWindow::resumeAfterDetachedSession() {
  // Kartend-3232r.1: lift the detached-session suspension exactly once —
  // whichever of the balanced detachedSessionEnded, the WindowActivate focus
  // backstop, or the never-took-focus probe fires first wins; the others
  // no-op on the cleared flag. setSuspended(false) re-arms a fresh attract
  // idle countdown (the only sanctioned attract entry point — no
  // selection-driving here).
  if (!m_detachedSuspendActive) {
    return;
  }
  m_detachedSuspendActive = false;
  auto *interaction = m_appManager ? m_appManager->getInteractionManager() : nullptr;
  if (!interaction) {
    return;
  }
  if (auto *launch = interaction->launchManager()) {
    // The user is demonstrably back (or the child exited): a new launch is
    // intentional again, so drop the detached single-child block even while
    // the previous child (or its double-forked descendant) lives on.
    launch->releaseDetachedLaunchBlock();
    // A live tracked session owns the suspension now (the runtime-detection
    // setting flipped mid-detached-session): leave attract/gamepad suspended
    // — that session's own runtimeFinished resumes them.
    if (launch->isRuntimeChildRunning()) {
      return;
    }
  }
  if (auto *attract = interaction->attractManager()) {
    attract->setSuspended(false);
  }
  if (auto *gamepad = interaction->gamepadManager()) {
    gamepad->setSuspended(false);
  }
}

void MainWindow::wireNavigationManager() {
  NavigationManagerSetup navSetup;
  navSetup.ctx = &m_appContext; // Managers and UI elements from shared context

  // Callbacks (not in context)
  navSetup.isShuttingDown = [this]() { return isShuttingDown(); };
  navSetup.refreshTitleCounts = [this]() { refreshTitleCounts(); };
  navSetup.refreshCollectionWarningBadge = [this]() {
    if (m_toolbarController) {
      m_toolbarController->refreshCollectionWarningBadge();
    }
  };

  m_appManager->getNavigationManager()->setupReferences(navSetup);
}

void MainWindow::wireDetailPageManager() {
  // detail page wiring. The overlay was created in
  // mainwindow_setup.cpp and parented to ui->centralwidget so it can cover
  // the entire window. Hand it to DetailPageManager along with the sidebar
  // and DB so the manager can build payloads. Keyboard signal lives on
  // InteractionManager's owned KeyboardManager.
  if (auto *detail = m_appManager->getDetailPageManager()) {
    DetailPageManagerSetup detailSetup;
    detailSetup.ctx = &m_appContext;
    detailSetup.overlay = m_detailPageOverlay;
    detail->setupReferences(detailSetup);

    if (auto *kb = m_appManager->getInteractionManager()
                       ? m_appManager->getInteractionManager()->keyboardManager()
                       : nullptr) {
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
        if (auto *sb = m_appManager->getDetailsPaneManager()) {
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
}

void MainWindow::wireKartManager() {
  if (auto *km = m_appManager->getKartManager()) {
    kart::KartManagerSetup kartSetup;
    kartSetup.ctx = &m_appContext;
    kartSetup.getCollections = [this]() { return &m_collections; };
    kartSetup.getLauncherPresets = [this]() { return m_generalSettings.launchers.launcherPresets; };
    kartSetup.getParentWindow = [this]() -> QWidget * { return this; };
    // Kartend-kmj1: round-trip the active collection's playlists through the
    // bundle. Returns a base IPlaylistManager* so the data layer doesn't
    // need to know the concrete PlaylistManager class.
    kartSetup.getPlaylistManager = [this]() -> IPlaylistManager * {
      return m_appManager ? m_appManager->getPlaylistManager() : nullptr;
    };
    // Kartend-sqoq0: generic runners for the import/export file pickers and
    // failure warnings KartManager used to construct directly.
    kartSetup.dialogs = makeDialogRunners();
    // Kartend-a3ir: the interactive merge dialog lives in the UI layer
    // and is constructed here. KartManager (data layer) just invokes the
    // closure with the conflicting metadata and uses the returned
    // ConflictResolution. The dialog parents to `this` so it modals
    // correctly over the main window.
    kartSetup.mergeResolver = [this](const QString &itemPath,
                                     const ItemMetadataStore::ItemMetadata &existing,
                                     const ItemMetadataStore::ItemMetadata &incoming) {
      return m_dialogController->runKartMergeDialog(itemPath, existing, incoming);
    };
    // Kartend-s6mj / Kartend-kxqqf: interactive confirmation when an imported
    // .kart carries a launcher configuration — the program to run, the core
    // to load and the arguments to pass — or an icon / placeholder path
    // outside the safe allowlist. The user sees one (field, value) entry per
    // row, each labelled with why it is listed, and must opt in via the
    // non-default "Import anyway" button. Cancel is default to make the safe
    // choice the easy one: whatever the manifest names here is what a later
    // Launch click executes.
    kartSetup.suspiciousPathConfirmer =
        [this](const QList<kart::SuspiciousKartPath> &suspicious) -> bool {
      QStringList lines;
      lines.reserve(suspicious.size());
      for (const auto &[field, path] : suspicious) {
        lines.append(QStringLiteral("• %1: %2").arg(field, path));
      }
      QMessageBox box(this);
      box.setIcon(QMessageBox::Warning);
      box.setWindowTitle(tr("Import Kart — launcher configuration"));
      box.setText(tr("This .kart brings its own launcher settings. Importing it "
                     "registers the program below, along with the arguments it "
                     "will be started with, and a later Launch click runs them. "
                     "Import a bundle you did not make only if you trust its "
                     "source as much as you would trust a program it sent you."));
      box.setInformativeText(lines.join(QLatin1Char('\n')));
      auto *importAnyway = box.addButton(tr("Import anyway"), QMessageBox::AcceptRole);
      auto *cancel = box.addButton(tr("Cancel"), QMessageBox::RejectRole);
      box.setDefaultButton(cancel);
      box.exec();
      return box.clickedButton() == importAnyway;
    };
    // Kartend-fr4z: pre-extraction preflight dialog. Surfaces a unified
    // summary (bundle contents + every validation concern) before the
    // user picks a destination directory; rejecting here costs nothing
    // on disk.
    kartSetup.preflightConfirmer = [this](const KartPreflight::PreflightReport &report) -> bool {
      KartPreflightDialog dialog(report, this);
      return dialog.exec() == QDialog::Accepted;
    };
    // Kartend-jset: post-import notice when imported launcher paths don't
    // resolve on this host (common when the .kart was built on a different
    // machine). Informational only — the collection has already imported;
    // this just tells the user which paths to fix in Settings → Launchers.
    // The detail line is copyable from the QMessageBox text so the user
    // can paste it into a follow-up note.
    kartSetup.missingLauncherPathsReporter =
        [this](const QString &collectionName, const QList<kart::MissingLauncherPathIssue> &issues) {
          QStringList lines;
          lines.reserve(issues.size());
          for (const auto &[field, desc] : issues) {
            lines.append(QStringLiteral("• %1: %2").arg(field, desc));
          }
          QMessageBox box(this);
          box.setIcon(QMessageBox::Information);
          box.setWindowTitle(tr("Imported Kart — launcher paths to review"));
          box.setText(tr("The imported collection '%1' references launcher paths that do "
                         "not resolve on this host. The collection was imported successfully; "
                         "fix the paths in Settings → Launchers before launching items.")
                          .arg(collectionName));
          // setTextInteractionFlags + setInformativeText lets the user
          // select and copy the path list.
          box.setInformativeText(lines.join(QLatin1Char('\n')));
          box.setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
          box.addButton(QMessageBox::Ok);
          box.exec();
        };
    km->setupReferences(kartSetup);

    // Deliberately an immediate save, not the debounced per-click stage: a
    // .kart import completing is a one-shot external event (worker-thread
    // extraction finished), and the just-registered collection must survive
    // an immediate quit — there is no click burst to coalesce here.
    connect(km, &kart::KartManager::collectionImported, this, [this](const QString &) {
      if (auto *sm = m_appManager->getSettingsManager()) {
        ErrorPresentation::reportSaveResult(sm->saveCollections(m_collections), "collections",
                                            false);
      }
    });

    // KartManager is a data-layer manager and no longer #includes the
    // KartProgressDialog (a ui/ type). It emits a kartProgressStarted signal
    // when a long import/export begins; the owner builds the dialog here and
    // wires the remaining progress/lifecycle signals to it. The dialog is the
    // connection context for those onward hops, so they tear down when it
    // closes (WA_DeleteOnClose). cancelRequested routes back to KartManager.
    connect(km, &kart::KartManager::kartProgressStarted, this, [this, km](const QString &title) {
      m_dialogController->startKartProgressDialog(km, title);
    });

    // Kartend-h7xnr.1: sequential drop-drain continuation. While a drag-drop
    // drain is active (m_kartImportInProgress), each operation's terminal
    // signal dispatches the next queued job, so dropped .kart files extract
    // one at a time on the worker and the guard stays up until the queue
    // empties. Exactly one of collectionImported / importFailed fires per
    // runImport; the export pair covers a drain that started while a
    // menu-driven export was mid-run (dispatchNextKartImport defers on
    // operationInFlight and resumes here). Menu-driven operations leave the
    // guard false, so this is a no-op for them.
    connect(km, &kart::KartManager::collectionImported, this, &MainWindow::onKartOperationFinished);
    connect(km, &kart::KartManager::importFailed, this, &MainWindow::onKartOperationFinished);
    connect(km, &kart::KartManager::kartExported, this, &MainWindow::onKartOperationFinished);
    connect(km, &kart::KartManager::exportFailed, this, &MainWindow::onKartOperationFinished);
  }
}
