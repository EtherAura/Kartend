#include "test_toolbarcontroller.h"

#include "applicationmanager.h"
#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "collection/launcherconfig.h"
#include "collection/launcherpreset.h"
#include "collection/searchpreset.h"
#include "collectiontypes.h"
#include "mainwindow.h"
#include "mocks/mockedmainwindowfixture.h"
#include "mocks/mocksettingsmanager.h"
#include "pathutils.h"
#include "settingsutils.h"
#include "toolbarcontroller.h"
#include "uiconstants/timing.h"

#include <QAction>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QList>
#include <QMenu>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QWidget>

void TestToolbarController::setupViewModeButton_syncChecksExactlyActiveEntry_data() {
  QTest::addColumn<int>("viewType");
  QTest::addColumn<QByteArray>("expectedText");
  QTest::addColumn<QByteArray>("tooltipFragment");

  QTest::newRow("Grid") << int(ViewType::Grid) << QByteArrayLiteral("&Grid")
                        << QByteArrayLiteral("Grid");
  QTest::newRow("List") << int(ViewType::List) << QByteArrayLiteral("&List")
                        << QByteArrayLiteral("List");
  QTest::newRow("CoverFlow") << int(ViewType::CoverFlow) << QByteArrayLiteral("&Cover Flow")
                             << QByteArrayLiteral("Cover Flow");
  QTest::newRow("Horizontal") << int(ViewType::Horizontal) << QByteArrayLiteral("&Horizontal")
                              << QByteArrayLiteral("Horizontal");
}

void TestToolbarController::setupViewModeButton_syncChecksExactlyActiveEntry() {
  QFETCH(int, viewType);
  QFETCH(QByteArray, expectedText);
  QFETCH(QByteArray, tooltipFragment);

  // The picker menu + sync logic need no MainWindow — the back-pointer is
  // only dereferenced when an entry is actually triggered.
  QWidget host;
  auto *button = new QToolButton(&host);
  ToolbarController controller;
  ToolbarController::Setup setup;
  setup.viewModeButton = button;
  controller.initialize(setup);
  controller.setupViewModeButton();

  QVERIFY(button->menu());
  QCOMPARE(button->menu()->actions().size(), 4);

  controller.syncViewModeButton(ViewType(viewType));

  // Exactly the active view's entry is checked; the rest are explicitly
  // unchecked (sync sets checked == match for every entry).
  const auto actions = button->menu()->actions();
  for (QAction *action : actions) {
    QCOMPARE(action->isChecked(), action->text() == QString::fromLatin1(expectedText));
  }
  QVERIFY2(button->toolTip().contains(QString::fromLatin1(tooltipFragment)),
           qPrintable(QStringLiteral("tooltip '%1' misses '%2'")
                          .arg(button->toolTip(), QString::fromLatin1(tooltipFragment))));
}

// Kartend-lp7j9: opening the layout popup must reflect the ACTIVE collection's
// persisted viewType. setupViewModeButton() runs during MainWindow setup, which
// is before the deferred startup timer picks a collection, so at construction
// there is no view type to tick — and nothing came back to tick one later. The
// menu therefore opened with all four entries unchecked until the user's first
// explicit pick, i.e. it was blank exactly when consulted to answer "which view
// am I in?". The aboutToShow sync is what closes that.
void TestToolbarController::viewModePopup_aboutToShowSyncsFromActiveCollection() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();

  CollectionConfig films;
  films.name = QStringLiteral("Films");
  films.viewType = ViewType::CoverFlow; // persisted, and NOT the Grid default
  win->m_collections = {films};
  win->m_currentCollectionIndex = 0;

  QWidget host;
  auto *button = new QToolButton(&host);
  ToolbarController controller;
  ToolbarController::Setup setup;
  setup.mainWindow = win;
  setup.viewModeButton = button;
  controller.initialize(setup);
  controller.setupViewModeButton();

  QMenu *menu = button->menu();
  QVERIFY(menu);
  const auto actions = menu->actions();
  QCOMPARE(actions.size(), 4);

  // Premise: straight after construction nothing is checked. This IS the
  // startup state the bug report describes, reproduced here.
  for (QAction *action : actions) {
    QVERIFY2(!action->isChecked(), "premise: the picker starts with no entry checked");
  }

  // Opening the popup is the only thing that happens — no explicit
  // syncViewModeButton call, which is what the old code depended on.
  QMetaObject::invokeMethod(menu, "aboutToShow");

  for (QAction *action : actions) {
    QCOMPARE(action->isChecked(), action->text() == QStringLiteral("&Cover Flow"));
  }
  QVERIFY(button->toolTip().contains(QStringLiteral("Cover Flow")));
}

