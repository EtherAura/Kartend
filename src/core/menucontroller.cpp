// Handles menu bar setup and action connections, extracted from MainWindow.
#include "menucontroller.h"
#include "collectionutils.h"
#include "databasemanager.h"
#include "historystore.h"
#include "navigationmanager.h"
#include "scrolldatamanager.h"
#include "scrollmanager.h"
#include "settingsmanager.h"
#include "shortcutsdialog.h"
#include "sidebarmanager.h"
#include "statisticsdialog.h"
#include "ui_mainwindow.h"
#include "uiconstants.h"

#include <QApplication>
#include <QFileInfo>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QRandomGenerator>
#include <QToolButton>

MenuController::MenuController(QObject *parent) : QObject(parent) {}

MenuController::~MenuController() = default;

void MenuController::setContext(const MenuControllerContext &context) {
  m_ctx = context;
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
  setupGridWidthActions();
  setupActionOpenRandomItem();
  setupRecentMenu();
  setupLayoutActions();
  setupHamburgerMenu();
}

void MenuController::setupActionExit() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;

  if (m_ctx.ui->actionExit) {
    connect(m_ctx.ui->actionExit, &QAction::triggered, m_ctx.mainWindow, &QWidget::close);
    m_ctx.ui->actionExit->setShortcutContext(Qt::ApplicationShortcut);
    m_ctx.mainWindow->addAction(m_ctx.ui->actionExit);
  }
}

void MenuController::setupActionShowMenuBar() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;

  if (m_ctx.ui->actionShowMenuBar) {
    connect(m_ctx.ui->actionShowMenuBar, &QAction::triggered, [this](bool checked) {
      if (m_ctx.ui->menubar) {
        m_ctx.ui->menubar->setVisible(checked);
      }
      syncHamburgerVisibility();
    });
    m_ctx.ui->actionShowMenuBar->setShortcutContext(Qt::ApplicationShortcut);
    m_ctx.mainWindow->addAction(m_ctx.ui->actionShowMenuBar);
  }
}

void MenuController::setupActionShowToolbar() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;

  if (m_ctx.ui->actionShowToolbar) {
    connect(m_ctx.ui->actionShowToolbar, &QAction::triggered, [this](bool checked) {
      if (m_ctx.ui->itemsTopBar) {
        m_ctx.ui->itemsTopBar->setVisible(checked);
      }
    });
    m_ctx.ui->actionShowToolbar->setShortcutContext(Qt::ApplicationShortcut);
    m_ctx.mainWindow->addAction(m_ctx.ui->actionShowToolbar);
  }
}

void MenuController::setupActionShowSidebar() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;

  if (m_ctx.ui->actionShowSidebar) {
    connect(m_ctx.ui->actionShowSidebar, &QAction::triggered, [this]() {
      if (m_ctx.getSidebarManager) {
        if (auto *mgr = m_ctx.getSidebarManager()) {
          mgr->toggleSidebar();
        }
      }
    });
    m_ctx.ui->actionShowSidebar->setShortcutContext(Qt::ApplicationShortcut);
    m_ctx.mainWindow->addAction(m_ctx.ui->actionShowSidebar);
  }
}

void MenuController::setupActionSettings() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;

  if (m_ctx.ui->actionSettings) {
    connect(m_ctx.ui->actionSettings, &QAction::triggered, [this]() {
      if (m_ctx.onOpenSettings) {
        m_ctx.onOpenSettings();
      }
    });
    m_ctx.ui->actionSettings->setShortcutContext(Qt::ApplicationShortcut);
    m_ctx.mainWindow->addAction(m_ctx.ui->actionSettings);
  }
}

void MenuController::setupActionAbout() {
  if (!m_ctx.ui) return;

  if (m_ctx.ui->actionAbout) {
    connect(m_ctx.ui->actionAbout, &QAction::triggered, [this]() {
      if (m_ctx.onShowAbout) {
        m_ctx.onShowAbout();
      }
    });
  }
}

