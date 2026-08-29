// Owns the items-page toolbar's stateful Qt widgets: the layout-picker
// QToolButton + four view QActions, the search-mode QAction inside the
// search QLineEdit, and the consolidated filter QToolButton + its type
// radio group / title-pattern toggle / pattern editor. Replaces a chunk of
// the previous mainwindow_toolbar.cpp + mainwindow_wiring.cpp partial-class
// TUs that managed the same widgets through MainWindow::* members.
#include "toolbarcontroller.h"
#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "collection/launcherconfig.h"
#include "collection/typehelpers.h"

#include "applicationmanager.h"
#include "errorpresentation.h"
#include "inavigationmanager.h"
#include "isettingsmanager.h"
#include "mainwindow.h"
#include "navigationmanager.h"
#include "settingsutils.h"
#include "uiconstants/icons.h"

#include <utility>

#include <QAction>
#include <QActionGroup>
#include <QDialog>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSize>
#include <QStringList>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

ToolbarController::ToolbarController(QObject *parent) : QObject(parent) {}

void ToolbarController::initialize(const Setup &setup) {
  m_mainWindow = setup.mainWindow;
  m_viewModeButton = setup.viewModeButton;
  m_filterButton = setup.filterButton;
  m_homeButton = setup.homeButton;
  m_collectionWarningBadge = setup.collectionWarningBadge;
  m_searchBar = setup.searchBar;
  m_onManageSearchPresets = setup.onManageSearchPresets;
}

ApplicationManager *ToolbarController::applicationManager() const {
  return m_mainWindow ? m_mainWindow->applicationManager() : nullptr;
}

ISettingsManager *ToolbarController::settingsMgr() const {
  auto *app = applicationManager();
  return app ? app->getSettingsManager() : nullptr;
}

INavigationManager *ToolbarController::navMgr() const {
  auto *app = applicationManager();
  return app ? app->getNavigationManager() : nullptr;
}

void ToolbarController::setupViewModeButton() {
  if (!m_viewModeButton) {
    return;
  }
  // keep the toolbar layout-picker visually in lockstep with the View →
  // Layout menu by mounting a popup of text-only entries (Grid / List /
  // Cover Flow / Horizontal). The four legacy individual icon buttons used
  // to do this — collapsed here into a single Breeze-icon button so the
  // toolbar doesn't carry one slot per layout.
  auto *menu = new QMenu(m_viewModeButton);
  auto *group = new QActionGroup(menu);
  group->setExclusive(true);

  MainWindow *mw = m_mainWindow;
  auto addEntry = [&](const QString &text, ViewType type) {
    QAction *action = menu->addAction(text);
    action->setCheckable(true);
    group->addAction(action);
    QObject::connect(action, &QAction::triggered, this, [mw, type]() {
      if (mw) {
        mw->setViewTypeFromToolbar(type);
      }
    });
    return action;
  };
  m_viewActionGrid = addEntry(tr("&Grid"), ViewType::Grid);
  m_viewActionList = addEntry(tr("&List"), ViewType::List);
  m_viewActionCoverFlow = addEntry(tr("&Cover Flow"), ViewType::CoverFlow);
  m_viewActionHorizontal = addEntry(tr("&Horizontal"), ViewType::Horizontal);

  // Kartend-lp7j9: re-sync the checked entry every time the popup opens.
  //
  // This function runs during MainWindow setup, which is BEFORE the deferred
  // startup timer picks a collection (setupInitialTimersWithCollections in
  // mainwindow_timers.cpp defers via singleShot(0) so Qt can finish layout).
  // So there is no view type to tick at construction time, and nothing ever
  // came back to tick one: syncViewModeButton is only reached from
  // setViewTypeFromToolbar (an explicit pick) and
  // updateWindowTitleForCollection (only called by the settings dialog).
  // The menu came up with all four entries unchecked, and the user's first
  // explicit pick was what first synced it — actively misleading on exactly
  // the question the menu is opened to answer.
  //
  // The menu bar's Layout submenu never had this bug because
  // MenuController::setupLayoutActions ends with its own initial
  // syncLayoutActions(); the toolbar popup simply had no equivalent.
  //
  // Syncing on aboutToShow rather than adding a one-shot initial sync: it
  // costs the same, cannot be defeated by init ordering, and additionally
  // repairs any later desync. It is also the pattern already used here for
  // the Recent menu (MenuController::setupRecentMenu).
  QObject::connect(menu, &QMenu::aboutToShow, this,
                   [this]() { syncViewModeButtonFromCurrentCollection(); });

  m_viewModeButton->setMenu(menu);
  m_viewModeButton->setIcon(
      UIConstants::Icons::fromTheme({UIConstants::Icons::VIEW_PICKER, "view-list-icons"}));
  m_viewModeButton->setIconSize(QSize(18, 18));
}

