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
#include <QImage>
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
#include "propertyutils.h"

class TestChromeFollowsDesktopColor : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void cleanupTestCase();
  void toolbarTintFollowsTitlebarWithoutRestart();
  void collectionTintedChromeIsLeftAlone();
  void unsetOwnColourBarTakesTitlebarFromItsStyleSheet();
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

void TestChromeFollowsDesktopColor::unsetOwnColourBarTakesTitlebarFromItsStyleSheet() {
  // Kartend-b8955 (field report 2026-11-21): a collection on its OWN colour
  // with none chosen (the user's "Games") used to get its titlebar fill only
  // from the bar's palette, which Qt's style-sheet machinery replaces with a
  // saved copy whenever the bar's style sheet changes. After a desktop colour
  // change, a trip through a titlebar-sourced collection and back left the
  // bar on the plain window colour. The fill now lives in the style sheet,
  // where nothing can quietly put an old one back.
  QScrollArea scrollArea;
  QWidget topBar;
  topBar.setObjectName(QStringLiteral("itemsTopBar"));
  topBar.resize(200, 40);
  QMenuBar menubar;
  QLineEdit searchBar;

  QList<CollectionConfig> collections;
  CollectionConfig games;
  games.background.toolbarColorSource = ToolbarColorSource::CollectionPrimary;
  collections.append(games); // primaryColor left empty
  CollectionConfig nintendo;
  nintendo.background.toolbarColorSource = ToolbarColorSource::Titlebar;
  collections.append(nintendo);
  int currentIndex = 0;

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

  const auto paintedFill = [&topBar]() {
    const QImage frame = topBar.grab().toImage();
    return frame.pixelColor(frame.width() / 2, frame.height() / 2).name();
  };

  controller.applyPrimaryColorForCollection(0);
  QCOMPARE(paintedFill(), QColor(190, 70, 20).name());
  QCOMPARE(topBar.property(PropertyKeys::ToolbarChromeFill).value<QColor>().name(),
           QColor(190, 70, 20).name());
  // Only the FILL falls back: the controls keep the untinted look this
  // collection always had, so no control rules appear.
  QVERIFY2(!topBar.styleSheet().contains(QStringLiteral("QToolButton")),
           qPrintable(topBar.styleSheet()));

  // The desktop colour changes: this bar is desktop-derived, so it repaints.
  writeTitlebarColor(QStringLiteral("30,110,60"));
  controller.refreshDesktopDerivedChrome();
  QCOMPARE(paintedFill(), QColor(30, 110, 60).name());

  // ...and survives the round trip that used to lose it.
  currentIndex = 1;
  controller.applyPrimaryColorForCollection(1);
  currentIndex = 0;
  controller.applyPrimaryColorForCollection(0);
  QCOMPARE(paintedFill(), QColor(30, 110, 60).name());
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
