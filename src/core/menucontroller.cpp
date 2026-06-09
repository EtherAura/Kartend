// Handles menu bar setup and action connections, extracted from MainWindow.
#include "menucontroller.h"
#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "detailspanemanager.h"
#include "errorpresentation.h"
#include "historystore.h"
#include "idatabasemanager.h"
#include "isettingsmanager.h"
#include "navigationmanager.h"
#include "scrolldatamanager.h"
#include "scrollmanager.h"
#include "shortcutsdialog.h"
#include "statisticsdialog.h"
#include "ui_mainwindow.h"
#include "uiconstants/icons.h"

#include <algorithm>
#include <QApplication>
#include <QFileInfo>
#include <QFontMetrics>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QRandomGenerator>
#include <QSize>
#include <QToolButton>

namespace {

// Sort / layout / orientation actions share a setup-then-sync pattern:
// each entry binds a QAction (from .ui or a member of MenuController) to
// a model value. The setup loop wires `triggered → write+save+reload`;
// the sync loop just walks the same table to drive `setChecked`. Two
// callsites per group → one table per group keeps each group's
// shape declarative.

struct SortActionEntry {
  QAction *Ui_MainWindow::*action;
  SortMode mode;
};

// Order matches the .ui menu so the action group's tab order is stable.
// SortMode::ArtworkFirst/ArtworkLast/CollectionAsc/CollectionDesc are
// list-view-only and intentionally absent — syncSortActions leaves the
// previously-checked menu entry alone when sortMode is one of those.
constexpr SortActionEntry kSortActions[] = {
    {&Ui_MainWindow::actionSortNameAsc, SortMode::NameAscending},
    {&Ui_MainWindow::actionSortNameDesc, SortMode::NameDescending},
    {&Ui_MainWindow::actionSortDateDesc, SortMode::DateDescending},
    {&Ui_MainWindow::actionSortDateAsc, SortMode::DateAscending},
    {&Ui_MainWindow::actionSortSizeDesc, SortMode::SizeDescending},
    {&Ui_MainWindow::actionSortSizeAsc, SortMode::SizeAscending},
    {&Ui_MainWindow::actionSortRandom, SortMode::Random},
};

} // namespace

MenuController::MenuController(QObject *parent) : QObject(parent) {}

MenuController::~MenuController() = default;

void MenuController::setContext(const MenuControllerContext &context) {
  m_ctx = context;
}

// ─── Setup helpers ───────────────────────────────────────────────────────

bool MenuController::connectGlobalAction(QAction *action, std::function<void()> handler) {
  if (!action || !m_ctx.mainWindow) {
    return false;
  }
  connect(action, &QAction::triggered, this, std::move(handler));
  action->setShortcutContext(Qt::ApplicationShortcut);
  m_ctx.mainWindow->addAction(action);
  return true;
}

bool MenuController::connectMenuAction(QAction *action, std::function<void()> handler) {
  if (!action) {
    return false;
  }
  connect(action, &QAction::triggered, this, std::move(handler));
  return true;
}

bool MenuController::connectVisibilityToggle(QAction *action,
                                             const std::function<void(bool)> &applyVisual,
                                             bool ViewSettings::*field) {
  if (!action || !m_ctx.mainWindow) {
    return false;
  }
  connect(action, &QAction::triggered, this, [this, applyVisual, field](bool checked) {
    if (applyVisual) {
      applyVisual(checked);
    }
    // persist explicit user toggle.
    if (m_ctx.getGeneralSettings) {
      if (auto *settings = m_ctx.getGeneralSettings()) {
        settings->view.*field = checked;
        if (m_ctx.getSettingsManager) {
          if (auto *mgr = m_ctx.getSettingsManager()) {
            ErrorPresentation::reportSaveResult(mgr->saveGeneralSettings(*settings),
                                                "general settings", true);
          }
        }
      }
    }
  });
  action->setShortcutContext(Qt::ApplicationShortcut);
  m_ctx.mainWindow->addAction(action);
  return true;
}

