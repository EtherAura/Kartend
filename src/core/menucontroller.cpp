// Handles menu bar setup and action connections, extracted from MainWindow.
#include "menucontroller.h"
#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "detailspanemanager.h"
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

bool MenuController::connectVisibilityToggle(QAction *action, std::function<void(bool)> applyVisual,
                                             bool GeneralSettings::*field) {
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
        settings->*field = checked;
        if (m_ctx.getSettingsManager) {
          if (auto *mgr = m_ctx.getSettingsManager()) {
            mgr->saveGeneralSettings(*settings);
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
      &GeneralSettings::showMenuBar);
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
      &GeneralSettings::showToolbar);
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
          settings->sortMode = mode;
          if (m_ctx.getSettingsManager) {
            if (auto *mgr = m_ctx.getSettingsManager()) {
              mgr->saveGeneralSettings(*settings);
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
                  settings->excludeSubfoldersFromSort = checked;
                  if (m_ctx.getSettingsManager) {
                    if (auto *mgr = m_ctx.getSettingsManager()) {
                      mgr->saveGeneralSettings(*settings);
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
    if (settings->sortMode != entry.mode) continue;
    if (QAction *action = m_ctx.ui->*entry.action) {
      action->setChecked(true);
    }
    break;
  }
  if (m_ctx.ui->actionSortSubfolders) {
    m_ctx.ui->actionSortSubfolders->setChecked(settings->excludeSubfoldersFromSort);
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
        settings->fullscreen = entering;
        if (m_ctx.getSettingsManager) {
          if (auto *mgr = m_ctx.getSettingsManager()) {
            mgr->saveGeneralSettings(*settings);
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
    m_ctx.ui->actionShowMenuBar->setChecked(settings->showMenuBar);
    m_ctx.ui->menubar->setVisible(settings->showMenuBar);
  }

  // Toolbar
  if (m_ctx.ui && m_ctx.ui->actionShowToolbar && m_ctx.ui->itemsTopBar) {
    m_ctx.ui->actionShowToolbar->setChecked(settings->showToolbar);
    m_ctx.ui->itemsTopBar->setVisible(settings->showToolbar);
  }

  // Fullscreen: called from setupMenuBar() during MainWindow construction —
  // before main.cpp's window.show(). showFullScreen() makes the window visible
  // in fullscreen state; the subsequent show() leaves the state intact. We
  // also hide the menu bar here to mirror the F11 lambda's coupling so the
  // restored chrome matches what the user saw when they last enabled it.
  if (m_fullscreenAction) {
    if (settings->fullscreen) {
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
        const bool runtimeOn = settings && settings->runtimeDetectionEnabled;
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

  // grid-width chord moved to Ctrl+Shift+/- to free Ctrl+/-
  // for text zoom (see MainWindow::setupTextZoomShortcuts). The two
  // ApplicationShortcuts conflicted, leaving Ctrl+- ambiguous so neither
  // action fired. Bind both Plus and Equal for the increase action so US/EU
  // layouts hit the same chord (mirrors the text-zoom binding).
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

// pick one media file at random from the active view and launch
// it. Uses ScrollManager::filePathForVisualIndex() — the same path Enter /
// double-click resolve through — so the result is the absolute, resolved
// path that DatabaseManager::getCollectionIndexForFile() can key off. The
// raw ScrollDataStore::filePaths() list won't work here: those are the
// relative entries from the items table, while the file→collection map is
// indexed by the resolved absolute paths.
void MenuController::setupActionOpenRandomItem() {
  if (!m_ctx.ui || !m_ctx.mainWindow || !m_ctx.ui->actionOpenRandomItem) return;

  m_ctx.ui->actionOpenRandomItem->setShortcutContext(Qt::ApplicationShortcut);
  m_ctx.mainWindow->addAction(m_ctx.ui->actionOpenRandomItem);

  connect(m_ctx.ui->actionOpenRandomItem, &QAction::triggered, this, [this]() {
    ScrollManager *scroll = m_ctx.getScrollManager ? m_ctx.getScrollManager() : nullptr;
    if (!scroll) return;

    const int total = scroll->getTotalItems();
    if (total <= 0) return;

    const int viewingIndex =
        m_ctx.getCurrentCollectionIndex ? m_ctx.getCurrentCollectionIndex() : -1;
    if (viewingIndex < 0) return;

    // Visual indices interleave subcollections + virtual folders + media
    // when unified sort is on, so a single random draw can land on a non-
    // launchable entry. Retry up to a sensible bound; if every draw misses
    // (collection has no media at all), bail silently.
    QString path;
    for (int attempt = 0; attempt < 32 && path.isEmpty(); ++attempt) {
      const int pick = QRandomGenerator::global()->bounded(total);
      path = scroll->filePathForVisualIndex(pick);
    }
    if (path.isEmpty()) return;

    // Resolve the file's *source* collection — same dance Enter /
    // double-click / context-menu launches do. Without this, a random pick
    // from a synthetic collection (playlist, aggregator) hands its
    // empty-launcher CollectionConfig to LaunchManager and trips "No
    // launcher configured".
    IDatabaseManager *db = m_ctx.getDatabaseManager ? m_ctx.getDatabaseManager() : nullptr;
    const int sourceIndex = db ? db->getCollectionIndexForFile(path) : -1;
    const int ownerIndex = (sourceIndex >= 0) ? sourceIndex : viewingIndex;

    if (m_ctx.onLaunchItem) {
      m_ctx.onLaunchItem(path, ownerIndex);
    }
  });
}

// File menu entry for importing a.kart package.
void MenuController::setupActionImportKart() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;
  m_importKartAction = new QAction(tr("Import Kart..."), this);
  m_ctx.mainWindow->addAction(m_importKartAction);
  if (m_ctx.ui->menuFile) {
    m_ctx.ui->menuFile->addSeparator();
    m_ctx.ui->menuFile->addAction(m_importKartAction);
  }
  connect(m_importKartAction, &QAction::triggered, this, [this]() {
    if (m_ctx.onImportKart) m_ctx.onImportKart();
  });
}

// File menu entry for exporting the active collection as a.kart.
void MenuController::setupActionExportKart() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;
  m_exportKartAction = new QAction(tr("Export Collection as Kart..."), this);
  m_ctx.mainWindow->addAction(m_exportKartAction);
  if (m_ctx.ui->menuFile) {
    m_ctx.ui->menuFile->addAction(m_exportKartAction);
  }
  connect(m_exportKartAction, &QAction::triggered, this, [this]() {
    if (m_ctx.onExportKart) m_ctx.onExportKart();
  });
}

// File menu entry for importing a shareable theme preset (.kartend-theme.json).
// Distinct from Kart import — themes carry only the visual settings, not the
// collection's media paths / launcher / scraper config, so they can be shared
// across collections with completely different content.
void MenuController::setupActionImportTheme() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;
  m_importThemeAction = new QAction(tr("Import Theme..."), this);
  m_ctx.mainWindow->addAction(m_importThemeAction);
  if (m_ctx.ui->menuFile) {
    m_ctx.ui->menuFile->addSeparator();
    m_ctx.ui->menuFile->addAction(m_importThemeAction);
  }
  connect(m_importThemeAction, &QAction::triggered, this, [this]() {
    if (m_ctx.onImportTheme) m_ctx.onImportTheme();
  });
}

void MenuController::setupActionExportTheme() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;
  m_exportThemeAction = new QAction(tr("Export Current Theme..."), this);
  m_ctx.mainWindow->addAction(m_exportThemeAction);
  if (m_ctx.ui->menuFile) {
    m_ctx.ui->menuFile->addAction(m_exportThemeAction);
  }
  connect(m_exportThemeAction, &QAction::triggered, this, [this]() {
    if (m_ctx.onExportTheme) m_ctx.onExportTheme();
  });
}

void MenuController::setupActionLayoutProfiles() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;
  m_layoutProfilesAction = new QAction(tr("Layout Profiles..."), this);
  m_ctx.mainWindow->addAction(m_layoutProfilesAction);
  if (m_ctx.ui->menuFile) {
    m_ctx.ui->menuFile->addAction(m_layoutProfilesAction);
  }
  connect(m_layoutProfilesAction, &QAction::triggered, this, [this]() {
    if (m_ctx.onManageLayoutProfiles) m_ctx.onManageLayoutProfiles();
  });
}

void MenuController::setupActionCollectionHealth() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;
  m_collectionHealthAction = new QAction(tr("Collection Health..."), this);
  m_ctx.mainWindow->addAction(m_collectionHealthAction);
  if (m_ctx.ui->menuFile) {
    m_ctx.ui->menuFile->addAction(m_collectionHealthAction);
  }
  connect(m_collectionHealthAction, &QAction::triggered, this, [this]() {
    if (m_ctx.onShowCollectionHealth) m_ctx.onShowCollectionHealth();
  });
}

void MenuController::setupActionVariantGrouping() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;
  m_variantGroupingAction = new QAction(tr("Duplicates and variants..."), this);
  m_ctx.mainWindow->addAction(m_variantGroupingAction);
  if (m_ctx.ui->menuFile) {
    m_ctx.ui->menuFile->addAction(m_variantGroupingAction);
  }
  connect(m_variantGroupingAction, &QAction::triggered, this, [this]() {
    if (m_ctx.onShowVariantGrouping) m_ctx.onShowVariantGrouping();
  });
}

void MenuController::setupActionBulkEdit() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;
  m_bulkEditAction = new QAction(tr("Bulk Edit Items..."), this);
  m_ctx.mainWindow->addAction(m_bulkEditAction);
  if (m_ctx.ui->menuFile) {
    m_ctx.ui->menuFile->addAction(m_bulkEditAction);
  }
  connect(m_bulkEditAction, &QAction::triggered, this, [this]() {
    if (m_ctx.onBulkEdit) m_ctx.onBulkEdit();
  });
}

