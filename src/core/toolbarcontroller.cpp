// Owns the items-page toolbar's stateful Qt widgets: the layout-picker
// QToolButton + four view QActions, the search-mode QAction inside the
// search QLineEdit, and the consolidated filter QToolButton + its type
// radio group / title-pattern toggle / pattern editor. Replaces a chunk of
// the previous mainwindowtoolbar.cpp + mainwindowwiring.cpp partial-class
// TUs that managed the same widgets through MainWindow::* members.
#include "toolbarcontroller.h"

#include "mainwindow.h"
#include "navigationmanager.h"
#include "settingsmanager.h"
#include "settingsutils.h"
#include "uiconstants.h"

#include <QAction>
#include <QActionGroup>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSize>
#include <QToolButton>
#include <QVBoxLayout>

ToolbarController::ToolbarController(QObject *parent) : QObject(parent) {}

void ToolbarController::initialize(const Setup &setup) {
  m_mainWindow = setup.mainWindow;
  m_viewModeButton = setup.viewModeButton;
  m_filterButton = setup.filterButton;
  m_homeButton = setup.homeButton;
  m_searchBar = setup.searchBar;
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

  m_viewModeButton->setMenu(menu);
  m_viewModeButton->setIcon(
      UIConstants::Icons::fromTheme({UIConstants::Icons::VIEW_PICKER, "view-list-icons"}));
  m_viewModeButton->setIconSize(QSize(18, 18));
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
    m_filterButton->setVisible(gs.toolbarShowTypeFilter || gs.toolbarShowTitleFilter);
  }
  if (m_searchBar) {
    m_searchBar->setVisible(gs.toolbarShowSearchBar);
  }
  refreshHomeButton(gs);
}

void ToolbarController::setupHomeButton() {
  if (!m_homeButton || !m_mainWindow) {
    return;
  }
  MainWindow *mw = m_mainWindow;
  QObject::connect(m_homeButton, &QToolButton::clicked, this, [mw]() {
    if (mw && mw->getNavigationManager()) {
      mw->getNavigationManager()->loadRootView();
    }
  });
}

