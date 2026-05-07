// Tests for EmptyStateWidget contextual empty/loading state widget.
#include "emptystatewidget.h"

#include <QApplication>
#include <QLabel>
#include <QProgressBar>
#include <QSignalSpy>
#include <QTest>

class TestEmptyStateWidget : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void defaultsAreHiddenSubElements();
  void showMessageSetsPrimaryAndHint();
  void showMessageWithoutHintHidesHint();
  void showMessageWithoutIconHidesIcon();
  void showSearchEmptyShowsQueryInMessage();
  void showSearchEmptyHandlesEmptyQuery();
  void showNoMediaDirectoryShowsHint();
  void showScanningActivatesSpinner();
  void setProgressShowsBarWhenTotalPositive();
  void setProgressHidesBarWhenTotalZero();
  void setProgressClampsValues();
  void clearProgressHidesBar();
  void setSpinnerActiveTogglesAnimation();
  void setTextActsAsBackwardsCompatShim();
  void setSpinnerAngleStoresValue();
};

namespace {
QLabel *findLabelWithText(QWidget *root, const QString &text) {
  const auto labels = root->findChildren<QLabel *>();
  for (QLabel *label : labels) {
    if (label->text() == text) return label;
  }
  return nullptr;
}

QProgressBar *findProgressBar(QWidget *root) {
  return root->findChild<QProgressBar *>();
}
} // namespace

void TestEmptyStateWidget::initTestCase() {
  // Ensure a QApplication exists for widget creation.
  if (!qApp) {
    static int argc = 1;
    static char arg0[] = "test";
    static char *argv[] = {arg0, nullptr};
    new QApplication(argc, argv);
  }
}

void TestEmptyStateWidget::defaultsAreHiddenSubElements() {
  EmptyStateWidget w;
  QProgressBar *bar = findProgressBar(&w);
  QVERIFY(bar);
  QVERIFY(!bar->isVisibleTo(&w));
}

void TestEmptyStateWidget::showMessageSetsPrimaryAndHint() {
  EmptyStateWidget w;
  w.showMessage("Primary", "Hint", "📭");
  QVERIFY(findLabelWithText(&w, "Primary"));
  QVERIFY(findLabelWithText(&w, "Hint"));
  QVERIFY(findLabelWithText(&w, "📭"));
}

void TestEmptyStateWidget::showMessageWithoutHintHidesHint() {
  EmptyStateWidget w;
  w.showMessage("Primary", "Hint", "📭");
  w.showMessage("Just primary");
  QVERIFY(findLabelWithText(&w, "Just primary"));
  // Hint label should be cleared (no remaining "Hint" text)
  QVERIFY(!findLabelWithText(&w, "Hint"));
}

void TestEmptyStateWidget::showMessageWithoutIconHidesIcon() {
  EmptyStateWidget w;
  w.showMessage("Primary", QString(), "📭");
  QVERIFY(findLabelWithText(&w, "📭"));
  w.showMessage("Primary");
  QVERIFY(!findLabelWithText(&w, "📭"));
}

void TestEmptyStateWidget::showSearchEmptyShowsQueryInMessage() {
  EmptyStateWidget w;
  w.showSearchEmpty("mario");
  // Message should mention the query
  bool found = false;
  for (QLabel *label : w.findChildren<QLabel *>()) {
    if (label->text().contains("mario")) {
      found = true;
      break;
    }
  }
  QVERIFY(found);
}

void TestEmptyStateWidget::showSearchEmptyHandlesEmptyQuery() {
  EmptyStateWidget w;
  w.showSearchEmpty("   ");
  // Should still produce a message (no crash, no quoted empty)
  QVERIFY(!w.findChildren<QLabel *>().isEmpty());
}

void TestEmptyStateWidget::showNoMediaDirectoryShowsHint() {
  EmptyStateWidget w;
  w.showNoMediaDirectory();
  bool hasMessage = false;
  bool hasHint = false;
  for (QLabel *label : w.findChildren<QLabel *>()) {
    if (label->text().contains("media directory", Qt::CaseInsensitive)) hasMessage = true;
    if (label->text().contains("Settings", Qt::CaseInsensitive)) hasHint = true;
  }
  QVERIFY(hasMessage);
  QVERIFY(hasHint);
}

void TestEmptyStateWidget::showScanningActivatesSpinner() {
  EmptyStateWidget w;
  w.showScanning(0, 0);
  // Spinner widget should be visible (not the icon label)
  bool hasScanningText = false;
  for (QLabel *label : w.findChildren<QLabel *>()) {
    if (label->text().contains("Scanning", Qt::CaseInsensitive)) hasScanningText = true;
  }
  QVERIFY(hasScanningText);
}

void TestEmptyStateWidget::setProgressShowsBarWhenTotalPositive() {
  EmptyStateWidget w;
  w.show();
  w.showScanning(0, 0);
  w.setProgress(25, 100);
  QProgressBar *bar = findProgressBar(&w);
  QVERIFY(bar);
  QCOMPARE(bar->minimum(), 0);
  QCOMPARE(bar->maximum(), 100);
  QCOMPARE(bar->value(), 25);
}

void TestEmptyStateWidget::setProgressHidesBarWhenTotalZero() {
  EmptyStateWidget w;
  w.show();
  w.showScanning(0, 100);
  w.setProgress(0, 0);
  QProgressBar *bar = findProgressBar(&w);
  QVERIFY(bar);
  QVERIFY(!bar->isVisibleTo(&w));
}

void TestEmptyStateWidget::setProgressClampsValues() {
  EmptyStateWidget w;
  w.showScanning();
  w.setProgress(150, 100);
  QProgressBar *bar = findProgressBar(&w);
  QCOMPARE(bar->value(), 100);

  w.setProgress(-5, 100);
  QCOMPARE(bar->value(), 0);
}

void TestEmptyStateWidget::clearProgressHidesBar() {
  EmptyStateWidget w;
  w.show();
  w.showScanning(50, 100);
  QProgressBar *bar = findProgressBar(&w);
  QVERIFY(bar->isVisibleTo(&w));
  w.clearProgress();
  QVERIFY(!bar->isVisibleTo(&w));
}

void TestEmptyStateWidget::setSpinnerActiveTogglesAnimation() {
  EmptyStateWidget w;
  w.setSpinnerActive(true);
  // Setting an angle should not crash, indicating spinner widget exists
  w.setSpinnerAngle(180);
  QCOMPARE(w.spinnerAngle(), 180);
  w.setSpinnerActive(false);
  // No crash on toggling off
}

void TestEmptyStateWidget::setTextActsAsBackwardsCompatShim() {
  EmptyStateWidget w;
  w.showMessage("Old", "Old hint", "📭");
  w.setText("New text only");
  QVERIFY(findLabelWithText(&w, "New text only"));
  // Hint and icon should be gone after setText (acts as shim that resets)
  QVERIFY(!findLabelWithText(&w, "Old hint"));
  QVERIFY(!findLabelWithText(&w, "📭"));
}

void TestEmptyStateWidget::setSpinnerAngleStoresValue() {
  EmptyStateWidget w;
  w.setSpinnerAngle(45);
  QCOMPARE(w.spinnerAngle(), 45);
  w.setSpinnerAngle(270);
  QCOMPARE(w.spinnerAngle(), 270);
}

QTEST_MAIN(TestEmptyStateWidget)
#include "test_emptystatewidget.moc"
