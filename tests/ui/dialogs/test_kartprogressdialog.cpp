// Kartend-0eeuk: KartProgressDialog progress/cancel state machine. Covers
// the fraction → bar-value clamp, the one-shot cancel arm (click emits
// cancelRequested once, disables the button, flips the label to
// "Cancelling..."), and the markFinished repurpose: the bar saturates and
// the same button becomes an accept-only Close that must NOT re-emit
// cancelRequested. Driven headlessly — the dialog is never shown or
// exec()'d.

#include "kartprogressdialog.h"

#include <QApplication>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

namespace {

QLabel *entryLabel(const QWidget &dlg) {
  // Two labels exist: the bold title and the entry label below it. The
  // entry label is the one that starts at "Preparing...".
  const auto labels = dlg.findChildren<QLabel *>();
  for (QLabel *l : labels) {
    if (l->wordWrap()) {
      return l;
    }
  }
  return nullptr;
}

} // namespace

class TestKartProgressDialog : public QObject {
  Q_OBJECT

private slots:
  void fractionClampsIntoBarRange();
  void cancelClickArmsOnceAndLocksTheButton();
  void markFinishedTurnsCancelIntoAcceptOnlyClose();
};

void TestKartProgressDialog::fractionClampsIntoBarRange() {
  KartProgressDialog dlg(QStringLiteral("Importing"));
  auto *bar = dlg.findChild<QProgressBar *>();
  QVERIFY(bar);
  QCOMPARE(bar->value(), 0);

  dlg.setFraction(0.5);
  QCOMPARE(bar->value(), 500);
  dlg.setFraction(-0.25); // a confused caller must not underflow the bar
  QCOMPARE(bar->value(), 0);
  dlg.setFraction(1.75); // ...nor overflow it
  QCOMPARE(bar->value(), 1000);
}

void TestKartProgressDialog::cancelClickArmsOnceAndLocksTheButton() {
  KartProgressDialog dlg(QStringLiteral("Importing"));
  auto *button = dlg.findChild<QPushButton *>();
  QLabel *entry = entryLabel(dlg);
  QVERIFY(button && entry);

  dlg.setEntryName(QStringLiteral("media/a.mkv"));
  QCOMPARE(entry->text(), QStringLiteral("media/a.mkv"));

  QSignalSpy cancelled(&dlg, &KartProgressDialog::cancelRequested);
  button->click();
  QCOMPARE(cancelled.count(), 1);
  QVERIFY(!button->isEnabled()); // double-cancel is impossible
  QCOMPARE(entry->text(), QStringLiteral("Cancelling..."));
}

void TestKartProgressDialog::markFinishedTurnsCancelIntoAcceptOnlyClose() {
  KartProgressDialog dlg(QStringLiteral("Importing"));
  auto *button = dlg.findChild<QPushButton *>();
  auto *bar = dlg.findChild<QProgressBar *>();
  QVERIFY(button && bar);

  QSignalSpy cancelled(&dlg, &KartProgressDialog::cancelRequested);
  QSignalSpy accepted(&dlg, &QDialog::accepted);

  // Cancel mid-run, then the worker drains and finishes anyway — the
  // button must come back as Close and the old cancel wiring must be gone.
  button->click();
  QCOMPARE(cancelled.count(), 1);

  dlg.markFinished();
  QCOMPARE(bar->value(), 1000); // saturated regardless of last fraction
  QCOMPARE(button->text(), QStringLiteral("Close"));
  QVERIFY(button->isEnabled());

  button->click();
  QCOMPARE(accepted.count(), 1);
  QCOMPARE(cancelled.count(), 1); // Close must not re-fire cancelRequested
}

QTEST_MAIN(TestKartProgressDialog)
#include "test_kartprogressdialog.moc"
