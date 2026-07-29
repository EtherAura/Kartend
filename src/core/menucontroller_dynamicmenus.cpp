// Sibling TU of menucontroller.cpp: data-driven submenu lifecycle.
//
// Holds the setup + rebuild functions for menus whose entries come from
// runtime data (launch_history → Recent, usage stats → Most Launched) or
// are kept in sync with the active view configuration (layout actions
// reflecting the current ViewType, orientation actions reflecting the
// details-pane dock position). Each function is paired: setupX() wires
// the action group / aboutToShow connection once, rebuildX() / syncX()
// runs at every refresh.
//
// Extracted from menucontroller.cpp to keep that file focused on the
// static action setup (file/view/help/tools menus). The split is by
// "static at construction" vs "rebuilt on signal" — not by menu surface,
// because the action-group syncing for layout/orientation is tightly
// coupled to its action-creation companion.

#include "menucontroller.h"

#include "collection/collectionhierarchycache.h"
#include "detailspanemanager.h"
#include "errorpresentation.h"
#include "historystore.h"
#include "idatabasemanager.h"
#include "isettingsmanager.h"
#include "navigationmanager.h"
#include "scrolldatamanager.h"
#include "scrollmanager.h"
#include "ui_mainwindow.h"
#include "uiconstants/icons.h"

#include <QFileInfo>
#include <QFontMetrics>
#include <QMenu>
#include <QMenuBar>

namespace {
// Layout-action table: one entry per ViewType, mapping the menu's layout action
// to its view. The toolbar's parallel layout switch (ToolbarController) is
// already -Wswitch-enforced; the static_assert below ties this array to the
// ViewType enum so adding a ViewType forces a new row here too (Kartend-ox2go).
struct LayoutActionEntry {
  QAction *Ui_MainWindow::*action;
  ViewType viewType;
};

constexpr LayoutActionEntry kLayoutActions[] = {
    {&Ui_MainWindow::actionLayoutGrid, ViewType::Grid},
    {&Ui_MainWindow::actionLayoutList, ViewType::List},
    {&Ui_MainWindow::actionLayoutCoverFlow, ViewType::CoverFlow},
    {&Ui_MainWindow::actionLayoutHorizontal, ViewType::Horizontal},
};
static_assert(sizeof(kLayoutActions) / sizeof(kLayoutActions[0]) ==
                  static_cast<size_t>(ViewType::Count),
              "kLayoutActions must list every ViewType; add the new row when a "
              "ViewType is added.");
} // namespace

void MenuController::setupRecentMenu() {
  if (!m_ctx.ui || !m_ctx.ui->menuRecent) return;

  // Initial placeholder so the menu doesn't look empty before the first show.
  rebuildRecentMenu();

  connect(m_ctx.ui->menuRecent, &QMenu::aboutToShow, this, &MenuController::rebuildRecentMenu);
}

void MenuController::rebuildRecentMenu() {
  if (!m_ctx.ui || !m_ctx.ui->menuRecent) return;

  QMenu *menu = m_ctx.ui->menuRecent;
  menu->clear();

  IDatabaseManager *db = m_ctx.getDatabaseManager ? m_ctx.getDatabaseManager() : nullptr;
  if (!db) {
    QAction *empty = menu->addAction(tr("(no recent items)"));
    empty->setEnabled(false);
    return;
  }

  // source from items.last_played (UsageStatsStore) rather than
  // the history table — items.last_played is updated on every successful
  // launch unconditionally, while history rows are gated on historyEnabled.
  // A user who disabled the history log should still see their recent
  // launches here.
  constexpr int kMaxRecent = 10;
  const QList<UsageStatsStore::ItemUsageRow> rows = db->loadRecentlyPlayedItems(kMaxRecent);
  if (rows.isEmpty()) {
    QAction *empty = menu->addAction(tr("(no recent items)"));
    empty->setEnabled(false);
    return;
  }

  populateLaunchEntriesIntoMenu(menu, rows, db);
}

