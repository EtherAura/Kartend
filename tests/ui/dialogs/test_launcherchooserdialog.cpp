// Kartend-0eeuk: LauncherChooserDialog choice mapping. Pins the
// default-index preselection rules (valid index wins, out-of-range or
// negative falls back to row 0), chosenIndex() mirroring the list row, the
// double-click-accepts wiring, and the static choose() wrapper contract:
// empty launcher list short-circuits to -1 without ever building a dialog,
// Cancel maps to -1, and accept returns the row selected at accept time.
// The exec()-based cases are answered by a zero-interval modal driver under
// the offscreen QPA.

#include "launcherchooserdialog.h"

#include <QApplication>
#include <QDialog>
#include <QListWidget>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

namespace {

/// Runs @p action on the next active modal dialog (repeating zero-interval
/// timer, so it survives the window between exec() entry and modal
/// activation).
class ModalDriver : public QObject {
public:
  explicit ModalDriver(std::function<void(QDialog *)> action) : m_action(std::move(action)) {
    m_timer.setInterval(0);
    connect(&m_timer, &QTimer::timeout, this, [this] {
      auto *dlg = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      if (!dlg) {
        return;
      }
      triggered = true;
      m_timer.stop();
      m_action(dlg);
    });
    m_timer.start();
  }

  bool triggered = false;

private:
  std::function<void(QDialog *)> m_action;
  QTimer m_timer;
};

const QStringList kNames{QStringLiteral("Default Player"), QStringLiteral("Alternate Player"),
                         QStringLiteral("Editor")};

} // namespace

class TestLauncherChooserDialog : public QObject {
  Q_OBJECT

private slots:
  void validDefaultIndexPreselectsThatRow();
  void invalidDefaultIndexFallsBackToFirstRow();
  void chosenIndexTracksCurrentRow();
  void doubleClickAcceptsDialog();
  void chooseWithEmptyListReturnsMinusOne();
  void chooseReturnsRowSelectedAtAccept();
  void chooseReturnsMinusOneOnCancel();
};

void TestLauncherChooserDialog::validDefaultIndexPreselectsThatRow() {
  LauncherChooserDialog dlg(nullptr, QStringLiteral("Concert Films"), kNames, 1);
  auto *list = dlg.findChild<QListWidget *>();
  QVERIFY(list);
  QCOMPARE(list->count(), 3);
  QCOMPARE(dlg.chosenIndex(), 1);
}

void TestLauncherChooserDialog::invalidDefaultIndexFallsBackToFirstRow() {
  // Out-of-range and negative defaults must both leave a sane selection —
  // Enter on a freshly-opened chooser launches launcher 0, never nothing.
  LauncherChooserDialog tooBig(nullptr, QString(), kNames, 7);
  QCOMPARE(tooBig.chosenIndex(), 0);

  LauncherChooserDialog negative(nullptr, QString(), kNames, -2);
  QCOMPARE(negative.chosenIndex(), 0);
}

void TestLauncherChooserDialog::chosenIndexTracksCurrentRow() {
  LauncherChooserDialog dlg(nullptr, QString(), kNames, 0);
  auto *list = dlg.findChild<QListWidget *>();
  QVERIFY(list);
  list->setCurrentRow(2);
  QCOMPARE(dlg.chosenIndex(), 2);
}

void TestLauncherChooserDialog::doubleClickAcceptsDialog() {
  LauncherChooserDialog dlg(nullptr, QString(), kNames, 0);
  auto *list = dlg.findChild<QListWidget *>();
  QVERIFY(list);
  QSignalSpy accepted(&dlg, &QDialog::accepted);
  // Double-clicking a row launches with that pick rather than only
  // selecting it.
  list->setCurrentRow(1);
  emit list->itemDoubleClicked(list->item(1));
  QCOMPARE(accepted.count(), 1);
  QCOMPARE(dlg.chosenIndex(), 1);
}

void TestLauncherChooserDialog::chooseWithEmptyListReturnsMinusOne() {
  // Must short-circuit without exec() — there is nothing to choose, and a
  // modal dialog here would hang a launch with zero launchers.
  QCOMPARE(LauncherChooserDialog::choose(nullptr, QStringLiteral("Empty"), {}, 0), -1);
}

void TestLauncherChooserDialog::chooseReturnsRowSelectedAtAccept() {
  ModalDriver driver([](QDialog *dlg) {
    auto *list = dlg->findChild<QListWidget *>();
    if (list) {
      list->setCurrentRow(2); // the user changes their mind before Launch
    }
    dlg->accept();
  });
  const int idx = LauncherChooserDialog::choose(nullptr, QStringLiteral("Concert Films"), kNames,
                                                /*defaultIndex=*/0);
  QVERIFY(driver.triggered);
  QCOMPARE(idx, 2);
}

void TestLauncherChooserDialog::chooseReturnsMinusOneOnCancel() {
  ModalDriver driver([](QDialog *dlg) { dlg->reject(); });
  const int idx = LauncherChooserDialog::choose(nullptr, QString(), kNames, 1);
  QVERIFY(driver.triggered);
  QCOMPARE(idx, -1);
}

QTEST_MAIN(TestLauncherChooserDialog)
#include "test_launcherchooserdialog.moc"
