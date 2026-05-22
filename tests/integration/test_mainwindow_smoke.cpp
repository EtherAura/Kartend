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
#include "detailspanemanager.h"

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
  QVERIFY(win->getApplicationManager()->getSettingsManager());
  QVERIFY(win->getApplicationManager()->getSessionManager());
  QVERIFY(win->getApplicationManager()->getCacheManager());
  QVERIFY(win->getApplicationManager()->getArtworkManager());
  QVERIFY(win->getApplicationManager()->getDatabaseManager());
  QVERIFY(win->getApplicationManager()->getScrollManager());
  QVERIFY(win->getApplicationManager()->getNavigationManager());
  QVERIFY(win->getApplicationManager()->getInteractionManager());
  QVERIFY(win->getApplicationManager()->getDetailsPaneManager());
}