// Root/home view has no per-collection layout, so the sync must leave whatever
// was last checked alone rather than inventing a value (which would silently
// claim "Grid" for a view that isn't one).
void TestToolbarController::viewModePopup_aboutToShowLeavesStateAloneWithoutACollection() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();

  CollectionConfig films;
  films.name = QStringLiteral("Films");
  films.viewType = ViewType::List;
  win->m_collections = {films};
  win->m_currentCollectionIndex = -1; // root view

  QWidget host;
  auto *button = new QToolButton(&host);
  ToolbarController controller;
  ToolbarController::Setup setup;
  setup.mainWindow = win;
  setup.viewModeButton = button;
  controller.initialize(setup);
  controller.setupViewModeButton();

  // Seed a known checked entry, then open the popup with no active collection.
  controller.syncViewModeButton(ViewType::Horizontal);
  QMetaObject::invokeMethod(button->menu(), "aboutToShow");

  const auto actions = button->menu()->actions();
  for (QAction *action : actions) {
    QCOMPARE(action->isChecked(), action->text() == QStringLiteral("&Horizontal"));
  }

  // Same for an index past the end of the list.
  win->m_currentCollectionIndex = 7;
  QMetaObject::invokeMethod(button->menu(), "aboutToShow");
  for (QAction *action : actions) {
    QCOMPARE(action->isChecked(), action->text() == QStringLiteral("&Horizontal"));
  }
}

void TestToolbarController::setupSearchModeAction_installsLeadingSearchAction() {
  QWidget host;
  auto *searchBar = new QLineEdit(&host);
  ToolbarController controller;
  ToolbarController::Setup setup;
  setup.searchBar = searchBar;
  controller.initialize(setup);

  QVERIFY(!controller.searchModeAction());
  controller.setupSearchModeAction();

  QAction *action = controller.searchModeAction();
  QVERIFY2(action, "search-mode action must be created inside the search field");
  QCOMPARE(action->toolTip(), QStringLiteral("Toggle search scope"));
  // The action is installed on the line edit itself (LeadingPosition) so
  // the wiring pass can later connect it to InteractionManager.
  QVERIFY(searchBar->actions().contains(action));
}

void TestToolbarController::refreshHomeButton_followsStartupSettings() {
  QWidget host;
  auto *homeButton = new QToolButton(&host);
  ToolbarController controller;
  ToolbarController::Setup setup;
  setup.homeButton = homeButton;
  controller.initialize(setup);

  GeneralSettings gs;
  gs.startup.useHomeView = false;
  controller.refreshHomeButton(gs);
  QVERIFY2(homeButton->isHidden(), "home button must hide when useHomeView is off");

  gs.startup.useHomeView = true;
  controller.refreshHomeButton(gs);
  QVERIFY(!homeButton->isHidden());
  // Empty label falls back to the localized default.
  QCOMPARE(homeButton->toolTip(), QStringLiteral("Home"));

  gs.startup.homeViewLabel = QStringLiteral("Library");
  controller.refreshHomeButton(gs);
  QCOMPARE(homeButton->toolTip(), QStringLiteral("Library"));
}