void MenuController::setupActionReviewMissingMetadata() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;
  m_reviewMissingMetadataAction = new QAction(tr("Review Missing Metadata..."), this);
  m_ctx.mainWindow->addAction(m_reviewMissingMetadataAction);
  if (m_ctx.ui->menuFile) {
    m_ctx.ui->menuFile->addAction(m_reviewMissingMetadataAction);
  }
  connect(m_reviewMissingMetadataAction, &QAction::triggered, this, [this]() {
    if (m_ctx.onReviewMissingMetadata) m_ctx.onReviewMissingMetadata();
  });
}

void MenuController::setupActionArtworkWizard() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;
  m_artworkWizardAction = new QAction(tr("Assign Missing Artwork..."), this);
  m_ctx.mainWindow->addAction(m_artworkWizardAction);
  if (m_ctx.ui->menuFile) {
    m_ctx.ui->menuFile->addAction(m_artworkWizardAction);
  }
  connect(m_artworkWizardAction, &QAction::triggered, this, [this]() {
    if (m_ctx.onArtworkWizard) m_ctx.onArtworkWizard();
  });
}

void MenuController::setupActionBindingVisualizer() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;
  // Sits in the Help menu next to Usage Statistics / Shortcuts since
  // it's a read-only diagnostic surface for the user's input config.
  m_bindingVisualizerAction = new QAction(tr("Binding Visualizer..."), this);
  m_ctx.mainWindow->addAction(m_bindingVisualizerAction);
  if (m_ctx.ui->menuHelp) {
    m_ctx.ui->menuHelp->addAction(m_bindingVisualizerAction);
  }
  connect(m_bindingVisualizerAction, &QAction::triggered, this, [this]() {
    if (m_ctx.onShowBindings) m_ctx.onShowBindings();
  });
}