void ToolbarController::syncViewModeButtonFromCurrentCollection() {
  if (!m_mainWindow) {
    return;
  }
  const int idx = m_mainWindow->m_currentCollectionIndex;
  if (idx < 0 || idx >= m_mainWindow->m_collections.size()) {
    return; // root/home view — no per-collection layout to reflect
  }
  syncViewModeButton(m_mainWindow->m_collections[idx].viewType);
}

void ToolbarController::syncViewModeButton(ViewType viewType) {
  const auto setChecked = [](QAction *action, bool on) {
    if (action) {
      QSignalBlocker blocker(action);
      action->setChecked(on);
    }
  };
  setChecked(m_viewActionGrid, viewType == ViewType::Grid);
  setChecked(m_viewActionList, viewType == ViewType::List);
  setChecked(m_viewActionCoverFlow, viewType == ViewType::CoverFlow);
  setChecked(m_viewActionHorizontal, viewType == ViewType::Horizontal);
  if (m_viewModeButton) {
    QString tip;
    switch (viewType) {
    case ViewType::Grid:
      tip = tr("Layout: Grid (click to change)");
      break;
    case ViewType::List:
      tip = tr("Layout: List (click to change)");
      break;
    case ViewType::CoverFlow:
      tip = tr("Layout: Cover Flow (click to change)");
      break;
    case ViewType::Horizontal:
      tip = tr("Layout: Horizontal (click to change)");
      break;
    case ViewType::Count:
      break; // sentinel, never a runtime value
    }
    m_viewModeButton->setToolTip(tip);
  }
}

void ToolbarController::setupSearchModeAction() {
  if (!m_searchBar) {
    return;
  }
  // The search-mode toggle lives inside the QLineEdit (LeadingPosition)
  // rather than as a sibling QPushButton — keeps the toolbar tighter and
  // gives the search field a familiar magnifier-glass affordance. The
  // triggered() signal is wired later (by MainWindow's wiring pass) once
  // InteractionManager is alive.
  m_searchModeAction = m_searchBar->addAction(
      UIConstants::Icons::fromTheme(UIConstants::Icons::SEARCH), QLineEdit::LeadingPosition);
  if (m_searchModeAction) {
    m_searchModeAction->setToolTip(tr("Toggle search scope"));
    m_searchModeAction->setText(tr("Toggle search scope"));
  }
}

void ToolbarController::applyToolbarCustomization(const GeneralSettings &gs) {
  // Legacy per-view-button visibility flags and the hide-subcollections /
  // search-mode flags are kept in GeneralSettings for backward compat, but
  // the underlying buttons have been removed (single viewModeButton, in-
  // field search action, single filterButton). Only the consolidated filter
  // button and the search bar remain user-toggleable from this codepath; the
  // filter button stays on when *either* legacy flag (type or title) is on
  // so existing settings don't accidentally hide it.
  if (m_filterButton) {
    m_filterButton->setVisible(gs.toolbar.toolbarShowTypeFilter ||
                               gs.toolbar.toolbarShowTitleFilter);
  }
  if (m_searchBar) {
    m_searchBar->setVisible(gs.toolbar.toolbarShowSearchBar);
  }
  refreshHomeButton(gs);
}