void MenuController::setupMenuBar() {
  setupActionExit();
  setupActionShowMenuBar();
  setupActionShowToolbar();
  setupActionShowSidebar();
  setupActionSettings();
  setupActionRefresh();
  setupSortActions();
  setupActionAbout();
  setupActionAboutQt();
  setupFullscreenAction();
  setupShortcutsAction();
  setupStatisticsAction();
  setupFirstRunWizardAction();
  // Scraper Credentials moved out of the Help menu; the editor now
  // lives inline under Settings → Scrapers → Credentials. setup
  // helper is kept (still wired through MenuControllerContext) but
  // unregistered from the menu so the action object isn't shown.
  setupBatchScrapeAction();
  setupDatAuditAction();
  setupGridWidthActions();
  setupActionOpenRandomItem();
  setupActionImportKart();
  setupActionExportKart();
  setupActionImportTheme();
  setupActionExportTheme();
  setupActionLayoutProfiles();
  setupActionCollectionHealth();
  setupActionVariantGrouping();
  setupActionBulkEdit();
  setupActionReviewMissingMetadata();
  setupActionArtworkWizard();
  setupActionBindingVisualizer();
  setupActionNewLibraryWizard();
  setupActionPresentationProfiles();
  setupActionScraperProviders();
  setupRecentMenu();
  setupMostLaunchedMenu();
  setupLayoutActions();
  setupActionDetailsPaneOrientation();
  setupHamburgerMenu();
  applyPersistedViewState();
}

void MenuController::setupActionExit() {
  if (!m_ctx.ui) return;
  connectGlobalAction(m_ctx.ui->actionExit, [this]() {
    if (m_ctx.mainWindow) m_ctx.mainWindow->close();
  });
}

void MenuController::setupActionShowMenuBar() {
  if (!m_ctx.ui) return;
  connectVisibilityToggle(
      m_ctx.ui->actionShowMenuBar,
      [this](bool checked) {
        if (m_ctx.ui->menubar) {
          m_ctx.ui->menubar->setVisible(checked);
        }
        syncHamburgerVisibility();
      },
      &ViewSettings::showMenuBar);
}

void MenuController::setupActionShowToolbar() {
  if (!m_ctx.ui) return;
  connectVisibilityToggle(
      m_ctx.ui->actionShowToolbar,
      [this](bool checked) {
        if (m_ctx.ui->itemsTopBar) {
          m_ctx.ui->itemsTopBar->setVisible(checked);
        }
      },
      &ViewSettings::showToolbar);
}

void MenuController::setupActionShowSidebar() {
  if (!m_ctx.ui) return;
  connectGlobalAction(m_ctx.ui->actionShowSidebar, [this]() {
    if (m_ctx.getDetailsPaneManager) {
      if (auto *mgr = m_ctx.getDetailsPaneManager()) {
        mgr->toggleSidebar();
      }
    }
  });
}

void MenuController::setupActionSettings() {
  if (!m_ctx.ui) return;
  connectGlobalAction(m_ctx.ui->actionSettings, [this]() {
    if (m_ctx.onOpenSettings) {
      m_ctx.onOpenSettings();
    }
  });
}

void MenuController::setupActionAbout() {
  if (!m_ctx.ui) return;
  connectMenuAction(m_ctx.ui->actionAbout, [this]() {
    if (m_ctx.onShowAbout) {
      m_ctx.onShowAbout();
    }
  });
}

void MenuController::setupActionAboutQt() {
  if (!m_ctx.ui) return;
  connectMenuAction(m_ctx.ui->actionAboutQt, []() { QApplication::aboutQt(); });
}

