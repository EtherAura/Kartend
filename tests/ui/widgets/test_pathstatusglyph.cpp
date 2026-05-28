// Kartend-liye: PathStatusGlyph attaches a trailing-action warning to a
// QLineEdit and toggles it based on the result of a path-status checker.
// These tests drive the helper directly with stubbed checkers so the unit
// test stays hermetic — no actual filesystem access.

#include "pathstatusglyph.h"
#include "pathutils.h"

#include <QAction>
#include <QApplication>
#include <QLineEdit>
#include <QTest>

class TestPathStatusGlyph : public QObject {
  Q_OBJECT

private slots:
  void hiddenOnOK();
  void hiddenOnEmpty();
  void visibleOnMissing();
  void visibleOnNotExecutable();
  void tooltipDescribesStatus();
  void textChangeFlipsVisibility();
  void reinstallReplacesChecker();
  void doesNotAccumulateGhostActions();
};

namespace {

QAction *findGlyph(QLineEdit *edit) {
  for (QAction *a : edit->actions()) {
    if (a->objectName() == QLatin1String("kartend.pathStatusGlyph")) {
      return a;
    }
  }
  return nullptr;
}

} // namespace

void TestPathStatusGlyph::hiddenOnOK() {
  QLineEdit edit;
  edit.setText(QStringLiteral("/some/path"));
  PathStatusGlyph::install(&edit, [](const QString &) { return PathUtils::PathStatus::OK; });
  QAction *a = findGlyph(&edit);
  QVERIFY(a);
  QVERIFY(!a->isVisible());
  QCOMPARE(a->toolTip(), QString());
}

void TestPathStatusGlyph::hiddenOnEmpty() {
  QLineEdit edit;
  // Empty input — status callback returns Empty (matches PathUtils contract).
  PathStatusGlyph::install(&edit, [](const QString &) { return PathUtils::PathStatus::Empty; });
  QAction *a = findGlyph(&edit);
  QVERIFY(a);
  QVERIFY(!a->isVisible());
}

void TestPathStatusGlyph::visibleOnMissing() {
  QLineEdit edit;
  edit.setText(QStringLiteral("/nope"));
  PathStatusGlyph::install(&edit, [](const QString &) { return PathUtils::PathStatus::Missing; });
  QAction *a = findGlyph(&edit);
  QVERIFY(a);
  QVERIFY(a->isVisible());
  QVERIFY(!a->toolTip().isEmpty());
}

void TestPathStatusGlyph::visibleOnNotExecutable() {
  QLineEdit edit;
  edit.setText(QStringLiteral("/bin/no-x"));
  PathStatusGlyph::install(&edit,
                           [](const QString &) { return PathUtils::PathStatus::NotExecutable; });
  QAction *a = findGlyph(&edit);
  QVERIFY(a);
  QVERIFY(a->isVisible());
}

void TestPathStatusGlyph::tooltipDescribesStatus() {
  QLineEdit edit;
  edit.setText(QStringLiteral("/bad"));
  PathStatusGlyph::install(&edit, [](const QString &) { return PathUtils::PathStatus::WrongType; });
  QAction *a = findGlyph(&edit);
  QVERIFY(a);
  QCOMPARE(a->toolTip(), PathUtils::pathStatusDescription(PathUtils::PathStatus::WrongType));
}

void TestPathStatusGlyph::textChangeFlipsVisibility() {
  QLineEdit edit;
  // Checker switches based on the text — "OK" → OK, "MISS" → Missing.
  PathStatusGlyph::install(&edit, [](const QString &text) {
    if (text == QLatin1String("OK")) return PathUtils::PathStatus::OK;
    if (text == QLatin1String("MISS")) return PathUtils::PathStatus::Missing;
    return PathUtils::PathStatus::Empty;
  });
  QAction *a = findGlyph(&edit);
  QVERIFY(a);
  QVERIFY(!a->isVisible()); // initial: Empty (default empty text)

  edit.setText(QStringLiteral("MISS"));
  QVERIFY(a->isVisible());

  edit.setText(QStringLiteral("OK"));
  QVERIFY(!a->isVisible());
}

void TestPathStatusGlyph::reinstallReplacesChecker() {
  QLineEdit edit;
  edit.setText(QStringLiteral("anything"));
  PathStatusGlyph::install(&edit, [](const QString &) { return PathUtils::PathStatus::Missing; });
  QAction *first = findGlyph(&edit);
  QVERIFY(first);
  QVERIFY(first->isVisible());

  // Reinstall with a checker that always returns OK; the action visibility
  // should flip immediately, and the count should not double.
  PathStatusGlyph::install(&edit, [](const QString &) { return PathUtils::PathStatus::OK; });
  QAction *second = findGlyph(&edit);
  QCOMPARE(first, second); // same action reused, no ghost
  QVERIFY(!second->isVisible());
}

void TestPathStatusGlyph::doesNotAccumulateGhostActions() {
  QLineEdit edit;
  edit.setText(QStringLiteral("/x"));
  for (int i = 0; i < 5; ++i) {
    PathStatusGlyph::install(&edit, [](const QString &) { return PathUtils::PathStatus::Missing; });
  }
  int count = 0;
  for (QAction *a : edit.actions()) {
    if (a->objectName() == QLatin1String("kartend.pathStatusGlyph")) ++count;
  }
  QCOMPARE(count, 1);
}

QTEST_MAIN(TestPathStatusGlyph)
#include "test_pathstatusglyph.moc"
