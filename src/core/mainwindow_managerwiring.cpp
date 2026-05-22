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
#include "eventmanager.h"
#include "interactionmanager.h"
#include "isettingsmanager.h"
#include "kartmanager.h"
#include "keyboardmanager.h"
#include "launchmanager.h"
#include "navigationmanager.h"
#include "nowplayingoverlay.h"
#include "smartfilter.h"
#include "ui_mainwindow.h"

#include <QDesktopServices>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QUrl>

void MainWindow::wireInteractionManager() {
  InteractionManagerSetup setup;
  setup.ctx = &m_appContext; // Managers and UI elements from shared context

  // The input layer's right-click menu used to construct
  // CreateSmartPlaylistDialog / CustomFieldsDialog directly, taking an
  // upward input -> ui edge. The dialogs now run via these closures
  // supplied here in the UI layer; InteractionManager just invokes them
  // and reads the result. The actual dialog construction lives in
  // DialogController so this TU doesn't need the dialog headers.
  setup.runSmartPlaylistDialog = [this](const QString &initialName,
                                        const std::optional<SmartFilter::Filter> &initialFilter) {
    return m_dialogController->runSmartPlaylistDialog(initialName, initialFilter);
  };
  setup.runCustomFieldsDialog = [this](const QString &itemTitle,
                                       const ItemMetadataStore::CustomFieldList &initial) {
    return m_dialogController->runCustomFieldsDialog(itemTitle, initial);
  };

  loadingLabel = ui->loadingLabel;

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
              }
            });
    connect(launch, &LaunchManager::runtimeFinished, this, [this](const QString & /*filePath*/) {
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
      }
      // Bring Kartend back to the foreground when the tracked child
      // exits — the user expects "return on close" behavior.
      raise();
      activateWindow();
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

void MainWindow::wireNavigationManager() {
  NavigationManagerSetup navSetup;
  navSetup.ctx = &m_appContext; // Managers and UI elements from shared context

  // Callbacks (not in context)
  navSetup.isShuttingDown = [this]() { return isShuttingDown(); };
  navSetup.refreshTitleCounts = [this]() { refreshTitleCounts(); };

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
      return m_dialogController->runKartMergeDialog(itemPath, existing, incoming);
    };
    // Kartend-s6mj: interactive confirmation when an imported .kart carries
    // launcher / icon / placeholder paths outside the safe allowlist. The
    // user sees one (field, path) entry per row and must opt in via the
    // non-default "Import anyway" button. Cancel is default to make the
    // safe choice the easy one (a malicious .kart's launcherPath=/bin/sh
    // would otherwise execute on the next Launch click).
    kartSetup.suspiciousPathConfirmer =
        [this](const QList<kart::SuspiciousKartPath> &suspicious) -> bool {
      QStringList lines;
      lines.reserve(suspicious.size());
      for (const auto &[field, path] : suspicious) {
        lines.append(QStringLiteral("• %1: %2").arg(field, path));
      }
      QMessageBox box(this);
      box.setIcon(QMessageBox::Warning);
      box.setWindowTitle(tr("Import Kart — suspicious paths"));
      box.setText(tr("This .kart references paths outside the safe-prefix "
                     "allowlist (your home directory, /usr/bin, /usr/local/bin, "
                     "/opt). Continuing will register these paths and they may "
                     "be executed on a later Launch click."));
      box.setInformativeText(lines.join(QLatin1Char('\n')));
      auto *importAnyway = box.addButton(tr("Import anyway"), QMessageBox::AcceptRole);
      auto *cancel = box.addButton(tr("Cancel"), QMessageBox::RejectRole);
      box.setDefaultButton(cancel);
      box.exec();
      return box.clickedButton() == importAnyway;
    };
    km->setupReferences(kartSetup);

    connect(km, &kart::KartManager::collectionImported, this, [this](const QString &) {
      if (auto *sm = m_appManager->getSettingsManager()) {
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
      m_dialogController->startKartProgressDialog(km, title);
    });
  }
}