void MenuController::setupActionRefresh() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;

  // Hard refresh (Ctrl+F5) - rescan the database
  if (m_ctx.ui->actionRefresh) {
    connect(m_ctx.ui->actionRefresh, &QAction::triggered, this, [this]() {
      if (m_ctx.getNavigationManager && m_ctx.getCurrentCollectionIndex) {
        int idx = m_ctx.getCurrentCollectionIndex();
        if (auto *mgr = m_ctx.getNavigationManager(); mgr && idx >= 0) {
          mgr->forceRescanCollection(idx);
        }
      }
    });
    m_ctx.ui->actionRefresh->setShortcutContext(Qt::ApplicationShortcut);
    m_ctx.mainWindow->addAction(m_ctx.ui->actionRefresh);
  }

  // Soft refresh (F5) - just reload the view without database rescan
  if (m_ctx.ui->actionSoftRefresh) {
    connect(m_ctx.ui->actionSoftRefresh, &QAction::triggered, this, [this]() {
      if (m_ctx.getNavigationManager && m_ctx.getCurrentCollectionIndex) {
        int idx = m_ctx.getCurrentCollectionIndex();
        if (auto *mgr = m_ctx.getNavigationManager(); mgr && idx >= 0) {
          mgr->safeReloadCollection(idx);
        }
      }
    });
    m_ctx.ui->actionSoftRefresh->setShortcutContext(Qt::ApplicationShortcut);
    m_ctx.mainWindow->addAction(m_ctx.ui->actionSoftRefresh);
  }
}

void MenuController::setupSortActions() {
  if (!m_ctx.ui) return;

  // Mutually-exclusive group for the sort-mode entries. The
  // excludeSubfoldersFromSort toggle below intentionally stays outside
  // the group (it's an independent boolean, not a sort-mode option).
  m_sortActionGroup = new QActionGroup(this);
  m_sortActionGroup->setExclusive(true);

  auto reloadIfNeeded = [this]() {
    if (m_ctx.getNavigationManager && m_ctx.getCurrentCollectionIndex) {
      int idx = m_ctx.getCurrentCollectionIndex();
      if (auto *mgr = m_ctx.getNavigationManager(); mgr && idx >= 0) {
        mgr->safeReloadCollection(idx);
      }
    }
  };

  for (const auto &entry : kSortActions) {
    QAction *action = m_ctx.ui->*entry.action;
    if (!action) continue;
    m_sortActionGroup->addAction(action);
    const SortMode mode = entry.mode;
    connect(action, &QAction::triggered, this, [this, mode, reloadIfNeeded]() {
      if (m_ctx.getGeneralSettings) {
        if (auto *settings = m_ctx.getGeneralSettings()) {
          settings->view.sortMode = mode;
          if (m_ctx.getSettingsManager) {
            if (auto *mgr = m_ctx.getSettingsManager()) {
              ErrorPresentation::reportSaveResult(mgr->saveGeneralSettings(*settings),
                                                  "general settings", true);
            }
          }
          reloadIfNeeded();
        }
      }
    });
  }

  // Exclude subfolders is an independent toggle, not a member of the
  // sort-mode group.
  if (m_ctx.ui->actionSortSubfolders) {
    connect(m_ctx.ui->actionSortSubfolders, &QAction::triggered, this,
            [this, reloadIfNeeded](bool checked) {
              if (m_ctx.getGeneralSettings) {
                if (auto *settings = m_ctx.getGeneralSettings()) {
                  settings->view.excludeSubfoldersFromSort = checked;
                  if (m_ctx.getSettingsManager) {
                    if (auto *mgr = m_ctx.getSettingsManager()) {
                      ErrorPresentation::reportSaveResult(mgr->saveGeneralSettings(*settings),
                                                          "general settings", true);
                    }
                  }
                  reloadIfNeeded();
                }
              }
            });
  }

  syncSortActions();
}

void MenuController::syncSortActions() {
  if (!m_ctx.ui || !m_ctx.getGeneralSettings) return;

  auto *settings = m_ctx.getGeneralSettings();
  if (!settings) return;

  // Walk the table for an exact mode match. List-view-only modes
  // (ArtworkFirst/ArtworkLast/CollectionAsc/CollectionDesc) have no
  // entry, so the loop body is skipped and the previously-checked
  // menu action remains checked — matching the prior switch's behavior.
  for (const auto &entry : kSortActions) {
    if (settings->view.sortMode != entry.mode) continue;
    if (QAction *action = m_ctx.ui->*entry.action) {
      action->setChecked(true);
    }
    break;
  }
  if (m_ctx.ui->actionSortSubfolders) {
    m_ctx.ui->actionSortSubfolders->setChecked(settings->view.excludeSubfoldersFromSort);
  }
}