void MenuController::setupActionNewLibraryWizard() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;
  m_newLibraryWizardAction = new QAction(tr("New Library Wizard..."), this);
  m_ctx.mainWindow->addAction(m_newLibraryWizardAction);
  if (m_ctx.ui->menuFile) {
    m_ctx.ui->menuFile->addAction(m_newLibraryWizardAction);
  }
  connect(m_newLibraryWizardAction, &QAction::triggered, this, [this]() {
    if (m_ctx.onNewLibraryWizard) m_ctx.onNewLibraryWizard();
  });
}

void MenuController::setupActionPresentationProfiles() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;
  m_presentationProfilesAction = new QAction(tr("Presentation Profiles..."), this);
  m_ctx.mainWindow->addAction(m_presentationProfilesAction);
  if (m_ctx.ui->menuFile) {
    m_ctx.ui->menuFile->addAction(m_presentationProfilesAction);
  }
  connect(m_presentationProfilesAction, &QAction::triggered, this, [this]() {
    if (m_ctx.onPresentationProfiles) m_ctx.onPresentationProfiles();
  });
}

// Recent submenu populated from the launch_history table on
// QMenu::aboutToShow. Rebuilding lazily avoids stale entries after the user
// launches new items between menu opens, and keeps the cost off the boot
// path.