void MenuController::setupActionAboutQt() {
  if (!m_ctx.ui) return;

  if (m_ctx.ui->actionAboutQt) {
    connect(m_ctx.ui->actionAboutQt, &QAction::triggered, qApp, &QApplication::aboutQt);
  }
}

void MenuController::setupActionRefresh() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;

  // Hard refresh (Ctrl+F5) - rescan the database
  if (m_ctx.ui->actionRefresh) {
    connect(m_ctx.ui->actionRefresh, &QAction::triggered, [this]() {
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
    connect(m_ctx.ui->actionSoftRefresh, &QAction::triggered, [this]() {
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

  // Create action group for mutually exclusive sort options
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

  if (m_ctx.ui->actionSortNameAsc) {
    m_sortActionGroup->addAction(m_ctx.ui->actionSortNameAsc);
    connect(m_ctx.ui->actionSortNameAsc, &QAction::triggered, [this, reloadIfNeeded]() {
      if (m_ctx.getGeneralSettings) {
        if (auto *settings = m_ctx.getGeneralSettings()) {
          settings->sortMode = SortMode::NameAscending;
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

  if (m_ctx.ui->actionSortNameDesc) {
    m_sortActionGroup->addAction(m_ctx.ui->actionSortNameDesc);
    connect(m_ctx.ui->actionSortNameDesc, &QAction::triggered, [this, reloadIfNeeded]() {
      if (m_ctx.getGeneralSettings) {
        if (auto *settings = m_ctx.getGeneralSettings()) {
          settings->sortMode = SortMode::NameDescending;
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

  if (m_ctx.ui->actionSortDateDesc) {
    m_sortActionGroup->addAction(m_ctx.ui->actionSortDateDesc);
    connect(m_ctx.ui->actionSortDateDesc, &QAction::triggered, [this, reloadIfNeeded]() {
      if (m_ctx.getGeneralSettings) {
        if (auto *settings = m_ctx.getGeneralSettings()) {
          settings->sortMode = SortMode::DateDescending;
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

  if (m_ctx.ui->actionSortDateAsc) {
    m_sortActionGroup->addAction(m_ctx.ui->actionSortDateAsc);
    connect(m_ctx.ui->actionSortDateAsc, &QAction::triggered, [this, reloadIfNeeded]() {
      if (m_ctx.getGeneralSettings) {
        if (auto *settings = m_ctx.getGeneralSettings()) {
          settings->sortMode = SortMode::DateAscending;
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

  if (m_ctx.ui->actionSortSizeDesc) {
    m_sortActionGroup->addAction(m_ctx.ui->actionSortSizeDesc);
    connect(m_ctx.ui->actionSortSizeDesc, &QAction::triggered, [this, reloadIfNeeded]() {
      if (m_ctx.getGeneralSettings) {
        if (auto *settings = m_ctx.getGeneralSettings()) {
          settings->sortMode = SortMode::SizeDescending;
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

  if (m_ctx.ui->actionSortSizeAsc) {
    m_sortActionGroup->addAction(m_ctx.ui->actionSortSizeAsc);
    connect(m_ctx.ui->actionSortSizeAsc, &QAction::triggered, [this, reloadIfNeeded]() {
      if (m_ctx.getGeneralSettings) {
        if (auto *settings = m_ctx.getGeneralSettings()) {
          settings->sortMode = SortMode::SizeAscending;
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

  if (m_ctx.ui->actionSortRandom) {
    m_sortActionGroup->addAction(m_ctx.ui->actionSortRandom);
    connect(m_ctx.ui->actionSortRandom, &QAction::triggered, [this, reloadIfNeeded]() {
      if (m_ctx.getGeneralSettings) {
        if (auto *settings = m_ctx.getGeneralSettings()) {
          settings->sortMode = SortMode::Random;
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

  // Exclude subfolders option (not part of action group - it's a toggle)
  if (m_ctx.ui->actionSortSubfolders) {
    connect(m_ctx.ui->actionSortSubfolders, &QAction::triggered,
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

  // Sync initial checked states
  syncSortActions();
}

void MenuController::syncSortActions() {
  if (!m_ctx.ui || !m_ctx.getGeneralSettings) return;

  auto *settings = m_ctx.getGeneralSettings();
  if (!settings) return;

  switch (settings->sortMode) {
  case SortMode::NameAscending:
    if (m_ctx.ui->actionSortNameAsc) m_ctx.ui->actionSortNameAsc->setChecked(true);
    break;
  case SortMode::NameDescending:
    if (m_ctx.ui->actionSortNameDesc) m_ctx.ui->actionSortNameDesc->setChecked(true);
    break;
  case SortMode::ArtworkFirst:
  case SortMode::ArtworkLast:
  case SortMode::CollectionAscending:
  case SortMode::CollectionDescending:
    // These sort modes are list-view only, no menu action to check
    break;
  case SortMode::DateDescending:
    if (m_ctx.ui->actionSortDateDesc) m_ctx.ui->actionSortDateDesc->setChecked(true);
    break;
  case SortMode::DateAscending:
    if (m_ctx.ui->actionSortDateAsc) m_ctx.ui->actionSortDateAsc->setChecked(true);
    break;
  case SortMode::SizeDescending:
    if (m_ctx.ui->actionSortSizeDesc) m_ctx.ui->actionSortSizeDesc->setChecked(true);
    break;
  case SortMode::SizeAscending:
    if (m_ctx.ui->actionSortSizeAsc) m_ctx.ui->actionSortSizeAsc->setChecked(true);
    break;
  case SortMode::Random:
    if (m_ctx.ui->actionSortRandom) m_ctx.ui->actionSortRandom->setChecked(true);
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

  connect(m_fullscreenAction, &QAction::triggered, [this]() {
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
  });
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
  if (!m_ctx.mainWindow) return;

  m_shortcutsAction = new QAction(tr("Keyboard Shortcuts"), this);
  m_shortcutsAction->setShortcut(QKeySequence(Qt::Key_F1));
  m_shortcutsAction->setShortcutContext(Qt::ApplicationShortcut);
  m_ctx.mainWindow->addAction(m_shortcutsAction);

  // Add to Help menu if it exists
  if (m_ctx.ui && m_ctx.ui->menuHelp) {
    m_ctx.ui->menuHelp->addAction(m_shortcutsAction);
  }

  connect(m_shortcutsAction, &QAction::triggered, [this]() {
    ShortcutsDialog dialog(m_ctx.mainWindow);
    dialog.exec();
  });
}

void MenuController::setupStatisticsAction() {
  if (!m_ctx.mainWindow) return;

  // Programmatic action (no .ui entry yet) so the menu wiring stays self-
  // contained alongside the dialog. Lives in the Help menu next to Shortcuts.
  m_statisticsAction = new QAction(tr("Usage Statistics…"), this);
  m_statisticsAction->setShortcutContext(Qt::ApplicationShortcut);
  m_ctx.mainWindow->addAction(m_statisticsAction);

  if (m_ctx.ui && m_ctx.ui->menuHelp) {
    m_ctx.ui->menuHelp->addAction(m_statisticsAction);
  }

  connect(m_statisticsAction, &QAction::triggered, [this]() {
    DatabaseManager *db = m_ctx.getDatabaseManager ? m_ctx.getDatabaseManager() : nullptr;
    QList<CollectionConfig> *collections = m_ctx.getCollections ? m_ctx.getCollections() : nullptr;
    GeneralSettings *settings = m_ctx.getGeneralSettings ? m_ctx.getGeneralSettings() : nullptr;
    SettingsManager *settingsMgr = m_ctx.getSettingsManager ? m_ctx.getSettingsManager() : nullptr;
    const bool runtimeOn = settings && settings->runtimeDetectionEnabled;
    StatisticsDialog dialog(db, collections, runtimeOn, settings, settingsMgr, m_ctx.mainWindow);
    dialog.exec();
  });
}

void MenuController::setupGridWidthActions() {
  if (!m_ctx.mainWindow) return;

  // Ctrl++ to increase grid width (more columns, smaller items)
  m_gridWidthIncreaseAction = new QAction(tr("Increase Grid Width"), this);
  m_gridWidthIncreaseAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Plus));
  m_gridWidthIncreaseAction->setShortcutContext(Qt::ApplicationShortcut);
  m_ctx.mainWindow->addAction(m_gridWidthIncreaseAction);
  connect(m_gridWidthIncreaseAction, &QAction::triggered, [this]() {
    if (m_ctx.onAdjustGridWidth) {
      m_ctx.onAdjustGridWidth(1);
    }
  });

  // Ctrl+- to decrease grid width (fewer columns, larger items)
  m_gridWidthDecreaseAction = new QAction(tr("Decrease Grid Width"), this);
  m_gridWidthDecreaseAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus));
  m_gridWidthDecreaseAction->setShortcutContext(Qt::ApplicationShortcut);
  m_ctx.mainWindow->addAction(m_gridWidthDecreaseAction);
  connect(m_gridWidthDecreaseAction, &QAction::triggered, [this]() {
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

  m_ctx.ui->hamburgerMenuButton->setMenu(popup);

  syncHamburgerVisibility();
}

void MenuController::syncHamburgerVisibility() {
  if (!m_ctx.ui || !m_ctx.ui->hamburgerMenuButton || !m_ctx.mainWindow) return;
  QMenuBar *bar = m_ctx.mainWindow->menuBar();
  const bool menuBarHidden = bar && !bar->isVisible();
  m_ctx.ui->hamburgerMenuButton->setVisible(menuBarHidden);
}

// Kartend-iue: pick one media file at random from the active view and launch
// it. Uses ScrollManager::filePathForVisualIndex() — the same path Enter /
// double-click resolve through — so the result is the absolute, resolved
// path that DatabaseManager::getCollectionIndexForFile() can key off. The
// raw ScrollDataManager::filePaths() list won't work here: those are the
// relative entries from the items table, while the file→collection map is
// indexed by the resolved absolute paths.
void MenuController::setupActionOpenRandomItem() {
  if (!m_ctx.ui || !m_ctx.mainWindow || !m_ctx.ui->actionOpenRandomItem) return;

  m_ctx.ui->actionOpenRandomItem->setShortcutContext(Qt::ApplicationShortcut);
  m_ctx.mainWindow->addAction(m_ctx.ui->actionOpenRandomItem);

  connect(m_ctx.ui->actionOpenRandomItem, &QAction::triggered, [this]() {
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
    DatabaseManager *db = m_ctx.getDatabaseManager ? m_ctx.getDatabaseManager() : nullptr;
    const int sourceIndex = db ? db->getCollectionIndexForFile(path) : -1;
    const int ownerIndex = (sourceIndex >= 0) ? sourceIndex : viewingIndex;

    if (m_ctx.onLaunchItem) {
      m_ctx.onLaunchItem(path, ownerIndex);
    }
  });
}

// Kartend-iue: Recent submenu populated from the launch_history table on
// QMenu::aboutToShow. Rebuilding lazily avoids stale entries after the user
// launches new items between menu opens, and keeps the cost off the boot
// path.
void MenuController::setupRecentMenu() {
  if (!m_ctx.ui || !m_ctx.ui->menuRecent) return;

  // Initial placeholder so the menu doesn't look empty before the first show.
  rebuildRecentMenu();

  connect(m_ctx.ui->menuRecent, &QMenu::aboutToShow, this,
          &MenuController::rebuildRecentMenu);
}

void MenuController::rebuildRecentMenu() {
  if (!m_ctx.ui || !m_ctx.ui->menuRecent) return;

  QMenu *menu = m_ctx.ui->menuRecent;
  menu->clear();

  DatabaseManager *db = m_ctx.getDatabaseManager ? m_ctx.getDatabaseManager() : nullptr;
  if (!db) {
    QAction *empty = menu->addAction(tr("(no recent items)"));
    empty->setEnabled(false);
    return;
  }

  // Cap at 10 — long enough to be useful, short enough to stay scannable.
  // HistoryStore::loadRecent already orders by descending id (most recent
  // first) so the menu ordering is correct as-is.
  constexpr int kMaxRecent = 10;
  const QList<HistoryStore::HistoryEntry> entries = db->loadRecentHistory(kMaxRecent);
  if (entries.isEmpty()) {
    QAction *empty = menu->addAction(tr("(no recent items)"));
    empty->setEnabled(false);
    return;
  }

  const CollectionHierarchyCache *cache =
      m_ctx.getHierarchyCache ? m_ctx.getHierarchyCache() : nullptr;

  for (const auto &entry : entries) {
    // Prefer the denormalized name from history; fall back to basename so
    // even rows missing a name still render something readable.
    QString label = entry.name.isEmpty() ? QFileInfo(entry.path).fileName() : entry.name;
    QAction *action = menu->addAction(label);
    action->setToolTip(entry.path);

    // Resolve uuid → current collection index. Prefer the path-based
    // source-collection lookup (matches Enter / double-click / context-menu
    // resolution and steers around playlist/synthetic uuids), and fall back
    // to the stored uuid for legacy rows where the file→collection map is
    // empty. If neither resolves, the source collection is gone and we
    // disable the entry rather than launching into nothing.
    int collectionIndex = -1;
    if (db) {
      collectionIndex = db->getCollectionIndexForFile(entry.path);
    }
    if (collectionIndex < 0 && cache) {
      collectionIndex = cache->uuidToCollectionIndex(entry.collectionUuid);
    }
    if (collectionIndex < 0) {
      action->setEnabled(false);
      continue;
    }

    QString path = entry.path;
    connect(action, &QAction::triggered, this, [this, path, collectionIndex]() {
      if (m_ctx.onLaunchItem) {
        m_ctx.onLaunchItem(path, collectionIndex);
      }
    });
  }
}

// Kartend-iue: Layout submenu mirrors the toolbar view-mode buttons. Lives
// in a QActionGroup so the four entries are mutually exclusive; checked
// state is driven by syncLayoutActions() (called by MainWindow whenever the
// active view type changes).
void MenuController::setupLayoutActions() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;

  m_layoutActionGroup = new QActionGroup(this);
  m_layoutActionGroup->setExclusive(true);

  auto wire = [this](QAction *action, ViewType viewType) {
    if (!action) return;
    m_layoutActionGroup->addAction(action);
    action->setShortcutContext(Qt::ApplicationShortcut);
    m_ctx.mainWindow->addAction(action);
    connect(action, &QAction::triggered, [this, viewType]() {
      if (m_ctx.onSetViewType) {
        m_ctx.onSetViewType(viewType);
      }
    });
  };

  wire(m_ctx.ui->actionLayoutGrid, ViewType::Grid);
  wire(m_ctx.ui->actionLayoutList, ViewType::List);
  wire(m_ctx.ui->actionLayoutCoverFlow, ViewType::CoverFlow);
  wire(m_ctx.ui->actionLayoutHorizontal, ViewType::Horizontal);

  // Initial check — caller (MainWindow::createMenuBar) wires the view-type
  // sync on collection switch + setViewType() so subsequent changes flow
  // through automatically.
  if (m_ctx.getCurrentViewType) {
    syncLayoutActions(m_ctx.getCurrentViewType());
  }
}

void MenuController::syncLayoutActions(ViewType viewType) {
  if (!m_ctx.ui) return;
  if (m_ctx.ui->actionLayoutGrid) {
    m_ctx.ui->actionLayoutGrid->setChecked(viewType == ViewType::Grid);
  }
  if (m_ctx.ui->actionLayoutList) {
    m_ctx.ui->actionLayoutList->setChecked(viewType == ViewType::List);
  }
  if (m_ctx.ui->actionLayoutCoverFlow) {
    m_ctx.ui->actionLayoutCoverFlow->setChecked(viewType == ViewType::CoverFlow);
  }
  if (m_ctx.ui->actionLayoutHorizontal) {
    m_ctx.ui->actionLayoutHorizontal->setChecked(viewType == ViewType::Horizontal);
  }
}