void ToolbarController::setupHomeButton() {
  if (!m_homeButton || !m_mainWindow) {
    return;
  }
  MainWindow *mw = m_mainWindow;
  QObject::connect(m_homeButton, &QToolButton::clicked, this, [mw]() {
    if (mw && mw->getApplicationManager()->getNavigationManager()) {
      mw->getApplicationManager()->getNavigationManager()->loadRootView();
    }
  });
}

void ToolbarController::refreshHomeButton(const GeneralSettings &gs) {
  if (!m_homeButton) {
    return;
  }
  m_homeButton->setVisible(gs.startup.useHomeView);
  QIcon icon;
  const QString customPath = gs.startup.homeViewIcon.trimmed();
  if (!customPath.isEmpty()) {
    icon = QIcon(customPath);
  }
  if (icon.isNull()) {
    icon = UIConstants::Icons::fromTheme(
        {"user-home-symbolic", "go-home", "user-home", "go-home-symbolic"});
  }
  m_homeButton->setIcon(icon);
  m_homeButton->setIconSize(QSize(18, 18));
  const QString label = gs.startup.homeViewLabel.trimmed();
  m_homeButton->setToolTip(label.isEmpty() ? tr("Home") : label);
}

void ToolbarController::setupCollectionWarningBadge() {
  if (!m_collectionWarningBadge || !m_mainWindow) {
    return;
  }
  // SP_MessageBoxWarning is the platform-themed triangle/exclamation glyph —
  // matches every other "something is off" surface (sidebar, error dialogs)
  // so users don't have to learn a new icon dialect for the toolbar badge.
  const QIcon warningIcon =
      m_collectionWarningBadge->style()->standardIcon(QStyle::SP_MessageBoxWarning);
  m_collectionWarningBadge->setIcon(warningIcon);
  m_collectionWarningBadge->setIconSize(QSize(18, 18));
  MainWindow *mw = m_mainWindow;
  // Click opens Settings so the user has a one-click path from "I see a
  // warning" to "I can fix the path that triggered it". The Launchers
  // page hint lands the dialog directly on the global launcher list —
  // the only surface that owns the paths the badge actually flagged —
  // so the user doesn't have to walk the navigation rail first.
  QObject::connect(m_collectionWarningBadge, &QToolButton::clicked, this, [mw]() {
    if (mw) {
      mw->openSettingsDialog(SettingsPage::Launchers);
    }
  });
}

void ToolbarController::refreshCollectionWarningBadge() {
  if (!m_collectionWarningBadge || !m_mainWindow) {
    return;
  }
  QStringList issues;
  const int idx = m_mainWindow->m_currentCollectionIndex;
  if (idx >= 0 && idx < m_mainWindow->m_collections.size()) {
    // Launch-time overload: judge the EFFECTIVE config (preset-resolved +
    // %collection%-expanded) so the badge reaches the same verdict as the
    // pre-launch gate — the raw fields showed a false badge for a
    // placeholder-routed path that launches fine, and no badge for a broken
    // preset-backed entry.
    const CollectionConfig &collection = m_mainWindow->m_collections[idx];
    issues = LauncherUtils::launcherPathIssues(
        collection.launcher, m_mainWindow->m_generalSettings.launchers.launcherPresets,
        collection.name);
  }
  const bool hasIssues = !issues.isEmpty();
  m_collectionWarningBadge->setVisible(hasIssues);
  if (hasIssues) {
    // Header line names the kind of problem so the tooltip reads as a
    // diagnostic instead of a bare path dump. Detail lines come straight
    // from LauncherUtils::launcherPathIssues — identical wording to the
    // sidebar's ⚠ Launcher path rows.
    QString tooltip = tr("Launcher paths are unresolvable on this host:");
    for (const QString &issue : issues) {
      tooltip += QLatin1Char('\n') + issue;
    }
    tooltip += QLatin1Char('\n');
    tooltip += tr("Click to open Settings → Launchers.");
    m_collectionWarningBadge->setToolTip(tooltip);
  } else {
    m_collectionWarningBadge->setToolTip(QString());
  }
}