void MenuController::setupMostLaunchedMenu() {
  if (!m_ctx.ui || !m_ctx.ui->menuMostLaunched) return;
  rebuildMostLaunchedMenu();
  connect(m_ctx.ui->menuMostLaunched, &QMenu::aboutToShow, this,
          &MenuController::rebuildMostLaunchedMenu);
}

void MenuController::rebuildMostLaunchedMenu() {
  if (!m_ctx.ui || !m_ctx.ui->menuMostLaunched) return;

  QMenu *menu = m_ctx.ui->menuMostLaunched;
  menu->clear();

  IDatabaseManager *db = m_ctx.getDatabaseManager ? m_ctx.getDatabaseManager() : nullptr;
  if (!db) {
    QAction *empty = menu->addAction(tr("(no launched items)"));
    empty->setEnabled(false);
    return;
  }

  // top items by play_count. UsageStatsStore::TOP_PLAYED_SQL
  // already filters play_count > 0 so an unlaunched library shows the empty
  // placeholder rather than a list of zero-count rows.
  constexpr int kMaxTop = 10;
  const QList<UsageStatsStore::ItemUsageRow> rows = db->loadTopPlayedItems(kMaxTop);
  if (rows.isEmpty()) {
    QAction *empty = menu->addAction(tr("(no launched items)"));
    empty->setEnabled(false);
    return;
  }

  populateLaunchEntriesIntoMenu(menu, rows, db);
}

void MenuController::populateLaunchEntriesIntoMenu(QMenu *menu,
                                                   const QList<UsageStatsStore::ItemUsageRow> &rows,
                                                   IDatabaseManager *db) {
  if (!menu) return;

  const CollectionHierarchyCache *cache =
      m_ctx.getHierarchyCache ? m_ctx.getHierarchyCache() : nullptr;

  for (const auto &row : rows) {
    // Prefer the stored name; fall back to basename for legacy rows that
    // pre-date the denormalization.
    QString label = row.name.isEmpty() ? QFileInfo(row.path).fileName() : row.name;
    QAction *action = menu->addAction(label);
    action->setToolTip(row.path);

    // Resolve uuid → current collection index. Prefer path-based lookup so
    // moved/renamed collections still resolve; fall back to stored uuid via
    // the hierarchy cache. Disable the entry when the source collection is
    // gone rather than silently launching nothing.
    int collectionIndex = -1;
    if (db) {
      collectionIndex = db->getCollectionIndexForFile(row.path);
    }
    if (collectionIndex < 0 && cache) {
      collectionIndex = cache->uuidToCollectionIndex(row.collectionUuid);
    }
    if (collectionIndex < 0) {
      action->setEnabled(false);
      continue;
    }

    const QString path = row.path;
    connect(action, &QAction::triggered, this, [this, path, collectionIndex]() {
      if (m_ctx.onLaunchItem) {
        m_ctx.onLaunchItem(path, collectionIndex);
      }
    });
  }
}

// Layout submenu mirrors the toolbar view-mode buttons. Lives
// in a QActionGroup so the four entries are mutually exclusive; checked
// state is driven by syncLayoutActions() (called by MainWindow whenever the
// active view type changes).
void MenuController::setupLayoutActions() {
  if (!m_ctx.ui || !m_ctx.mainWindow) return;

  m_layoutActionGroup = new QActionGroup(this);
  m_layoutActionGroup->setExclusive(true);

  for (const auto &entry : kLayoutActions) {
    QAction *action = m_ctx.ui->*entry.action;
    if (!action) continue;
    m_layoutActionGroup->addAction(action);
    action->setShortcutContext(Qt::ApplicationShortcut);
    m_ctx.mainWindow->addAction(action);
    const ViewType viewType = entry.viewType;
    connect(action, &QAction::triggered, this, [this, viewType]() {
      if (m_ctx.onSetViewType) m_ctx.onSetViewType(viewType);
    });
  }

  // Initial check — caller (MainWindow::createMenuBar) wires the view-type
  // sync on collection switch + setViewType() so subsequent changes flow
  // through automatically.
  if (m_ctx.getCurrentViewType) {
    syncLayoutActions(m_ctx.getCurrentViewType());
  }
}

