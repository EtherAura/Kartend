// The titlebar colour drives the app's chrome tint (user request
// 2026-08-18). It comes from kdeglobals' [WM] activeBackground — NOT the
// accent colour, which differs under Plasma's accent-from-wallpaper and
// was why accent-tinted chrome never matched the titlebar.
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
};

void TestKdeColorSchemeTitlebar::activeTitlebarColor_parsesWmSection() {
  QStandardPaths::setTestModeEnabled(true);
  const QString configRoot = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
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