void ToolbarController::connectFilterToolbar() {
  // Single setup pass: refresh wires the QMenu::triggered handler the first
  // time it runs, and every subsequent call just rebuilds the action list
  // (the handler stays on the menu).
  refreshFilterToolbar();
}

void ToolbarController::refreshFilterToolbar() {
  if (!m_filterButton || !m_mainWindow) {
    return;
  }
  // Rebuild the popup from scratch: the type list comes from the live
  // collection set (so retagged/deleted types vanish on the next refresh)
  // and the title-pattern checkable mirrors the active collection's flag,
  // which changes per-view.
  QMenu *menu = m_filterButton->menu();
  if (!menu) {
    menu = new QMenu(m_filterButton);
    // Saved-filter entries carry their query as a tooltip (Kartend-w4knq);
    // QMenu suppresses action tooltips unless asked, so the names would be
    // the only thing distinguishing two similar filters.
    menu->setToolTipsVisible(true);
    m_filterButton->setMenu(menu);
    QObject::connect(menu, &QMenu::triggered, this, [this](QAction *action) {
      if (!action || !m_mainWindow) {
        return;
      }
      // Kartend-0zhz: per-action role lookup is a strongly-typed enum
      // lookup now, not a QObject dynamic property.
      const auto roleIt = m_filterRoles.constFind(action);
      if (roleIt == m_filterRoles.constEnd()) {
        return;
      }
      const FilterRole role = roleIt.value();
      if (role == FilterRole::Type) {
        const QString chosen = action->data().toString();
        if (m_mainWindow->m_generalSettings.view.collectionTypeFilter == chosen) {
          return;
        }
        m_mainWindow->m_generalSettings.view.collectionTypeFilter = chosen;
        if (settingsMgr()) {
          ErrorPresentation::reportSaveResult(
              settingsMgr()->saveGeneralSettings(m_mainWindow->m_generalSettings),
              "general settings", true);
        }
        if (navMgr() && m_mainWindow->m_currentCollectionIndex >= 0) {
          navMgr()->safeReloadCollection(m_mainWindow->m_currentCollectionIndex);
        }
      } else if (role == FilterRole::TitleToggle) {
        if (m_mainWindow->m_currentCollectionIndex < 0 ||
            m_mainWindow->m_currentCollectionIndex >= m_mainWindow->m_collections.size()) {
          return;
        }
        CollectionConfig &c = m_mainWindow->m_collections[m_mainWindow->m_currentCollectionIndex];
        const bool checked = action->isChecked();
        if (c.filter.titleExclusionEnabled == checked) {
          return;
        }
        c.filter.titleExclusionEnabled = checked;
        // Per-click interactive path: the flag is already applied to the
        // live collection (the reload below reads memory), so ride the
        // debounced save stage instead of paying a full INI rewrite +
        // config-dir fsync on every toggle. A save still pending at quit
        // is covered by the shutdown path's synchronous persist.
        m_mainWindow->requestDebouncedCollectionsSave();
        if (navMgr()) {
          navMgr()->safeReloadCollection(m_mainWindow->m_currentCollectionIndex);
        }
      } else if (role == FilterRole::TitleEdit) {
        showTitleFilterEditor();
      } else if (role == FilterRole::SavePreset) {
        saveCurrentSearchAsPreset();
      } else if (role == FilterRole::ApplyPreset) {
        // The action carries the preset's name rather than an index: the
        // registry can be reordered by a save between this menu being built
        // and being clicked, and a stale index would apply the wrong filter.
        const QString name = action->data().toString();
        ensureSearchPresetsLoaded();
        // Resolved to a COPY before applying: applySearchPreset rebuilds this
        // very menu, and anything that later made that rebuild re-read the
        // registry would leave a reference into m_searchPresets dangling
        // mid-call. Cheap struct; not worth the hazard.
        SearchPreset match;
        bool found = false;
        for (const SearchPreset &preset : std::as_const(m_searchPresets)) {
          if (preset.name.compare(name, Qt::CaseInsensitive) == 0) {
            match = preset;
            found = true;
            break;
          }
        }
        if (found) {
          m_mainWindow->applySearchPreset(match);
        }
      } else if (role == FilterRole::ManagePresets) {
        if (!m_onManageSearchPresets) return;
        m_onManageSearchPresets();
        // The dialog owns its own copy of the registry and persists on close,
        // so this controller's cache is stale the moment it returns.
        invalidateSearchPresetCache();
        refreshFilterToolbar();
      }
    });
  } else {
    menu->clear();
  }
  // Wiped on clear() — null the cached pointer so we don't dereference a
  // dangling QAction the next time refresh runs without rebuilding the
  // section.
  m_titleFilterEnabledAction = nullptr;
  // Kartend-0zhz: clear the per-action role map so old QAction* keys
  // from the just-cleared menu can't accumulate as dangling references.
  m_filterRoles.clear();

  // Type filter section — only emitted when at least one collection actually
  // declares a type tag. QActionGroup gives radio-button semantics so the
  // <All types> sentinel and concrete types are mutually exclusive.
  // Ownership is the menu so the group dies with the next clear().
  const QStringList allTypes =
      CollectionUtils::collectAllCollectionTypes(m_mainWindow->m_collections);
  if (!allTypes.isEmpty()) {
    auto *typeGroup = new QActionGroup(menu);
    typeGroup->setExclusive(true);

    const QString previous = m_mainWindow->m_generalSettings.view.collectionTypeFilter;
    bool matchedPrevious = previous.isEmpty();

    QAction *allAction = menu->addAction(tr("<All types>"));
    allAction->setCheckable(true);
    allAction->setData(QString());
    m_filterRoles.insert(allAction, FilterRole::Type);
    typeGroup->addAction(allAction);
    if (previous.isEmpty()) {
      allAction->setChecked(true);
    }

    for (const QString &type : allTypes) {
      QAction *action = menu->addAction(type);
      action->setCheckable(true);
      action->setData(type);
      m_filterRoles.insert(action, FilterRole::Type);
      typeGroup->addAction(action);
      if (type == previous) {
        action->setChecked(true);
        matchedPrevious = true;
      }
    }

    // If the previously-selected type no longer exists (collection deleted
    // or retagged), fall back to <All types> and clear the persisted filter
    // so the toolbar reflects reality.
    if (!matchedPrevious) {
      allAction->setChecked(true);
      m_mainWindow->m_generalSettings.view.collectionTypeFilter.clear();
      if (settingsMgr()) {
        ErrorPresentation::reportSaveResult(
            settingsMgr()->saveGeneralSettings(m_mainWindow->m_generalSettings), "general settings",
            true);
      }
    }

    menu->addSeparator();
  }

  // Title-pattern section — always present so the user can edit patterns
  // even on collections that haven't enabled the toggle yet. Mirror the
  // active collection's flag onto the checkable entry.
  QAction *toggleAction = menu->addAction(tr("Apply title patterns"));
  toggleAction->setCheckable(true);
  m_filterRoles.insert(toggleAction, FilterRole::TitleToggle);
  bool toggleOn = false;
  if (m_mainWindow->m_currentCollectionIndex >= 0 &&
      m_mainWindow->m_currentCollectionIndex < m_mainWindow->m_collections.size()) {
    const CollectionConfig &c = m_mainWindow->m_collections[m_mainWindow->m_currentCollectionIndex];
    toggleOn = c.filter.titleExclusionEnabled && !c.filter.titleExclusionPatterns.isEmpty();
  }
  {
    QSignalBlocker blocker(toggleAction);
    toggleAction->setChecked(toggleOn);
  }
  m_titleFilterEnabledAction = toggleAction;

  QAction *editAction = menu->addAction(tr("Edit title patterns…"));
  m_filterRoles.insert(editAction, FilterRole::TitleEdit);

  appendSavedFilterSection(menu);
}

