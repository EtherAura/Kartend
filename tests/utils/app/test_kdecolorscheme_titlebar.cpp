// The titlebar colour drives the app's chrome tint (user request
// 2026-08-18). It is NOT the accent colour, which differs under Plasma's
// accent-from-wallpaper and was why accent-tinted chrome never matched the
// titlebar.
//
// Kartend-5w1zb: it comes from [Colors:Header] BackgroundNormal, with [WM]
// activeBackground as the fallback. Breeze has painted the decoration from
// the Header group since Plasma 5.23, so reading [WM] alone left the chrome a
// couple of levels off the titlebar it was supposed to match — measured
// 41,44,48 on the decoration against 39,44,49 in [WM] under Breeze Dark.
#include "kdecolorscheme.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTest>
#include <QTextStream>

class TestKdeColorSchemeTitlebar : public QObject {
  Q_OBJECT
private slots:
  void activeTitlebarColor_parsesWmSection();
  void activeTitlebarColor_prefersHeaderOverWm();
  void activeTitlebarTextColor_prefersHeaderOverWm();
};

namespace {
/// Write a kdeglobals into the test-mode config root and return its path.
QString writeGlobals(const QString &body) {
  QStandardPaths::setTestModeEnabled(true);
  const QString root = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
  QDir().mkpath(root);
  const QString path = root + QStringLiteral("/kdeglobals");
  QFile f(path);
  if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QTextStream(&f) << body;
  }
  return path;
}
} // namespace

void TestKdeColorSchemeTitlebar::activeTitlebarColor_prefersHeaderOverWm() {
  // Both groups present and DIFFERENT, which is the real Breeze case — the
  // two disagreeing is exactly the bug. Header must win; picking [WM] here is
  // the regression, and it is invisible to the eye at a 2/255 delta, so assert
  // it rather than trusting a screenshot.
  writeGlobals(QStringLiteral("[Colors:Header]\nBackgroundNormal=41,44,48\n\n"
                              "[WM]\nactiveBackground=39,44,49\n"));
  QCOMPARE(KdeColorScheme::activeTitlebarColor(), QColor(41, 44, 48));

  // The [Inactive] variant must not be mistaken for the active group: its
  // section line also begins "[Colors:Header", so a prefix match would take it.
  writeGlobals(QStringLiteral("[Colors:Header][Inactive]\nBackgroundNormal=1,2,3\n\n"
                              "[Colors:Header]\nBackgroundNormal=41,44,48\n"));
  QCOMPARE(KdeColorScheme::activeTitlebarColor(), QColor(41, 44, 48));
  QStandardPaths::setTestModeEnabled(false);
}

void TestKdeColorSchemeTitlebar::activeTitlebarTextColor_prefersHeaderOverWm() {
  writeGlobals(QStringLiteral("[Colors:Header]\nForegroundNormal=10,20,30\n\n"
                              "[WM]\nactiveForeground=252,252,252\n"));
  QCOMPARE(KdeColorScheme::activeTitlebarTextColor(), QColor(10, 20, 30));

  // No Header group at all — a pre-5.23 or hand-written scheme still resolves.
  writeGlobals(QStringLiteral("[WM]\nactiveForeground=252,252,252\n"));
  QCOMPARE(KdeColorScheme::activeTitlebarTextColor(), QColor(252, 252, 252));
  QStandardPaths::setTestModeEnabled(false);
}

void TestKdeColorSchemeTitlebar::activeTitlebarColor_parsesWmSection() {
  QStandardPaths::setTestModeEnabled(true);
  const QString configRoot =
      QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
  QDir().mkpath(configRoot);
  const QString path = configRoot + QStringLiteral("/kdeglobals");
  {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&f);
    // The exact shape Plasma writes, including the accent that must NOT
    // be mistaken for the titlebar.
    out << "[General]\nAccentColor=196,81,3\n\n"
        << "[WM]\nactiveBackground=146,67,13\nactiveForeground=255,255,255\n";
  }

  const QColor titlebar = KdeColorScheme::activeTitlebarColor();
  QVERIFY2(titlebar.isValid(), "a present [WM] activeBackground must parse");
  QCOMPARE(titlebar, QColor(146, 67, 13));
  QVERIFY2(titlebar != QColor(196, 81, 3), "the titlebar is not the accent colour");
  QStandardPaths::setTestModeEnabled(false);
}

QTEST_MAIN(TestKdeColorSchemeTitlebar)
#include "test_kdecolorscheme_titlebar.moc"