void MenuController::setupFullscreenAction() {
  if (!m_ctx.mainWindow) return;

  m_fullscreenAction = new QAction(tr("Fullscreen"), this);
  m_fullscreenAction->setCheckable(true);
  m_fullscreenAction->setShortcut(QKeySequence(Qt::Key_F11));
  m_fullscreenAction->setShortcutContext(Qt::ApplicationShortcut);
  m_ctx.mainWindow->addAction(m_fullscreenAction);

  insertFullscreenInViewMenu(m_fullscreenAction);

  connect(m_fullscreenAction, &QAction::triggered, this, [this]() {
    bool entering = !m_ctx.mainWindow->isFullScreen();
    if (entering) {
      m_ctx.mainWindow->showFullScreen();
      if (m_ctx.mainWindow->menuBar()) {
        m_ctx.mainWindow->menuBar()->hide();
      }
      m_fullscreenAction->setChecked(true);
    } else {
      m_ctx.mainWindow->showNormal();
      if (m_ctx.mainWindow->menuBar()) {
        m_ctx.mainWindow->menuBar()->show();
      }
      m_fullscreenAction->setChecked(false);
    }
    syncHamburgerVisibility();
    // persist explicit user toggle. We only mirror the fullscreen
    // flag — the menu-bar auto-hide/show inside this lambda is a transient UI
    // affordance, not a change to the user's saved Show Menu Bar preference.
    if (m_ctx.getGeneralSettings) {
      if (auto *settings = m_ctx.getGeneralSettings()) {
        settings->view.fullscreen = entering;
        if (m_ctx.getSettingsManager) {
          if (auto *mgr = m_ctx.getSettingsManager()) {
            ErrorPresentation::reportSaveResult(mgr->saveGeneralSettings(*settings),
                                                "general settings", true);
          }
        }
      }
    }
  });
}

void MenuController::applyPersistedViewState() {
  if (!m_ctx.mainWindow || !m_ctx.getGeneralSettings) return;
  GeneralSettings *settings = m_ctx.getGeneralSettings();
  if (!settings) return;

  // Menu bar: setChecked() emits toggled (not triggered), so the persistence
  // lambda above is not re-entered during restore.
  if (m_ctx.ui && m_ctx.ui->actionShowMenuBar && m_ctx.ui->menubar) {
    m_ctx.ui->actionShowMenuBar->setChecked(settings->view.showMenuBar);
    m_ctx.ui->menubar->setVisible(settings->view.showMenuBar);
  }

  // Toolbar
  if (m_ctx.ui && m_ctx.ui->actionShowToolbar && m_ctx.ui->itemsTopBar) {
    m_ctx.ui->actionShowToolbar->setChecked(settings->view.showToolbar);
    m_ctx.ui->itemsTopBar->setVisible(settings->view.showToolbar);
  }

  // Fullscreen: called from setupMenuBar() during MainWindow construction —
  // before main.cpp's window.show(). showFullScreen() makes the window visible
  // in fullscreen state; the subsequent show() leaves the state intact. We
  // also hide the menu bar here to mirror the F11 lambda's coupling so the
  // restored chrome matches what the user saw when they last enabled it.
  if (m_fullscreenAction) {
    if (settings->view.fullscreen) {
      m_ctx.mainWindow->showFullScreen();
      if (m_ctx.mainWindow->menuBar()) {
        m_ctx.mainWindow->menuBar()->hide();
      }
      m_fullscreenAction->setChecked(true);
    } else {
      m_fullscreenAction->setChecked(false);
    }
  }

  syncHamburgerVisibility();
}