void ToolbarController::refreshHomeButton(const GeneralSettings &gs) {
  if (!m_homeButton) {
    return;
  }
  m_homeButton->setVisible(gs.useHomeView);
  QIcon icon;
  const QString customPath = gs.homeViewIcon.trimmed();
  if (!customPath.isEmpty()) {
    icon = QIcon(customPath);
  }
  if (icon.isNull()) {
    icon = UIConstants::Icons::fromTheme({"go-home", "user-home", "go-home-symbolic"});
  }
  m_homeButton->setIcon(icon);
  m_homeButton->setIconSize(QSize(18, 18));
  const QString label = gs.homeViewLabel.trimmed();
  m_homeButton->setToolTip(label.isEmpty() ? tr("Home") : label);
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
    m_filterButton->setMenu(menu);
    QObject::connect(menu, &QMenu::triggered, this, [this](QAction *action) {
      if (!action || !m_mainWindow) {
        return;
      }
      const QString role = action->property("filterRole").toString();
      if (role == QLatin1String("type")) {
        const QString chosen = action->data().toString();
        if (m_mainWindow->m_generalSettings.collectionTypeFilter == chosen) {
          return;
        }
        m_mainWindow->m_generalSettings.collectionTypeFilter = chosen;
        if (m_mainWindow->getSettingsManager()) {
          m_mainWindow->getSettingsManager()->saveGeneralSettings(m_mainWindow->m_generalSettings);
        }
        if (m_mainWindow->getNavigationManager() && m_mainWindow->currentCollectionIndex >= 0) {
          m_mainWindow->getNavigationManager()->safeReloadCollection(
              m_mainWindow->currentCollectionIndex);
        }
      } else if (role == QLatin1String("title-toggle")) {
        if (m_mainWindow->currentCollectionIndex < 0 ||
            m_mainWindow->currentCollectionIndex >= m_mainWindow->m_collections.size()) {
          return;
        }
        CollectionConfig &c = m_mainWindow->m_collections[m_mainWindow->currentCollectionIndex];
        const bool checked = action->isChecked();
        if (c.titleExclusionEnabled == checked) {
          return;
        }
        c.titleExclusionEnabled = checked;
        if (m_mainWindow->getSettingsManager()) {
          m_mainWindow->getSettingsManager()->saveCollections(m_mainWindow->m_collections);
        }
        if (m_mainWindow->getNavigationManager()) {
          m_mainWindow->getNavigationManager()->safeReloadCollection(
              m_mainWindow->currentCollectionIndex);
        }
      } else if (role == QLatin1String("title-edit")) {
        showTitleFilterEditor();
      }
    });
  } else {
    menu->clear();
  }
  // Wiped on clear() — null the cached pointer so we don't dereference a
  // dangling QAction the next time refresh runs without rebuilding the
  // section.
  m_titleFilterEnabledAction = nullptr;

  // Type filter section — only emitted when at least one collection actually
  // declares a type tag. QActionGroup gives radio-button semantics so the
  // <All types> sentinel and concrete types are mutually exclusive.
  // Ownership is the menu so the group dies with the next clear().
  const QStringList allTypes =
      CollectionUtils::collectAllCollectionTypes(m_mainWindow->m_collections);
  if (!allTypes.isEmpty()) {
    auto *typeGroup = new QActionGroup(menu);
    typeGroup->setExclusive(true);

    const QString previous = m_mainWindow->m_generalSettings.collectionTypeFilter;
    bool matchedPrevious = previous.isEmpty();

    QAction *allAction = menu->addAction(tr("<All types>"));
    allAction->setCheckable(true);
    allAction->setData(QString());
    allAction->setProperty("filterRole", QStringLiteral("type"));
    typeGroup->addAction(allAction);
    if (previous.isEmpty()) {
      allAction->setChecked(true);
    }

    for (const QString &type : allTypes) {
      QAction *action = menu->addAction(type);
      action->setCheckable(true);
      action->setData(type);
      action->setProperty("filterRole", QStringLiteral("type"));
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
      m_mainWindow->m_generalSettings.collectionTypeFilter.clear();
      if (m_mainWindow->getSettingsManager()) {
        m_mainWindow->getSettingsManager()->saveGeneralSettings(m_mainWindow->m_generalSettings);
      }
    }

    menu->addSeparator();
  }

  // Title-pattern section — always present so the user can edit patterns
  // even on collections that haven't enabled the toggle yet. Mirror the
  // active collection's flag onto the checkable entry.
  QAction *toggleAction = menu->addAction(tr("Apply title patterns"));
  toggleAction->setCheckable(true);
  toggleAction->setProperty("filterRole", QStringLiteral("title-toggle"));
  bool toggleOn = false;
  if (m_mainWindow->currentCollectionIndex >= 0 &&
      m_mainWindow->currentCollectionIndex < m_mainWindow->m_collections.size()) {
    const CollectionConfig &c = m_mainWindow->m_collections[m_mainWindow->currentCollectionIndex];
    toggleOn = c.titleExclusionEnabled && !c.titleExclusionPatterns.isEmpty();
  }
  {
    QSignalBlocker blocker(toggleAction);
    toggleAction->setChecked(toggleOn);
  }
  m_titleFilterEnabledAction = toggleAction;

  QAction *editAction = menu->addAction(tr("Edit title patterns…"));
  editAction->setProperty("filterRole", QStringLiteral("title-edit"));
}

void ToolbarController::showTitleFilterEditor() {
  if (!m_mainWindow) {
    return;
  }
  if (m_mainWindow->currentCollectionIndex < 0 ||
      m_mainWindow->currentCollectionIndex >= m_mainWindow->m_collections.size()) {
    return;
  }
  CollectionConfig &c = m_mainWindow->m_collections[m_mainWindow->currentCollectionIndex];

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
  editor->setPlainText(c.titleExclusionPatterns.join(QLatin1Char('\n')));
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
  if (parsed == c.titleExclusionPatterns) {
    return; // Nothing actually changed; skip the reload.
  }
  c.titleExclusionPatterns = parsed;
  // Auto-enable when the user adds the first pattern from an empty list, so
  // applying immediately does the visible thing. Honor the existing toggle
  // otherwise so a user who explicitly disabled cleanup keeps it off.
  if (!parsed.isEmpty() && !c.titleExclusionEnabled) {
    c.titleExclusionEnabled = true;
  }
  if (m_mainWindow->getSettingsManager()) {
    m_mainWindow->getSettingsManager()->saveCollections(m_mainWindow->m_collections);
  }
  refreshFilterToolbar();
  if (m_mainWindow->getNavigationManager() && m_mainWindow->currentCollectionIndex >= 0) {
    m_mainWindow->getNavigationManager()->safeReloadCollection(
        m_mainWindow->currentCollectionIndex);
  }
}