void MenuController::syncLayoutActions(ViewType viewType) {
  if (!m_ctx.ui) return;
  for (const auto &entry : kLayoutActions) {
    if (QAction *action = m_ctx.ui->*entry.action) {
      action->setChecked(viewType == entry.viewType);
    }
  }
}

void MenuController::setupActionDetailsPaneOrientation() {
  if (!m_ctx.ui || !m_ctx.mainWindow || !m_ctx.ui->menuView) return;

  m_orientationMenu = new QMenu(tr("Details Pane Orientation"), m_ctx.mainWindow);
  m_orientationActionGroup = new QActionGroup(this);
  m_orientationActionGroup->setExclusive(true);

  auto addOrientation = [this](const QString &label, DetailsPanePosition pos) {
    auto *action = new QAction(label, this);
    action->setCheckable(true);
    m_orientationActionGroup->addAction(action);
    m_orientationMenu->addAction(action);
    connect(action, &QAction::triggered, this, [this, pos]() {
      auto *collections = m_ctx.getCollections ? m_ctx.getCollections() : nullptr;
      const int idx = m_ctx.getCurrentCollectionIndex ? m_ctx.getCurrentCollectionIndex() : -1;
      if (!collections || idx < 0 || idx >= collections->size()) return;
      if ((*collections)[idx].sidebar.sidebarPosition == pos) return;
      (*collections)[idx].sidebar.sidebarPosition = pos;
      if (m_ctx.getSettingsManager) {
        if (auto *sm = m_ctx.getSettingsManager()) {
          ErrorPresentation::reportSaveResult(sm->saveCollections(*collections), "collections",
                                              true);
        }
      }
      // applySidebarStateForCollection re-runs applyAppearance + layout swap so
      // a Right→Top change reparents the pane into m_outerLayout immediately
      // (otherwise the user would have to toggle the pane to see the new edge).
      if (m_ctx.getDetailsPaneManager) {
        if (auto *sb = m_ctx.getDetailsPaneManager()) {
          sb->applySidebarStateForCollection(idx);
        }
      }
    });
    return action;
  };

  m_orientationActionRight = addOrientation(tr("Right"), DetailsPanePosition::Right);
  m_orientationActionLeft = addOrientation(tr("Left"), DetailsPanePosition::Left);
  m_orientationActionTop = addOrientation(tr("Top"), DetailsPanePosition::Top);
  m_orientationActionBottom = addOrientation(tr("Bottom"), DetailsPanePosition::Bottom);

  // Sits beside the Layout submenu, in the "how the view is arranged" group
  // below the visibility toggles. mainwindow.ui's View menu ends with
  // menuLayout, and setupMenuBar() calls this right after setupLayoutActions(),
  // so a plain append lands in the right slot (Kartend-7lsh1).
  m_ctx.ui->menuView->addMenu(m_orientationMenu);

  // Initial sync — picks up the active collection's persisted position so the
  // checkmark is correct on first menu open without waiting for a switch.
  if (m_ctx.getCollections && m_ctx.getCurrentCollectionIndex) {
    auto *collections = m_ctx.getCollections();
    const int idx = m_ctx.getCurrentCollectionIndex();
    if (collections && idx >= 0 && idx < collections->size()) {
      syncOrientationActions((*collections)[idx].sidebar.sidebarPosition);
    }
  }
}

void MenuController::syncOrientationActions(DetailsPanePosition position) {
  if (m_orientationActionRight) {
    m_orientationActionRight->setChecked(position == DetailsPanePosition::Right);
  }
  if (m_orientationActionLeft) {
    m_orientationActionLeft->setChecked(position == DetailsPanePosition::Left);
  }
  if (m_orientationActionTop) {
    m_orientationActionTop->setChecked(position == DetailsPanePosition::Top);
  }
  if (m_orientationActionBottom) {
    m_orientationActionBottom->setChecked(position == DetailsPanePosition::Bottom);
  }
}