void TestToolbarController::applyToolbarCustomization_togglesFilterAndSearchVisibility() {
  QWidget host;
  auto *filterButton = new QToolButton(&host);
  auto *homeButton = new QToolButton(&host);
  auto *searchBar = new QLineEdit(&host);
  ToolbarController controller;
  ToolbarController::Setup setup;
  setup.filterButton = filterButton;
  setup.homeButton = homeButton;
  setup.searchBar = searchBar;
  controller.initialize(setup);

  GeneralSettings gs;

  // The consolidated filter button stays visible while EITHER legacy flag
  // (type or title) is on, so pre-consolidation settings don't hide it.
  gs.toolbar.toolbarShowTypeFilter = false;
  gs.toolbar.toolbarShowTitleFilter = false;
  gs.toolbar.toolbarShowSearchBar = false;
  controller.applyToolbarCustomization(gs);
  QVERIFY(filterButton->isHidden());
  QVERIFY(searchBar->isHidden());

  gs.toolbar.toolbarShowTypeFilter = true;
  controller.applyToolbarCustomization(gs);
  QVERIFY(!filterButton->isHidden());

  gs.toolbar.toolbarShowTypeFilter = false;
  gs.toolbar.toolbarShowTitleFilter = true;
  controller.applyToolbarCustomization(gs);
  QVERIFY(!filterButton->isHidden());

  gs.toolbar.toolbarShowSearchBar = true;
  controller.applyToolbarCustomization(gs);
  QVERIFY(!searchBar->isHidden());
}

void TestToolbarController::refreshCollectionWarningBadge_hiddenWhenLauncherPathsClean() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();

  CollectionConfig videos;
  videos.name = QStringLiteral("Videos");
  // Empty launcher paths are "unconfigured", not "broken" — no badge.
  win->m_collections = {videos};
  win->m_currentCollectionIndex = 0;

  QWidget host;
  auto *badge = new QToolButton(&host);
  badge->setVisible(true);
  ToolbarController controller;
  ToolbarController::Setup setup;
  setup.mainWindow = win;
  setup.collectionWarningBadge = badge;
  controller.initialize(setup);

  controller.refreshCollectionWarningBadge();
  QVERIFY(badge->isHidden());
  QVERIFY(badge->toolTip().isEmpty());

  // Out-of-range collection index → no issues either.
  win->m_currentCollectionIndex = 5;
  badge->setVisible(true);
  controller.refreshCollectionWarningBadge();
  QVERIFY(badge->isHidden());
}

void TestToolbarController::refreshCollectionWarningBadge_missingLauncherShowsIssueTooltip() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString missing = dir.filePath(QStringLiteral("player-not-here"));

  CollectionConfig videos;
  videos.name = QStringLiteral("Videos");
  videos.launcher.launcherPath = missing;
  win->m_collections = {videos};
  win->m_currentCollectionIndex = 0;

  QWidget host;
  auto *badge = new QToolButton(&host);
  ToolbarController controller;
  ToolbarController::Setup setup;
  setup.mainWindow = win;
  setup.collectionWarningBadge = badge;
  controller.initialize(setup);

  controller.refreshCollectionWarningBadge();
  QVERIFY2(!badge->isHidden(), "an unresolvable launcher path must surface the badge");
  QVERIFY2(badge->toolTip().contains(missing),
           "tooltip must carry the launcherPathIssues line naming the broken path");
  QVERIFY(badge->toolTip().contains(QStringLiteral("Settings")));
}

void TestToolbarController::refreshCollectionWarningBadge_placeholderPathExpandsBeforeJudging() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();

  // A launcher routed through the %collection% placeholder that expands to
  // a real executable.
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("Videos")));
#ifdef Q_OS_WIN
  const QString scriptName = QStringLiteral("play.cmd");
#else
  const QString scriptName = QStringLiteral("play.sh");
#endif
  const QString expanded = dir.filePath(QStringLiteral("Videos/") + scriptName);
  {
    QFile script(expanded);
    QVERIFY(script.open(QIODevice::WriteOnly));
    script.write(QByteArrayLiteral("exit 0\n"));
    script.close();
    QVERIFY(script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                  QFile::ReadUser | QFile::ExeUser));
  }
  // Premise: the EXPANDED path is a perfectly launchable binary.
  QCOMPARE(PathUtils::checkLauncherPath(expanded), PathUtils::PathStatus::OK);

  CollectionConfig videos;
  videos.name = QStringLiteral("Videos");
  videos.launcher.launcherPath = dir.filePath(QStringLiteral("%collection%/") + scriptName);
  win->m_collections = {videos};
  win->m_currentCollectionIndex = 0;

  QWidget host;
  auto *badge = new QToolButton(&host);
  badge->setVisible(true);
  ToolbarController controller;
  ToolbarController::Setup setup;
  setup.mainWindow = win;
  setup.collectionWarningBadge = badge;
  controller.initialize(setup);

  controller.refreshCollectionWarningBadge();

  // The badge now routes through the launch-time 3-arg overload
  // (launcherPathIssues(profile, presets, collectionName)), which expands
  // %collection% exactly like buildLaunchCommand before judging — a
  // placeholder-routed launcher that launches fine must NOT badge. (This
  // used to pin the raw-overload false positive; flipped when the
  // production call moved to the launch-time overload.)
  QVERIFY2(badge->isHidden(),
           "a %collection% path that expands to a launchable binary must not badge");
  QVERIFY(badge->toolTip().isEmpty());
}

