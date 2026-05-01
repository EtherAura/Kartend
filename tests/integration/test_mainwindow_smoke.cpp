#include "test_mainwindow_smoke.h"

#include "applicationmanager.h"
#include "artworkmanager.h"
#include "cachemanager.h"
#include "databasemanager.h"
#include "interactionmanager.h"
#include "mainwindow.h"
#include "mainwindowfixture.h"
#include "navigationmanager.h"
#include "scrollmanager.h"
#include "sessionmanager.h"
#include "settingsmanager.h"
#include "sidebarmanager.h"

#include <QDir>
#include <QStandardPaths>
#include <QString>
#include <QTest>

void TestMainWindowSmoke::testFixtureBuildsWithoutTouchingRealConfig() {
  KartendTest::MainWindowFixture fixture;
  QVERIFY(fixture.window() != nullptr);

  // setTestModeEnabled redirects ConfigLocation under ~/.qttest on Linux.
  // If real config ever leaked through, this path would not contain "qttest".
  const QString configRoot = KartendTest::MainWindowFixture::sandboxConfigPath();
  QVERIFY2(configRoot.contains(QStringLiteral("qttest")),
           qPrintable(QStringLiteral("ConfigLocation %1 escaped qttest sandbox")
                          .arg(configRoot)));
}

void TestMainWindowSmoke::testTopLevelManagersAreWired() {
  KartendTest::MainWindowFixture fixture;
  MainWindow *win = fixture.window();
  QVERIFY(win);

  QVERIFY(win->getApplicationManager());
  QVERIFY(win->getSettingsManager());
  QVERIFY(win->getSessionManager());
  QVERIFY(win->getCacheManager());
  QVERIFY(win->getArtworkManager());
  QVERIFY(win->getDatabaseManager());
  QVERIFY(win->getScrollManager());
  QVERIFY(win->getNavigationManager());
  QVERIFY(win->getInteractionManager());
  QVERIFY(win->getSidebarManager());
}
