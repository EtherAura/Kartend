// Toolbar chrome must follow the DESKTOP colour while the app runs — a
// Plasma activity switch with a per-activity wallpaper hands the session a
// new titlebar colour, and the bar has to move with it (user request
// 2026-08-19).
//
// Why this is not covered by the palette path: the toolbar fill is baked
// into a stylesheet STRING read from kdeglobals, not resolved from
// QPalette. Measured on a Plasma guest — the desktop colour changed, the
// watcher fired, but no palette event was dispatched, so the deferred
// re-theme never ran and the bar kept its startup colour until a restart.
#include "collectionbackgroundcontroller.h"

#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QList>
#include <QMenuBar>
#include <QScrollArea>
#include <QStandardPaths>
#include <QTest>
#include <QTextStream>
#include <QWidget>

#include "collection/collectionconfig.h"
#include "kdecolorscheme.h"
#include "overlayscrollbars.h"

class TestChromeFollowsDesktopColor : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void cleanupTestCase();
  void toolbarTintFollowsTitlebarWithoutRestart();
  void collectionTintedChromeIsLeftAlone();
  void scrollbarHandleStaysVisibleOnATitlebarFilledRow();

private:
  static void writeTitlebarColor(const QString &rgb);
};

void TestChromeFollowsDesktopColor::initTestCase() {
  QStandardPaths::setTestModeEnabled(true);
  QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation));
}

void TestChromeFollowsDesktopColor::cleanupTestCase() {
  QStandardPaths::setTestModeEnabled(false);
}

void TestChromeFollowsDesktopColor::writeTitlebarColor(const QString &rgb) {
  const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
                       QStringLiteral("/kdeglobals");
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
  QTextStream(&f) << "[WM]\nactiveBackground=" << rgb << "\nactiveForeground=255,255,255\n";
}

void TestChromeFollowsDesktopColor::toolbarTintFollowsTitlebarWithoutRestart() {
  QScrollArea scrollArea;
  QWidget topBar;
  QMenuBar menubar;
  QLineEdit searchBar;

  QList<CollectionConfig> collections;
  CollectionConfig collection;
  collection.background.toolbarColorSource = ToolbarColorSource::Titlebar;
  collections.append(collection);
  const int currentIndex = 0;

  writeTitlebarColor(QStringLiteral("190,70,20"));

  CollectionBackgroundController controller;
  CollectionBackgroundControllerSetup setup;
  setup.itemScrollArea = &scrollArea;
  setup.itemsTopBar = &topBar;
  setup.menubar = &menubar;
  setup.searchBar = &searchBar;
  setup.currentCollectionIndex = &currentIndex;
  setup.collections = &collections;
  controller.setupReferences(setup);

  controller.applyPrimaryColorForCollection(0);
  QVERIFY2(topBar.styleSheet().contains(QColor(190, 70, 20).name(), Qt::CaseInsensitive),
           qPrintable(QStringLiteral("startup tint missing from: %1").arg(topBar.styleSheet())));

  // The desktop switches to a different activity/wallpaper.
  writeTitlebarColor(QStringLiteral("30,110,60"));
  controller.refreshDesktopDerivedChrome();

  QVERIFY2(topBar.styleSheet().contains(QColor(30, 110, 60).name(), Qt::CaseInsensitive),
           qPrintable(QStringLiteral("bar kept the old colour: %1").arg(topBar.styleSheet())));
  QVERIFY2(!topBar.styleSheet().contains(QColor(190, 70, 20).name(), Qt::CaseInsensitive),
           "the previous desktop colour must be gone, not merely joined");
}

void TestChromeFollowsDesktopColor::collectionTintedChromeIsLeftAlone() {
  // A collection tinted from its OWN primary colour has nothing to refresh;
  // repainting it on every desktop change would be pure waste (and would
  // re-run the appearance pipeline behind the user's back).
  QScrollArea scrollArea;
  QWidget topBar;
  QMenuBar menubar;
  QLineEdit searchBar;

  QList<CollectionConfig> collections;
  CollectionConfig collection;
  collection.background.toolbarColorSource = ToolbarColorSource::CollectionPrimary;
  collection.background.primaryColor = QStringLiteral("#3366cc");
  collections.append(collection);
  const int currentIndex = 0;

  writeTitlebarColor(QStringLiteral("190,70,20"));

  CollectionBackgroundController controller;
  CollectionBackgroundControllerSetup setup;
  setup.itemScrollArea = &scrollArea;
  setup.itemsTopBar = &topBar;
  setup.menubar = &menubar;
  setup.searchBar = &searchBar;
  setup.currentCollectionIndex = &currentIndex;
  setup.collections = &collections;
  controller.setupReferences(setup);

  controller.applyPrimaryColorForCollection(0);
  const QString before = topBar.styleSheet();

  writeTitlebarColor(QStringLiteral("30,110,60"));
  controller.refreshDesktopDerivedChrome();

  QCOMPARE(topBar.styleSheet(), before);
  QVERIFY2(!topBar.styleSheet().contains(QColor(30, 110, 60).name(), Qt::CaseInsensitive),
           "a collection-tinted bar must not pick up the desktop colour");
}

void TestChromeFollowsDesktopColor::scrollbarHandleStaysVisibleOnATitlebarFilledRow() {
  // The handle IS the titlebar colour (user decision 2026-08-19). It was
  // briefly lightened to survive being drawn over a titlebar-filled
  // selection; the selection now stops at the scrollbar lane instead, so
  // the handle no longer has to compromise. If something ever puts content
  // back under the handle, fix the content — not this colour.
  writeTitlebarColor(QStringLiteral("146,67,13"));

  const QColor fill = KdeColorScheme::activeTitlebarColor();
  QVERIFY(fill.isValid());
  const QColor handle = OverlayScrollbars::handleColor();
  QVERIFY(handle.isValid());

  QCOMPARE(handle.red(), fill.red());
  QCOMPARE(handle.green(), fill.green());
  QCOMPARE(handle.blue(), fill.blue());
}

QTEST_MAIN(TestChromeFollowsDesktopColor)
#include "test_chromefollowsdesktopcolor.moc"