void TestToolbarController::refreshCollectionWarningBadge_brokenPresetBackedEntryShowsBadge() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();

  // A preset-backed launcher slot whose inline fields are empty and whose
  // preset's stored path is broken. The raw overload skipped it as
  // "unconfigured" (no badge) even though the launch was guaranteed to fail;
  // the launch-time overload judges the preset's stored path — the same
  // verdict the pre-launch gate reaches — so the badge must show.
  LauncherPreset preset;
  preset.id = QStringLiteral("preset-player");
  preset.name = QStringLiteral("Cloud Player");
  preset.launcherPath = QStringLiteral("/nonexistent/preset/player");
  win->m_generalSettings.launchers.launcherPresets = {preset};

  CollectionConfig videos;
  videos.name = QStringLiteral("Videos");
  LauncherConfig entry;
  entry.presetId = preset.id; // inline path left empty — the preset supplies it
  videos.launcher.additionalLaunchers.append(entry);
  win->m_collections = {videos};
  win->m_currentCollectionIndex = 0;

  QWidget host;
  auto *badge = new QToolButton(&host);
  ToolbarController controller;
  ToolbarController::Setup setup;
  setup.mainWindow = win;
  setup.collectionWarningBadge = badge;
  controller.initialize(setup);

  controller.refreshCollectionWarningBadge();

  QVERIFY2(!badge->isHidden(), "a broken preset-backed entry must surface the badge");
  QVERIFY2(badge->toolTip().contains(QStringLiteral("/nonexistent/preset/player")),
           "tooltip must name the preset's stored path (what would actually run)");
}

void TestToolbarController::refreshFilterToolbar_listsSavedFiltersFromTheRegistry() {
  // Kartend-w4knq: saved filters are read from the on-disk registry and listed
  // in the filter popup under a disabled section label, above Save/Manage.
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();
  win->m_collections = {}; // no type tags → no type-radio section to count past

  SearchPreset unplayed;
  unplayed.name = QStringLiteral("Unplayed soundtracks");
  unplayed.searchText = QStringLiteral("played:false tag:soundtrack");
  SearchPreset sortOnly;
  sortOnly.name = QStringLiteral("Newest first");
  // No searchText at all: a preset may be pure filter/sort state.
  sortOnly.sortMode = SortMode::DateDescending;

  const QString registryPath = SettingsUtils::getSearchPresetsPath();
  auto written = SearchPresetIO::saveRegistry({unplayed, sortOnly}, registryPath);
  QVERIFY2(!written.isError(), qPrintable(written.isError() ? written.error().message : QString()));

  QWidget host;
  auto *filterButton = new QToolButton(&host);
  ToolbarController controller;
  ToolbarController::Setup setup;
  setup.mainWindow = win;
  setup.filterButton = filterButton;
  controller.initialize(setup);

  controller.refreshFilterToolbar();

  QMenu *menu = filterButton->menu();
  QVERIFY(menu);
  const auto actions = menu->actions();
  // Title toggle, title editor, separator, "Saved filters" label, the two
  // presets, Save current, Manage.
  QCOMPARE(actions.size(), 8);
  QCOMPARE(actions.at(0)->text(), QStringLiteral("Apply title patterns"));
  QCOMPARE(actions.at(1)->text(), QStringLiteral("Edit title patterns…"));
  QVERIFY(actions.at(2)->isSeparator());
  QCOMPARE(actions.at(3)->text(), QStringLiteral("Saved filters"));
  QVERIFY2(!actions.at(3)->isEnabled(), "the section label must not be clickable");
  QCOMPARE(actions.at(4)->text(), QStringLiteral("Unplayed soundtracks"));
  QCOMPARE(actions.at(5)->text(), QStringLiteral("Newest first"));
  QCOMPARE(actions.at(6)->text(), QStringLiteral("Save current filter as…"));
  QCOMPARE(actions.at(7)->text(), QStringLiteral("Manage saved filters…"));

  // Entries are addressed by NAME, not by registry index — a save that
  // reorders the registry between build and click must not apply the wrong
  // filter.
  QCOMPARE(actions.at(4)->data().toString(), QStringLiteral("Unplayed soundtracks"));
  QCOMPARE(actions.at(5)->data().toString(), QStringLiteral("Newest first"));
  // The query rides along as a tooltip so two similar names stay tellable
  // apart. The queryless preset gets an explicit tooltip rather than none:
  // an unset QAction tooltip falls back to the action's text, which would
  // just repeat the name.
  QCOMPARE(actions.at(4)->toolTip(), QStringLiteral("played:false tag:soundtrack"));
  QCOMPARE(actions.at(5)->toolTip(), QStringLiteral("Filters and sort only — no search text"));
  QVERIFY2(menu->toolTipsVisible(), "QMenu suppresses action tooltips unless asked");

  QVERIFY(QFile::remove(registryPath));
}