void ToolbarController::appendSavedFilterSection(QMenu *menu) {
  // Kartend-w4knq: the filter popup is where the state a preset captures
  // already lives, so it is where saving and recalling it belongs.
  if (!menu) return;
  ensureSearchPresetsLoaded();

  menu->addSeparator();

  if (!m_searchPresets.isEmpty()) {
    // Section label. A disabled action rather than a QMenu title so the entry
    // list stays flat — a submenu would hide the presets behind another hover
    // on the one popup the user opened specifically to change the filter.
    QAction *label = menu->addAction(tr("Saved filters"));
    label->setEnabled(false);
    for (const SearchPreset &preset : std::as_const(m_searchPresets)) {
      if (preset.name.trimmed().isEmpty()) continue;
      QAction *action = menu->addAction(preset.name);
      action->setData(preset.name);
      // The query as a tooltip: the entry has to stay short enough to read as
      // a menu row, but which of two similarly-named filters is which is
      // exactly the question the query answers. Set explicitly even when
      // there is no query — an unset QAction tooltip falls back to the
      // action's own text, which would just repeat the name back at the user.
      const QString query = preset.searchText.trimmed();
      action->setToolTip(query.isEmpty() ? tr("Filters and sort only — no search text") : query);
      m_filterRoles.insert(action, FilterRole::ApplyPreset);
    }
  }

  QAction *saveAction = menu->addAction(tr("Save current filter as…"));
  m_filterRoles.insert(saveAction, FilterRole::SavePreset);
  QAction *manageAction = menu->addAction(tr("Manage saved filters…"));
  m_filterRoles.insert(manageAction, FilterRole::ManagePresets);
}

