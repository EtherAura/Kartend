// Kartend-0eeuk: BulkEditDialog dialog-to-store mapping. The store half
// (BulkEdit::applyAction) is covered by tests/utils/db/test_bulkedit.cpp;
// these tests cover the dialog side: the action combo carries the full
// BulkEdit::Action set as userData (so dispatch survives row reordering),
// the parameter field is gated on actionRequiresParameter, Apply is blocked
// while a tag action has a blank parameter, and result() hands the caller
// the chosen action + trimmed parameter. Driven headlessly — the dialog is
// never shown or exec()'d.

#include "bulkeditdialog.h"

#include "bulkedit.h"

#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTest>

namespace {

QComboBox *actionCombo(const QWidget &dlg) {
  return dlg.findChild<QComboBox *>();
}

QLineEdit *parameterEdit(const QWidget &dlg) {
  return dlg.findChild<QLineEdit *>();
}

QPushButton *applyButton(const QWidget &dlg) {
  const auto buttons = dlg.findChildren<QPushButton *>();
  for (QPushButton *b : buttons) {
    if (b->text() == QStringLiteral("Apply")) {
      return b;
    }
  }
  return nullptr;
}

void selectAction(QComboBox *combo, BulkEdit::Action action) {
  const int idx = combo->findData(int(action));
  QVERIFY(idx >= 0);
  combo->setCurrentIndex(idx);
}

} // namespace

class TestBulkEditDialog : public QObject {
  Q_OBJECT

private slots:
  void comboCarriesEveryActionAsUserData();
  void tagActionGatesApplyOnParameter();
  void whitespaceOnlyParameterKeepsApplyDisabled();
  void applyMapsActionAndTrimmedParameter();
  void flagActionDisablesAndClearsParameter();
  void switchingBackToTagActionRequiresFreshParameter();
};

void TestBulkEditDialog::comboCarriesEveryActionAsUserData() {
  BulkEditDialog dlg;
  QComboBox *combo = actionCombo(dlg);
  QVERIFY(combo);

  // Every action must be present exactly once, with the enum in userData and
  // the canonical label as display text. The order is the dialog's choice;
  // dispatch must not depend on it, so we only assert set membership.
  const QList<BulkEdit::Action> expected{BulkEdit::Action::AddTag,
                                         BulkEdit::Action::RemoveTag,
                                         BulkEdit::Action::MarkPinned,
                                         BulkEdit::Action::UnmarkPinned,
                                         BulkEdit::Action::MarkHidden,
                                         BulkEdit::Action::UnmarkHidden,
                                         BulkEdit::Action::MarkContinueLater,
                                         BulkEdit::Action::UnmarkContinueLater,
                                         BulkEdit::Action::ClearRating};
  QCOMPARE(combo->count(), expected.size());
  for (BulkEdit::Action action : expected) {
    const int idx = combo->findData(int(action));
    QVERIFY2(idx >= 0, qPrintable(BulkEdit::actionLabel(action)));
    QCOMPARE(combo->itemText(idx), BulkEdit::actionLabel(action));
  }
}

void TestBulkEditDialog::tagActionGatesApplyOnParameter() {
  BulkEditDialog dlg;
  QComboBox *combo = actionCombo(dlg);
  QLineEdit *param = parameterEdit(dlg);
  QPushButton *apply = applyButton(dlg);
  QVERIFY(combo && param && apply);

  selectAction(combo, BulkEdit::Action::AddTag);
  QVERIFY(param->isEnabled());
  QVERIFY(!apply->isEnabled()); // blank tag would silently no-op every row

  param->setText(QStringLiteral("live"));
  QVERIFY(apply->isEnabled());

  param->clear();
  QVERIFY(!apply->isEnabled()); // gate re-engages when the text is removed
}

void TestBulkEditDialog::whitespaceOnlyParameterKeepsApplyDisabled() {
  BulkEditDialog dlg;
  QComboBox *combo = actionCombo(dlg);
  QLineEdit *param = parameterEdit(dlg);
  QPushButton *apply = applyButton(dlg);
  QVERIFY(combo && param && apply);

  selectAction(combo, BulkEdit::Action::RemoveTag);
  param->setText(QStringLiteral("   "));
  QVERIFY(!apply->isEnabled()); // trims before judging emptiness
}

void TestBulkEditDialog::applyMapsActionAndTrimmedParameter() {
  BulkEditDialog dlg;
  QComboBox *combo = actionCombo(dlg);
  QLineEdit *param = parameterEdit(dlg);
  QPushButton *apply = applyButton(dlg);
  QVERIFY(combo && param && apply);

  selectAction(combo, BulkEdit::Action::RemoveTag);
  param->setText(QStringLiteral("  jazz  "));
  QVERIFY(apply->isEnabled());
  apply->click();

  QCOMPARE(dlg.result().action, BulkEdit::Action::RemoveTag);
  QCOMPARE(dlg.result().parameter, QStringLiteral("jazz")); // trimmed
  QCOMPARE(static_cast<QDialog &>(dlg).result(), int(QDialog::Accepted));
}

void TestBulkEditDialog::flagActionDisablesAndClearsParameter() {
  BulkEditDialog dlg;
  QComboBox *combo = actionCombo(dlg);
  QLineEdit *param = parameterEdit(dlg);
  QPushButton *apply = applyButton(dlg);
  QVERIFY(combo && param && apply);

  // Type a tag first, then switch to a flag action: the stale parameter must
  // be cleared so it can't leak into result() for an action that ignores it.
  selectAction(combo, BulkEdit::Action::AddTag);
  param->setText(QStringLiteral("live"));
  selectAction(combo, BulkEdit::Action::MarkPinned);

  QVERIFY(!param->isEnabled());
  QVERIFY(param->text().isEmpty());
  QVERIFY(apply->isEnabled()); // no parameter needed → apply available

  apply->click();
  QCOMPARE(dlg.result().action, BulkEdit::Action::MarkPinned);
  QVERIFY(dlg.result().parameter.isEmpty());
}

void TestBulkEditDialog::switchingBackToTagActionRequiresFreshParameter() {
  BulkEditDialog dlg;
  QComboBox *combo = actionCombo(dlg);
  QLineEdit *param = parameterEdit(dlg);
  QPushButton *apply = applyButton(dlg);
  QVERIFY(combo && param && apply);

  selectAction(combo, BulkEdit::Action::ClearRating);
  QVERIFY(apply->isEnabled());

  // Back to a tag action: the field was cleared on the way out, so Apply
  // must drop back to disabled instead of accepting the empty parameter.
  selectAction(combo, BulkEdit::Action::AddTag);
  QVERIFY(param->isEnabled());
  QVERIFY(param->text().isEmpty());
  QVERIFY(!apply->isEnabled());
}

QTEST_MAIN(TestBulkEditDialog)
#include "test_bulkeditdialog.moc"