void TestToolbarController::refreshFilterToolbar_buildsTypeRadiosAndTitleEntries() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();

  CollectionConfig films;
  films.name = QStringLiteral("Films");
  films.type = QStringLiteral("Video");
  CollectionConfig albums;
  albums.name = QStringLiteral("Albums");
  albums.type = QStringLiteral("Audio");
  albums.filter.titleExclusionEnabled = true;
  albums.filter.titleExclusionPatterns = {QStringLiteral("\\s*\\(demo\\)$")};
  win->m_collections = {films, albums};
  win->m_currentCollectionIndex = 1; // Albums drives the title-pattern toggle
  win->m_generalSettings.view.collectionTypeFilter = QStringLiteral("Video");

  QWidget host;
  auto *filterButton = new QToolButton(&host);
  ToolbarController controller;
  ToolbarController::Setup setup;
  setup.mainWindow = win;
  setup.filterButton = filterButton;
  controller.initialize(setup);

  controller.refreshFilterToolbar();

  QMenu *menu = filterButton->menu();
  QVERIFY(menu);
  // <All types>, Audio, Video (case-insensitive sort), separator, title
  // toggle, title editor, then the saved-filter section (Kartend-w4knq):
  // separator, Save current, Manage. No preset entries and no "Saved
  // filters" label here — this fixture's registry is empty.
  const auto actions = menu->actions();
  QCOMPARE(actions.size(), 9);
  QCOMPARE(actions.at(0)->text(), QStringLiteral("<All types>"));
  QCOMPARE(actions.at(1)->text(), QStringLiteral("Audio"));
  QCOMPARE(actions.at(2)->text(), QStringLiteral("Video"));
  QVERIFY(actions.at(3)->isSeparator());
  QCOMPARE(actions.at(4)->text(), QStringLiteral("Apply title patterns"));
  QCOMPARE(actions.at(5)->text(), QStringLiteral("Edit title patterns…"));
  QVERIFY(actions.at(6)->isSeparator());
  QCOMPARE(actions.at(7)->text(), QStringLiteral("Save current filter as…"));
  QCOMPARE(actions.at(8)->text(), QStringLiteral("Manage saved filters…"));

  // The persisted type filter is reflected as the checked radio…
  QVERIFY(!actions.at(0)->isChecked());
  QVERIFY(!actions.at(1)->isChecked());
  QVERIFY(actions.at(2)->isChecked());
  // …and the active collection's title-exclusion state drives the toggle.
  QVERIFY(actions.at(4)->isChecked());

  // Refresh is a full rebuild, not an append — the action count is stable.
  controller.refreshFilterToolbar();
  QCOMPARE(filterButton->menu()->actions().size(), 9);
}