void MenuController::insertFullscreenInViewMenu(QAction *fullscreenAction) {
  if (!m_ctx.ui || !m_ctx.ui->menuView) return;

  QList<QAction *> acts = m_ctx.ui->menuView->actions();
  QAction *insertBefore = nullptr;
  for (QAction *action : acts) {
    if (action == m_ctx.ui->actionSettings) {
      insertBefore = action;
      break;
    }
  }
  if (insertBefore) {
    m_ctx.ui->menuView->insertAction(insertBefore, fullscreenAction);
  } else {
    m_ctx.ui->menuView->addAction(fullscreenAction);
  }
}

void MenuController::setupShortcutsAction() {
  m_shortcutsAction = new QAction(tr("Keyboard Shortcuts"), this);
  m_shortcutsAction->setShortcut(QKeySequence(Qt::Key_F1));
  if (!connectGlobalAction(m_shortcutsAction, [this]() {
        ShortcutsDialog dialog(m_ctx.mainWindow);
        dialog.exec();
      })) {
    return;
  }
  if (m_ctx.ui && m_ctx.ui->menuHelp) {
    m_ctx.ui->menuHelp->addAction(m_shortcutsAction);
  }
}

void MenuController::setupStatisticsAction() {
  // Programmatic action (no .ui entry yet) so the menu wiring stays self-
  // contained alongside the dialog. Lives in the Help menu next to Shortcuts.
  m_statisticsAction = new QAction(tr("Usage Statistics…"), this);
  if (!connectGlobalAction(m_statisticsAction, [this]() {
        IDatabaseManager *db = m_ctx.getDatabaseManager ? m_ctx.getDatabaseManager() : nullptr;
        QList<CollectionConfig> *collections =
            m_ctx.getCollections ? m_ctx.getCollections() : nullptr;
        GeneralSettings *settings = m_ctx.getGeneralSettings ? m_ctx.getGeneralSettings() : nullptr;
        ISettingsManager *settingsMgr =
            m_ctx.getSettingsManager ? m_ctx.getSettingsManager() : nullptr;
        const bool runtimeOn = settings && settings->runtimeDetection.runtimeDetectionEnabled;
        StatisticsDialog dialog(db, collections, runtimeOn, settings, settingsMgr,
                                m_ctx.mainWindow);
        // Wire the navigate-on-double-click signal: dismiss the dialog and
        // hand off to MainWindow's navigation handler. accept() unblocks
        // exec() so the closure below runs without lingering in the
        // modal stack.
        QObject::connect(&dialog, &StatisticsDialog::navigateToItemRequested, &dialog,
                         [this, &dialog](const QString &filePath) {
                           dialog.accept();
                           if (m_ctx.onNavigateToItem) {
                             m_ctx.onNavigateToItem(filePath);
                           }
                         });
        dialog.exec();
      })) {
    return;
  }
  if (m_ctx.ui && m_ctx.ui->menuHelp) {
    m_ctx.ui->menuHelp->addAction(m_statisticsAction);
  }
}

void MenuController::setupFirstRunWizardAction() {
  // Same programmatic-action pattern as setupStatisticsAction. Lives in the
  // Help menu next to Usage Statistics; the auto-launch on first run is
  // wired separately in MainWindow::setupInitialTimers().
  m_firstRunWizardAction = new QAction(tr("Setup Wizard…"), this);
  if (!connectMenuAction(m_firstRunWizardAction, [this]() {
        if (m_ctx.onShowFirstRunWizard) {
          m_ctx.onShowFirstRunWizard();
        }
      })) {
    return;
  }
  if (m_ctx.ui && m_ctx.ui->menuHelp) {
    m_ctx.ui->menuHelp->addAction(m_firstRunWizardAction);
  }
}