QList<CommandPaletteDialog::Command> MenuController::buildPaletteCommands() {
  QList<CommandPaletteDialog::Command> commands;

  // Collection switch entries. Skip playlists / smart playlists from
  // the suggestions — they have their own access paths and the palette
  // would otherwise be dominated by reserved/auto-generated rows.
  QList<CollectionConfig> *collections = m_ctx.getCollections ? m_ctx.getCollections() : nullptr;
  if (collections) {
    for (int i = 0; i < collections->size(); ++i) {
      const CollectionConfig &cfg = collections->at(i);
      if (cfg.isPlaylist) continue;
      const int idx = i;
      commands.append({tr("Collection"), cfg.name, [this, idx]() {
                         auto *nav =
                             m_ctx.getNavigationManager ? m_ctx.getNavigationManager() : nullptr;
                         if (nav) {
                           nav->showCollectionItems(idx);
                         }
                       }});
    }
  }

  // View-mode toggles. Map the live ViewType enum to two simple verbs; the
  // toolbar already syncs visually once the new mode is set. One helper for
  // both entries — the flows differ only in the target ViewType + label.
  const auto makeViewModeCommand = [this](ViewType viewType, const QString &label) {
    return CommandPaletteDialog::Command{
        tr("View"), label, [this, viewType]() {
          syncLayoutActions(viewType);
          QList<CollectionConfig> *cols = m_ctx.getCollections ? m_ctx.getCollections() : nullptr;
          const int index =
              m_ctx.getCurrentCollectionIndex ? m_ctx.getCurrentCollectionIndex() : -1;
          if (!cols || index < 0 || index >= cols->size()) {
            return;
          }
          (*cols)[index].viewType = viewType;
          if (auto *settings = m_ctx.getSettingsManager ? m_ctx.getSettingsManager() : nullptr) {
            ErrorPresentation::reportSaveResult(settings->saveCollections(*cols), "collections",
                                                true);
          }
          if (auto *nav = m_ctx.getNavigationManager ? m_ctx.getNavigationManager() : nullptr) {
            nav->safeReloadCollection(index);
          }
        }};
  };
  commands.append(makeViewModeCommand(ViewType::Grid, tr("Switch to grid view")));
  commands.append(makeViewModeCommand(ViewType::List, tr("Switch to list view")));

  // Common tool entries already reachable via menus — surfaced here so
  // the palette is the single keyboard-driven entry point users learn.
  // Each routes through the same MenuControllerContext callback its menu
  // action uses, so the two surfaces can't drift.
  if (m_ctx.onOpenSettings) {
    commands.append({tr("Tools"), tr("Open settings"), m_ctx.onOpenSettings});
  }
  commands.append({tr("Tools"), tr("Rescan current collection"), [this]() {
                     const int index =
                         m_ctx.getCurrentCollectionIndex ? m_ctx.getCurrentCollectionIndex() : -1;
                     auto *nav =
                         m_ctx.getNavigationManager ? m_ctx.getNavigationManager() : nullptr;
                     if (index >= 0 && nav) {
                       nav->safeReloadCollection(index);
                     }
                   }});
  if (m_ctx.onShowCollectionHealth) {
    commands.append({tr("Tools"), tr("Show collection health…"), m_ctx.onShowCollectionHealth});
  }
  if (m_ctx.onBulkEdit) {
    commands.append({tr("Tools"), tr("Bulk edit items…"), m_ctx.onBulkEdit});
  }
  if (m_ctx.onManageLayoutProfiles) {
    commands.append({tr("Tools"), tr("Layout profiles…"), m_ctx.onManageLayoutProfiles});
  }

  return commands;
}
