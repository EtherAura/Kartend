// Headless CommandPaletteDialog behavior: caller-order listing on an empty
// query, fuzzy filtering + rank ordering as the user types, the "[Category]
// Label" rendering, keyboard navigation forwarded from the search field, and
// the Enter contract — accept the dialog FIRST, then run the selected
// command's callback resolved through the original-index UserRole stash (the
// visible row order is a sort artifact). The dialog is never exec()'d or
// shown; events are synthesized straight at the search field, matching the
// established offscreen dialog-test pattern.

#include "commandpalettedialog.h"

#include <QLineEdit>
#include <QListWidget>
#include <QTest>

class TestCommandPaletteDialog : public QObject {
  Q_OBJECT

private slots:
  void emptyQueryListsEveryCommandInCallerOrder();
  void queryDropsNonMatchesAndRanksBetterMatchesFirst();
  void categoryPrefixedRenderingAndBareLabels();
  void enterAcceptsThenRunsSelectedCommand();
  void arrowKeysMoveListSelectionFromSearchField();
  void enterWithNoMatchesIsANoOp();
};

namespace {

CommandPaletteDialog::Command cmd(const QString &category, const QString &label,
                                  int *counter = nullptr) {
  return {category, label,
          counter ? std::function<void()>([counter] { ++(*counter); }) : std::function<void()>()};
}

} // namespace

void TestCommandPaletteDialog::emptyQueryListsEveryCommandInCallerOrder() {
  CommandPaletteDialog dlg;
  dlg.setCommands({cmd(QStringLiteral("View"), QStringLiteral("Toggle fullscreen")),
                   cmd(QString(), QStringLiteral("Rescan library")),
                   cmd(QStringLiteral("Settings"), QStringLiteral("Open settings"))});

  auto *list = dlg.findChild<QListWidget *>();
  QVERIFY(list);
  // Empty query: every command scores 0, stable sort preserves the caller's
  // order — the caller puts the most useful entries up top.
  QCOMPARE(list->count(), 3);
  QCOMPARE(list->item(0)->text(), QStringLiteral("[View] Toggle fullscreen"));
  QCOMPARE(list->item(1)->text(), QStringLiteral("Rescan library"));
  QCOMPARE(list->item(2)->text(), QStringLiteral("[Settings] Open settings"));
  // First row is preselected so Enter always has a target.
  QCOMPARE(list->currentRow(), 0);
}

void TestCommandPaletteDialog::queryDropsNonMatchesAndRanksBetterMatchesFirst() {
  CommandPaletteDialog dlg;
  // Caller order puts "Grape" first. For the query "ap": "Apple" matches at a
  // word boundary with consecutive characters (high score); "Grape" only as a
  // scattered subsequence (a..p); "Melon" not at all.
  dlg.setCommands({cmd(QString(), QStringLiteral("Grape")), cmd(QString(), QStringLiteral("Apple")),
                   cmd(QString(), QStringLiteral("Melon"))});
  auto *edit = dlg.findChild<QLineEdit *>();
  auto *list = dlg.findChild<QListWidget *>();
  QVERIFY(edit && list);

  edit->setText(QStringLiteral("ap"));
  QCOMPARE(list->count(), 2); // Melon dropped
  QCOMPARE(list->item(0)->text(), QStringLiteral("Apple"));
  QCOMPARE(list->item(1)->text(), QStringLiteral("Grape"));

  // Clearing the query restores the full caller-ordered list.
  edit->clear();
  QCOMPARE(list->count(), 3);
  QCOMPARE(list->item(0)->text(), QStringLiteral("Grape"));
}