void MenuController::setupBatchScrapeAction() {
  // Programmatic File-menu entry that opens the unified Scraper
  // dialog (tree of collections + items, media-type checkboxes,
  // auto/interactive toggle). Replaces the legacy "Batch Scrape
  // Current Collection" Help-menu entry — the dialog now drives
  // both individual and batch scrapes off one window.
  m_batchScrapeAction = new QAction(tr("Scraper…"), this);
  if (!connectMenuAction(m_batchScrapeAction, [this]() {
        if (m_ctx.onRunBatchScrape) {
          m_ctx.onRunBatchScrape();
        }
      })) {
    return;
  }
  if (m_ctx.ui && m_ctx.ui->menuFile) {
    // Tuck Scraper above Exit so the destructive action stays at the
    // bottom. The File menu currently ends with separator + Exit; we
    // insert before that final separator.
    QAction *exitAction = m_ctx.ui->actionExit;
    if (exitAction) {
      m_ctx.ui->menuFile->insertAction(exitAction, m_batchScrapeAction);
      m_ctx.ui->menuFile->insertSeparator(exitAction);
    } else {
      m_ctx.ui->menuFile->addAction(m_batchScrapeAction);
    }
  }
}

void MenuController::setupDatAuditAction() {
  // File-menu entry that opens the standalone DAT Audit window (scan folders
  // against DAT catalogues; report have/missing/wrong-name/unknown; export
  // CSV / fixdat / miss-list). Sits next to Scraper, above Exit.
  m_datAuditAction = new QAction(tr("DAT Audit…"), this);
  if (!connectMenuAction(m_datAuditAction, [this]() {
        if (m_ctx.onRunDatAudit) {
          m_ctx.onRunDatAudit();
        }
      })) {
    return;
  }
  if (m_ctx.ui && m_ctx.ui->menuFile) {
    QAction *exitAction = m_ctx.ui->actionExit;
    if (exitAction) {
      m_ctx.ui->menuFile->insertAction(exitAction, m_datAuditAction);
    } else {
      m_ctx.ui->menuFile->addAction(m_datAuditAction);
    }
  }
}

void MenuController::setupActionScraperProviders() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;
  m_scraperProvidersAction = new QAction(tr("Scraper Providers..."), this);
  m_ctx.mainWindow->addAction(m_scraperProvidersAction);
  // Sits in the Help menu next to Scraper Credentials so configuration
  // affordances stay grouped.
  if (m_ctx.ui->menuHelp) {
    m_ctx.ui->menuHelp->addAction(m_scraperProvidersAction);
  }
  connect(m_scraperProvidersAction, &QAction::triggered, this, [this]() {
    if (m_ctx.onShowScraperProviders) m_ctx.onShowScraperProviders();
  });
}

void MenuController::setupScraperCredentialsAction() {
  // Help-menu entry that opens the per-provider credential editor.
  // Sits next to Setup Wizard / Usage Statistics so configuration
  // affordances stay grouped.
  m_scraperCredentialsAction = new QAction(tr("Scraper Credentials…"), this);
  if (!connectMenuAction(m_scraperCredentialsAction, [this]() {
        if (m_ctx.onShowScraperCredentials) {
          m_ctx.onShowScraperCredentials();
        }
      })) {
    return;
  }
  if (m_ctx.ui && m_ctx.ui->menuHelp) {
    m_ctx.ui->menuHelp->addAction(m_scraperCredentialsAction);
  }
}

void MenuController::setupGridWidthActions() {
  if (!m_ctx.mainWindow) return;

  // grid-width chord lives on Ctrl+Shift+/- so Ctrl+/- stays free for text
  // zoom (see MainWindow::setupTextZoomShortcuts). On a US keyboard '+' is
  // Shift+'=', so "Ctrl++" (zoom-in) and "Ctrl+Shift++" (this action) are the
  // SAME keypress (Ctrl+Shift+=); to keep it unambiguous, text zoom-in binds
  // only Ctrl+= (NOT Ctrl+Plus). Bind both Plus and Equal here so US (Shift+=)
  // and EU (dedicated + key) layouts both reach the increase action.
  m_gridWidthIncreaseAction = new QAction(tr("Increase Grid Width"), this);
  m_gridWidthIncreaseAction->setShortcuts({QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Plus),
                                           QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Equal)});
  m_gridWidthIncreaseAction->setShortcutContext(Qt::ApplicationShortcut);
  m_ctx.mainWindow->addAction(m_gridWidthIncreaseAction);
  connect(m_gridWidthIncreaseAction, &QAction::triggered, this, [this]() {
    if (m_ctx.onAdjustGridWidth) {
      m_ctx.onAdjustGridWidth(1);
    }
  });

  m_gridWidthDecreaseAction = new QAction(tr("Decrease Grid Width"), this);
  m_gridWidthDecreaseAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Minus));
  m_gridWidthDecreaseAction->setShortcutContext(Qt::ApplicationShortcut);
  m_ctx.mainWindow->addAction(m_gridWidthDecreaseAction);
  connect(m_gridWidthDecreaseAction, &QAction::triggered, this, [this]() {
    if (m_ctx.onAdjustGridWidth) {
      m_ctx.onAdjustGridWidth(-1);
    }
  });
}

