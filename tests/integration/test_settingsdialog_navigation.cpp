#include "test_settingsdialog_navigation.h"

#include "collection/collectionconfig.h"
#include "isettingsdialog.h" // SettingsPage
#include "settingsdialog.h"

#include <QListWidget>
#include <QListWidgetItem>
#include <QStackedWidget>
#include <QTest>

namespace {

QList<CollectionConfig> singleCollection() {
  CollectionConfig c;
  c.name = QStringLiteral("Nav Target");
  c.gridLayout.gridWidth = 5;
  c.gridLayout.itemWidth = 200;
  c.gridLayout.itemHeight = 300;
  c.gridLayout.fontSize = 12;
  c.viewType = ViewType::Grid;
  return {c};
}

// The Launchers (application-wide) page is the 14th QStackedWidget child
// (index 13) per the addEntry() order in settingsdialog setupNavigation. This
// constant documents the contract setInitialPage(Launchers) relies on; if the
// .ui page order changes, this test fails loudly rather than the badge
// silently opening the wrong tab.
constexpr int kLaunchersPageIndex = 13;

} // namespace

void TestSettingsDialogNavigation::defaultHint_landsOnFirstCollectionRow() {
  SettingsDialog dialog(nullptr, singleCollection(), 0);
  auto *pageStack = dialog.findChild<QStackedWidget *>(QStringLiteral("pageStack"));
  QVERIFY2(pageStack, "pageStack must exist on the dialog .ui");
  // Constructor selects the first real category row (Configuration, page 0).
  // setInitialPage(Default) is a no-op, so the landing page is unchanged.
  dialog.setInitialPage(SettingsPage::Default);
  QCOMPARE(pageStack->currentIndex(), 0);
}

void TestSettingsDialogNavigation::launchersHint_selectsGlobalLaunchersRow() {
  SettingsDialog dialog(nullptr, singleCollection(), 0);
  auto *pageStack = dialog.findChild<QStackedWidget *>(QStringLiteral("pageStack"));
  auto *categoryList = dialog.findChild<QListWidget *>(QStringLiteral("categoryList"));
  QVERIFY2(pageStack, "pageStack must exist on the dialog .ui");
  QVERIFY2(categoryList, "categoryList must exist on the dialog .ui");

  dialog.setInitialPage(SettingsPage::Launchers);

  QCOMPARE(pageStack->currentIndex(), kLaunchersPageIndex);
  // The selected rail row must be the global "Launchers" entry, not the
  // per-collection "Launcher" row — they share a system-run icon, so the
  // page index is the discriminator. The text guards against the index and
  // the label drifting apart.
  const QListWidgetItem *current = categoryList->currentItem();
  QVERIFY(current);
  QCOMPARE(current->text(), QStringLiteral("Launchers"));
}

void TestSettingsDialogNavigation::defaultHintAfterConstruction_leavesRowUnchanged() {
  SettingsDialog dialog(nullptr, singleCollection(), 0);
  auto *pageStack = dialog.findChild<QStackedWidget *>(QStringLiteral("pageStack"));
  QVERIFY(pageStack);
  // Move to Launchers, then assert Default does not snap us back.
  dialog.setInitialPage(SettingsPage::Launchers);
  QCOMPARE(pageStack->currentIndex(), kLaunchersPageIndex);
  dialog.setInitialPage(SettingsPage::Default);
  QCOMPARE(pageStack->currentIndex(), kLaunchersPageIndex);
}