void TestCommandPaletteDialog::categoryPrefixedRenderingAndBareLabels() {
  CommandPaletteDialog dlg;
  dlg.setCommands({cmd(QStringLiteral("Collection"), QStringLiteral("Movies")),
                   cmd(QString(), QStringLiteral("Quit"))});
  auto *edit = dlg.findChild<QLineEdit *>();
  auto *list = dlg.findChild<QListWidget *>();
  QVERIFY(edit && list);
  QCOMPARE(list->item(0)->text(), QStringLiteral("[Collection] Movies"));
  QCOMPARE(list->item(1)->text(), QStringLiteral("Quit"));

  // The matcher sees "Category: Label", so typing the category side alone
  // still finds the command.
  edit->setText(QStringLiteral("collection"));
  QCOMPARE(list->count(), 1);
  QCOMPARE(list->item(0)->text(), QStringLiteral("[Collection] Movies"));
}

void TestCommandPaletteDialog::enterAcceptsThenRunsSelectedCommand() {
  CommandPaletteDialog dlg;
  int grapeRuns = 0;
  int appleRuns = 0;
  int resultInsideCallback = -1;
  // Apple is caller-index 1 but ranks to visible row 0 for "ap" — Enter must
  // resolve the ORIGINAL index through the UserRole stash, not the row number.
  // The dialog also contracts to accept() BEFORE invoking, so the action can
  // open its own modal without nesting under the palette.
  dlg.setCommands({cmd(QString(), QStringLiteral("Grape"), &grapeRuns),
                   {QString(), QStringLiteral("Apple"), [&dlg, &appleRuns, &resultInsideCallback] {
                      ++appleRuns;
                      resultInsideCallback = dlg.result();
                    }}});
  auto *edit = dlg.findChild<QLineEdit *>();
  QVERIFY(edit);

  edit->setText(QStringLiteral("ap"));
  QTest::keyClick(edit, Qt::Key_Return);

  QCOMPARE(appleRuns, 1);
  QCOMPARE(grapeRuns, 0);
  QCOMPARE(dlg.result(), static_cast<int>(QDialog::Accepted));
  QCOMPARE(resultInsideCallback, static_cast<int>(QDialog::Accepted)); // accept-before-run
}

void TestCommandPaletteDialog::arrowKeysMoveListSelectionFromSearchField() {
  CommandPaletteDialog dlg;
  int firstRuns = 0;
  int secondRuns = 0;
  dlg.setCommands({cmd(QString(), QStringLiteral("First"), &firstRuns),
                   cmd(QString(), QStringLiteral("Second"), &secondRuns)});
  auto *edit = dlg.findChild<QLineEdit *>();
  auto *list = dlg.findChild<QListWidget *>();
  QVERIFY(edit && list);
  QCOMPARE(list->currentRow(), 0);

  // Down/Up land on the search field but are forwarded to the list by the
  // event filter — the user never has to Tab away from typing.
  QTest::keyClick(edit, Qt::Key_Down);
  QCOMPARE(list->currentRow(), 1);
  QTest::keyClick(edit, Qt::Key_Up);
  QCOMPARE(list->currentRow(), 0);
  QTest::keyClick(edit, Qt::Key_Down);
  QTest::keyClick(edit, Qt::Key_Return);
  QCOMPARE(secondRuns, 1);
  QCOMPARE(firstRuns, 0);
}

void TestCommandPaletteDialog::enterWithNoMatchesIsANoOp() {
  CommandPaletteDialog dlg;
  int runs = 0;
  dlg.setCommands({cmd(QString(), QStringLiteral("Only command"), &runs)});
  auto *edit = dlg.findChild<QLineEdit *>();
  auto *list = dlg.findChild<QListWidget *>();
  QVERIFY(edit && list);

  edit->setText(QStringLiteral("zzz-no-such-command"));
  QCOMPARE(list->count(), 0);
  QTest::keyClick(edit, Qt::Key_Return);
  QCOMPARE(runs, 0);
  // The dialog must stay open — nothing was chosen.
  QVERIFY(dlg.result() != static_cast<int>(QDialog::Accepted));
}

QTEST_MAIN(TestCommandPaletteDialog)
#include "test_commandpalettedialog.moc"
