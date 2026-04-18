// Handles menu bar setup and action connections, extracted from MainWindow.
#include "menucontroller.h"
#include "collectionutils.h"
#include "navigationmanager.h"
#include "settingsmanager.h"
#include "shortcutsdialog.h"
#include "sidebarmanager.h"
#include "ui_mainwindow.h"
#include "uiconstants.h"

#include <QApplication>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>

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
  setupGridWidthActions();
}

void MenuController::setupActionExit() {
  if (!m_ctx.ui || !m_ctx.mainWindow)
    return;

  if (m_ctx.ui->actionExit) {
    connect(m_ctx.ui->actionExit, &QAction::triggered, m_ctx.mainWindow,
            &QWidget::close);
    m_ctx.ui->actionExit->setShortcutContext(Qt::ApplicationShortcut);
    m_ctx.mainWindow->addAction(m_ctx.ui->actionExit);
  }
}

void MenuController::setupActionShowMenuBar() {
  if (!m_ctx.ui || !m_ctx.mainWindow)
    return;

  if (m_ctx.ui->actionShowMenuBar) {
    connect(m_ctx.ui->actionShowMenuBar, &QAction::triggered,
            [this](bool checked) {
              if (m_ctx.ui->menubar) {
                m_ctx.ui->menubar->setVisible(checked);
              }
            });
    m_ctx.ui->actionShowMenuBar->setShortcutContext(Qt::ApplicationShortcut);
    m_ctx.mainWindow->addAction(m_ctx.ui->actionShowMenuBar);
  }
}

void MenuController::setupActionShowToolbar() {
  if (!m_ctx.ui || !m_ctx.mainWindow)
    return;

  if (m_ctx.ui->actionShowToolbar) {
    connect(m_ctx.ui->actionShowToolbar, &QAction::triggered,
            [this](bool checked) {
              if (m_ctx.ui->itemsTopBar) {
                m_ctx.ui->itemsTopBar->setVisible(checked);
              }
            });
    m_ctx.ui->actionShowToolbar->setShortcutContext(Qt::ApplicationShortcut);
    m_ctx.mainWindow->addAction(m_ctx.ui->actionShowToolbar);
  }
}

void MenuController::setupActionShowSidebar() {
  if (!m_ctx.ui || !m_ctx.mainWindow)
    return;

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
  if (!m_ctx.ui || !m_ctx.mainWindow)
    return;

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
  if (!m_ctx.ui)
    return;

  if (m_ctx.ui->actionAbout) {
    connect(m_ctx.ui->actionAbout, &QAction::triggered, [this]() {
      if (m_ctx.onShowAbout) {
        m_ctx.onShowAbout();
      }
    });
  }
}

void MenuController::setupActionAboutQt() {
  if (!m_ctx.ui)
    return;

  if (m_ctx.ui->actionAboutQt) {
    connect(m_ctx.ui->actionAboutQt, &QAction::triggered, qApp,
            &QApplication::aboutQt);
  }
}

void MenuController::setupActionRefresh() {
  if (!m_ctx.ui || !m_ctx.mainWindow)
    return;

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
  if (!m_ctx.ui)
    return;

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
    connect(m_ctx.ui->actionSortNameAsc, &QAction::triggered,
            [this, reloadIfNeeded]() {
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
    connect(m_ctx.ui->actionSortNameDesc, &QAction::triggered,
            [this, reloadIfNeeded]() {
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

  if (m_ctx.ui->actionSortRandom) {
    m_sortActionGroup->addAction(m_ctx.ui->actionSortRandom);
    connect(m_ctx.ui->actionSortRandom, &QAction::triggered,
            [this, reloadIfNeeded]() {
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
  if (!m_ctx.ui || !m_ctx.getGeneralSettings)
    return;

  auto *settings = m_ctx.getGeneralSettings();
  if (!settings)
    return;

  switch (settings->sortMode) {
  case SortMode::NameAscending:
    if (m_ctx.ui->actionSortNameAsc)
      m_ctx.ui->actionSortNameAsc->setChecked(true);
    break;
  case SortMode::NameDescending:
    if (m_ctx.ui->actionSortNameDesc)
      m_ctx.ui->actionSortNameDesc->setChecked(true);
    break;
  case SortMode::ArtworkFirst:
  case SortMode::ArtworkLast:
  case SortMode::CollectionAscending:
  case SortMode::CollectionDescending:
    // These sort modes are list-view only, no menu action to check
    break;
  case SortMode::Random:
    if (m_ctx.ui->actionSortRandom)
      m_ctx.ui->actionSortRandom->setChecked(true);
    break;
  }
  if (m_ctx.ui->actionSortSubfolders) {
    m_ctx.ui->actionSortSubfolders->setChecked(
        settings->excludeSubfoldersFromSort);
  }
}

void MenuController::setupFullscreenAction() {
  if (!m_ctx.mainWindow)
    return;

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
  });
}

void MenuController::insertFullscreenInViewMenu(QAction *fullscreenAction) {
  if (!m_ctx.ui || !m_ctx.ui->menuView)
    return;

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
  if (!m_ctx.mainWindow)
    return;

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

void MenuController::setupGridWidthActions() {
  if (!m_ctx.mainWindow)
    return;

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
  m_gridWidthDecreaseAction->setShortcut(
      QKeySequence(Qt::CTRL | Qt::Key_Minus));
  m_gridWidthDecreaseAction->setShortcutContext(Qt::ApplicationShortcut);
  m_ctx.mainWindow->addAction(m_gridWidthDecreaseAction);
  connect(m_gridWidthDecreaseAction, &QAction::triggered, [this]() {
    if (m_ctx.onAdjustGridWidth) {
      m_ctx.onAdjustGridWidth(-1);
    }
  });
}
