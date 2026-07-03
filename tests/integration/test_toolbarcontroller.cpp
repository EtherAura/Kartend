#include "test_toolbarcontroller.h"

#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "collection/launcherconfig.h"
#include "collection/launcherpreset.h"
#include "collectiontypes.h"
#include "mainwindow.h"
#include "mocks/mockedmainwindowfixture.h"
#include "pathutils.h"
#include "toolbarcontroller.h"

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
  // toggle, title editor.
  const auto actions = menu->actions();
  QCOMPARE(actions.size(), 6);
  QCOMPARE(actions.at(0)->text(), QStringLiteral("<All types>"));
  QCOMPARE(actions.at(1)->text(), QStringLiteral("Audio"));
  QCOMPARE(actions.at(2)->text(), QStringLiteral("Video"));
  QVERIFY(actions.at(3)->isSeparator());
  QCOMPARE(actions.at(4)->text(), QStringLiteral("Apply title patterns"));
  QCOMPARE(actions.at(5)->text(), QStringLiteral("Edit title patterns…"));

  // The persisted type filter is reflected as the checked radio…
  QVERIFY(!actions.at(0)->isChecked());
  QVERIFY(!actions.at(1)->isChecked());
  QVERIFY(actions.at(2)->isChecked());
  // …and the active collection's title-exclusion state drives the toggle.
  QVERIFY(actions.at(4)->isChecked());

  // Refresh is a full rebuild, not an append — the action count is stable.
  controller.refreshFilterToolbar();
  QCOMPARE(filterButton->menu()->actions().size(), 6);
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
