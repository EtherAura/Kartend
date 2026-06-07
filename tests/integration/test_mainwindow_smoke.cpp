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

#include <QAction>
#include <QDir>
#include <QKeySequence>
#include <QList>
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

void TestMainWindowSmoke::testGridWidthAndZoomShortcutsAreDisambiguated() {
  // Regression guard for the grid add-column shortcut bug. On a US keyboard
  // '+' is Shift+'=', so text zoom-in's Ctrl++ (Ctrl+Plus) is the same
  // physical chord as grid add-column's Ctrl+Shift+= — binding both made the
  // chord ambiguous and Qt fired neither, silently breaking add-column. Lock
  // in the split: zoom-in is Ctrl+= only (never Ctrl+Plus); the Ctrl+Shift+=
  // family belongs to grid width.
  KartendTest::MainWindowFixture fixture;
  MainWindow *win = fixture.window();
  QVERIFY(win);

  const auto findAction = [win](const QString &text) -> QAction * {
    // Zoom + grid-width actions register via QWidget::addAction(); also scan
    // children so the lookup survives a future reparent.
    QList<QAction *> all = win->actions();
    all += win->findChildren<QAction *>();
    for (QAction *a : all) {
      if (a && a->text() == text) {
        return a;
      }
    }
    return nullptr;
  };

  QAction *zoomIn = findAction(QStringLiteral("Zoom Text In"));
  QAction *gridInc = findAction(QStringLiteral("Increase Grid Width"));
  QAction *gridDec = findAction(QStringLiteral("Decrease Grid Width"));
  QVERIFY2(zoomIn, "Zoom Text In action is not registered on the MainWindow");
  QVERIFY2(gridInc, "Increase Grid Width action is not registered on the MainWindow");
  QVERIFY2(gridDec, "Decrease Grid Width action is not registered on the MainWindow");

  const QKeySequence ctrlEqual(Qt::CTRL | Qt::Key_Equal);
  const QKeySequence ctrlPlus(Qt::CTRL | Qt::Key_Plus);
  const QKeySequence ctrlShiftPlus(Qt::CTRL | Qt::SHIFT | Qt::Key_Plus);
  const QKeySequence ctrlShiftEqual(Qt::CTRL | Qt::SHIFT | Qt::Key_Equal);
  const QKeySequence ctrlShiftMinus(Qt::CTRL | Qt::SHIFT | Qt::Key_Minus);

  // Zoom-in binds Ctrl+= and must never bind Ctrl+Plus (the collision source).
  QVERIFY2(zoomIn->shortcuts().contains(ctrlEqual), "zoom-in must bind Ctrl+=");
  QVERIFY2(!zoomIn->shortcuts().contains(ctrlPlus),
           "zoom-in must NOT bind Ctrl+Plus — on US keyboards it is the same "
           "chord as grid add-column Ctrl+Shift+= and makes both ambiguous");

  // Grid add-column keeps both Plus and Equal (US Shift+= and EU dedicated +);
  // remove-column stays on Ctrl+Shift+-.
  QVERIFY2(gridInc->shortcuts().contains(ctrlShiftPlus) &&
               gridInc->shortcuts().contains(ctrlShiftEqual),
           "grid add-column must bind both Ctrl+Shift+Plus and Ctrl+Shift+Equal");
  QVERIFY2(gridDec->shortcuts().contains(ctrlShiftMinus),
           "grid remove-column must bind Ctrl+Shift+Minus");

  // Defence in depth: the two actions must not literally share a sequence.
  const QList<QKeySequence> zoomSeqs = zoomIn->shortcuts();
  for (const QKeySequence &seq : zoomSeqs) {
    QVERIFY2(!gridInc->shortcuts().contains(seq),
             "zoom-in and grid add-column must not share a QKeySequence");
  }
}