void ToolbarController::ensureSearchPresetsLoaded() {
  if (m_searchPresetsLoaded) return;
  m_searchPresetsLoaded = true;
  auto loaded = SearchPresetIO::loadRegistry(SettingsUtils::getSearchPresetsPath());
  // A missing registry is the normal first-run state, and loadRegistry reports
  // a genuine read/parse failure the same way. Neither is worth a modal while
  // the user is opening a menu — the Manage dialog surfaces the real error
  // when they go looking for their filters.
  m_searchPresets = loaded.isError() ? QList<SearchPreset>{} : loaded.value();
}

void ToolbarController::invalidateSearchPresetCache() {
  m_searchPresetsLoaded = false;
  m_searchPresets.clear();
}

void ToolbarController::persistSearchPresets() {
  auto saved = SearchPresetIO::saveRegistry(m_searchPresets, SettingsUtils::getSearchPresetsPath());
  if (saved.isError()) {
    QMessageBox::warning(m_mainWindow, tr("Saved filters — could not save"), saved.error().message);
  }
}

void ToolbarController::saveCurrentSearchAsPreset() {
  if (!m_mainWindow) return;
  ensureSearchPresetsLoaded();

  bool ok = false;
  const QString name = QInputDialog::getText(m_mainWindow, tr("Save filter"), tr("Filter name:"),
                                             QLineEdit::Normal, QString(), &ok);
  if (!ok) return;
  const QString trimmed = name.trimmed();
  if (trimmed.isEmpty()) {
    QMessageBox::warning(m_mainWindow, tr("Save filter"), tr("The filter name cannot be empty."));
    return;
  }
  // addOrReplace is name-keyed and case-insensitive, so a clashing name
  // replaces silently. Ask first — the registry has no undo.
  for (const SearchPreset &existing : std::as_const(m_searchPresets)) {
    if (existing.name.trimmed().compare(trimmed, Qt::CaseInsensitive) == 0) {
      const auto choice = QMessageBox::question(
          m_mainWindow, tr("Overwrite filter"),
          tr("A saved filter named \"%1\" already exists. Overwrite it?").arg(trimmed),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
      if (choice != QMessageBox::Yes) return;
      break;
    }
  }

  // The search box's text is not a ViewSettings field, so it is captured
  // separately here rather than falling out of the settings snapshot.
  const QString searchText = m_searchBar ? m_searchBar->text() : QString();
  const SearchPreset preset =
      SearchPresetIO::fromViewSettings(m_mainWindow->m_generalSettings.view, searchText, trimmed);
  m_searchPresets = SearchPresetIO::addOrReplace(m_searchPresets, preset);
  persistSearchPresets();
  refreshFilterToolbar();
}

void ToolbarController::showTitleFilterEditor() {
  if (!m_mainWindow) {
    return;
  }
  if (m_mainWindow->m_currentCollectionIndex < 0 ||
      m_mainWindow->m_currentCollectionIndex >= m_mainWindow->m_collections.size()) {
    return;
  }
  CollectionConfig &c = m_mainWindow->m_collections[m_mainWindow->m_currentCollectionIndex];

  // Modal popup-style dialog. A QDialog with a QPlainTextEdit lets the user
  // see and edit the full pattern list at once; QMenu-with-widget would
  // dismiss on focus loss while the user is editing a long regex.
  QDialog dialog(m_mainWindow);
  dialog.setWindowTitle(tr("Filter — %1").arg(c.name));
  dialog.setModal(true);
  auto *layout = new QVBoxLayout(&dialog);

  auto *label =
      new QLabel(tr("Filter patterns (regex) — one per line. Each pattern is removed from item "
                    "titles in order. Examples:\n  \\s*\\(USA\\)$\n  \\s*\\[!\\]\n  "
                    "\\s*\\(Rev \\d+\\)"),
                 &dialog);
  label->setWordWrap(true);
  layout->addWidget(label);

  auto *editor = new QPlainTextEdit(&dialog);
  editor->setPlainText(c.filter.titleExclusionPatterns.join(QLatin1Char('\n')));
  editor->setPlaceholderText(tr("\\s*\\(USA\\)$"));
  layout->addWidget(editor, 1);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  dialog.resize(420, 280);

  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  QObject::connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, &dialog,
                   &QDialog::accept);

  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  // Split, trim, and drop empty lines so a stray newline can't smuggle a
  // pattern that matches everything (empty regex is technically valid and
  // would erase the whole title).
  QStringList parsed;
  const QStringList rawLines = editor->toPlainText().split(QLatin1Char('\n'));
  parsed.reserve(rawLines.size());
  for (const QString &line : rawLines) {
    const QString trimmed = line.trimmed();
    if (!trimmed.isEmpty()) {
      parsed.append(trimmed);
    }
  }
  if (parsed == c.filter.titleExclusionPatterns) {
    return; // Nothing actually changed; skip the reload.
  }
  c.filter.titleExclusionPatterns = parsed;
  // Auto-enable when the user adds the first pattern from an empty list, so
  // applying immediately does the visible thing. Honor the existing toggle
  // otherwise so a user who explicitly disabled cleanup keeps it off.
  if (!parsed.isEmpty() && !c.filter.titleExclusionEnabled) {
    c.filter.titleExclusionEnabled = true;
  }
  // Deliberately immediate, not debounced: this is a one-shot modal-dialog
  // apply (the user just committed an edited pattern list), not a per-click
  // burst path — persist it before the reload below so the change survives
  // even an immediate crash/quit.
  if (settingsMgr()) {
    ErrorPresentation::reportSaveResult(settingsMgr()->saveCollections(m_mainWindow->m_collections),
                                        "collections", true);
  }
  refreshFilterToolbar();
  if (navMgr() && m_mainWindow->m_currentCollectionIndex >= 0) {
    navMgr()->safeReloadCollection(m_mainWindow->m_currentCollectionIndex);
  }
}