void TestToolbarController::refreshFilterToolbar_staleTypeFilterFallsBackToAllTypes() {
  KartendTest::MockedMainWindowFixture fixture;
  MainWindow *win = fixture.window();

  CollectionConfig films;
  films.name = QStringLiteral("Films");
  films.type = QStringLiteral("Video");
  win->m_collections = {films};
  win->m_currentCollectionIndex = 0;
  // The persisted filter names a type that no collection carries anymore
  // (collection deleted / retagged since the filter was saved).
  win->m_generalSettings.view.collectionTypeFilter = QStringLiteral("Slides");

  QWidget host;
  auto *filterButton = new QToolButton(&host);
  ToolbarController controller;
  ToolbarController::Setup setup;
  setup.mainWindow = win;
  setup.filterButton = filterButton;
  controller.initialize(setup);

  controller.refreshFilterToolbar();

  const auto actions = filterButton->menu()->actions();
  QVERIFY2(actions.first()->isChecked(),
           "a stale persisted type filter must fall back to <All types>");
  QVERIFY2(win->m_generalSettings.view.collectionTypeFilter.isEmpty(),
           "the stale persisted filter must be cleared so the toolbar reflects reality");
}

void TestToolbarController::titleFilterToggle_burstCoalescesIntoOneDebouncedSave() {
  CollectionConfig albums;
  albums.name = QStringLiteral("Albums");
  albums.type = QStringLiteral("Audio");
  albums.filter.titleExclusionPatterns = {QStringLiteral("\\s*\\(demo\\)$")};
  // The struct default is enabled=true; pin the start state so the toggle
  // begins unchecked and the on/off/on burst below lands on enabled.
  albums.filter.titleExclusionEnabled = false;

  // Seed via the fixture ctor rather than assigning m_collections afterwards:
  // an empty-at-startup MainWindow schedules the modal first-collection
  // prompt via singleShot(0), which fires the moment this test spins the
  // event loop (QTRY below) and hangs the run headless.
  KartendTest::MockedMainWindowFixture fixture({albums});
  MainWindow *win = fixture.window();
  win->m_currentCollectionIndex = 0;

  QWidget host;
  auto *filterButton = new QToolButton(&host);
  ToolbarController controller;
  ToolbarController::Setup setup;
  setup.mainWindow = win;
  setup.filterButton = filterButton;
  controller.initialize(setup);
  controller.refreshFilterToolbar();

  auto *mock = qobject_cast<KartendTest::MockSettingsManager *>(
      win->applicationManager()->getSettingsManager());
  QVERIFY2(mock, "MockedMainWindowFixture must have installed MockSettingsManager");
  const int baseline = mock->saveCollectionsCalls();

  // Resolve the toggle by label (positional grabs land on a neighbor), and
  // re-resolve before every click: the reload each trigger schedules rebuilds
  // the filter menu, deleting and recreating its actions, so a pointer held
  // across triggers goes stale.
  const auto findToggle = [filterButton]() -> QAction * {
    const auto menuActions = filterButton->menu()->actions();
    for (QAction *action : menuActions) {
      if (action->text() == QStringLiteral("Apply title patterns")) {
        return action;
      }
    }
    return nullptr;
  };

  // Simulate a rapid on/off/on click burst. Each trigger mutates the live
  // collection immediately (the reload it queues reads memory)…
  for (int i = 0; i < 3; ++i) {
    QAction *toggle = findToggle();
    QVERIFY2(toggle, "filter menu must offer the Apply title patterns toggle");
    toggle->trigger();
  }

  // …but no INI write happens inline: the per-click path rides MainWindow's
  // debounced save stage instead of paying a full rewrite per click.
  QCOMPARE(mock->saveCollectionsCalls(), baseline);
  QVERIFY2(win->m_collections[0].filter.titleExclusionEnabled,
           "the toggle must land in the live collection before any disk write");

  // Exactly one coalesced write once the debounce window elapses, and no
  // trailing second write after a further full window.
  QTRY_COMPARE(mock->saveCollectionsCalls(), baseline + 1);
  QTest::qWait(UIConstants::Timing::LONG_DELAY_MS * 2);
  QCOMPARE(mock->saveCollectionsCalls(), baseline + 1);
}