void MenuController::setupHamburgerMenu() {
  if (!m_ctx.ui || !m_ctx.ui->hamburgerMenuButton || !m_ctx.mainWindow) return;

  // Build a popup that mirrors the menu bar by re-using each top-level QMenu's
  // menuAction(). The QMenu objects stay shared between the menu bar and this
  // popup, so any later additions (like setupFullscreenAction inserting itself
  // into menuView) appear in both places automatically.
  auto *popup = new QMenu(m_ctx.mainWindow);
  if (m_ctx.ui->menuFile) popup->addAction(m_ctx.ui->menuFile->menuAction());
  if (m_ctx.ui->menuView) popup->addAction(m_ctx.ui->menuView->menuAction());
  if (m_ctx.ui->menuSort) popup->addAction(m_ctx.ui->menuSort->menuAction());
  if (m_ctx.ui->menuSettings) popup->addAction(m_ctx.ui->menuSettings->menuAction());
  if (m_ctx.ui->menuHelp) popup->addAction(m_ctx.ui->menuHelp->menuAction());

  // Replace the legacy "≡" glyph with the kde-breeze application-menu icon so
  // the hamburger renders correctly even on themes that don't size the
  // unicode glyph well, and so the button no longer gets visually clipped at
  // the .ui's tight 30-px width.
  m_ctx.ui->hamburgerMenuButton->setMenu(popup);
  m_ctx.ui->hamburgerMenuButton->setIcon(
      UIConstants::Icons::fromTheme({UIConstants::Icons::MENU, "open-menu-symbolic"}));
  m_ctx.ui->hamburgerMenuButton->setIconSize(QSize(18, 18));

  // Qt's auto-sizing for a popup containing only submenu actions
  // underestimates the column reserved for the ">" indicator on some styles,
  // leaving the arrow crowding the label text. Reserve room for the longest
  // label plus padding for the indicator and side margins.
  const QFontMetrics fm(popup->fontMetrics());
  int labelWidth = 0;
  const auto popupActions = popup->actions();
  for (const QAction *a : popupActions) {
    labelWidth = std::max(labelWidth, fm.horizontalAdvance(a->text()));
  }
  popup->setMinimumWidth(labelWidth + 80);

  syncHamburgerVisibility();
}

void MenuController::syncHamburgerVisibility() {
  if (!m_ctx.ui || !m_ctx.ui->hamburgerMenuButton || !m_ctx.mainWindow) return;
  // QMenuBar::isVisible() is false until the window has actually been shown,
  // so reading it during early setup falsely reports "menu bar hidden" and
  // the hamburger button gets stuck visible even though the menu bar is on.
  // Drive visibility off the user-intent action instead, falling back to the
  // menu bar's hidden state for runtime calls (fullscreen toggle).
  bool showHamburger = false;
  if (m_ctx.ui->actionShowMenuBar) {
    showHamburger = !m_ctx.ui->actionShowMenuBar->isChecked();
  } else if (QMenuBar *bar = m_ctx.mainWindow->menuBar()) {
    showHamburger = bar->isHidden();
  }
  m_ctx.ui->hamburgerMenuButton->setVisible(showHamburger);
}
