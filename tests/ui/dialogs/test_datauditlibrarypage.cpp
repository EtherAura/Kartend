// DatAuditLibraryPage — the DAT Manager's "DAT library" stacked page, a pure
// view (Kartend-oa0lu): the dialog drives state through setters and listens
// to intent signals. Covered: the empty-path placeholder round-trip, the
// review/check-updates/import gating setters (including the busy relabel),
// and every button→intent-signal wire. Constructed headlessly, never shown —
// visibility assertions read the explicit hidden flag.

#include "datauditlibrarypage.h"

#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

namespace {

QPushButton *buttonWithText(const QWidget &root, const QString &text) {
  const auto buttons = root.findChildren<QPushButton *>();
  for (auto *b : buttons) {
    if (b->text() == text) {
      return b;
    }
  }
  return nullptr;
}

bool hasLabelWithText(const QWidget &root, const QString &text) {
  const auto labels = root.findChildren<QLabel *>();
  for (const auto *l : labels) {
    if (l->text() == text) {
      return true;
    }
  }
  return false;
}

} // namespace

class TestDatAuditLibraryPage : public QObject {
  Q_OBJECT

private slots:
  void constructsWithGatedDefaults();
  void libraryPathTextRoundTripsThroughPlaceholder();
  void checkUpdatesBusyDisablesAndRelabels();
  void visibilityAndEnableSettersDriveTheirWidgets();
  void buttonsEmitTheirIntentSignals();
};

void TestDatAuditLibraryPage::constructsWithGatedDefaults() {
  DatAuditLibraryPage page;
  // Placeholder until the dialog pushes a real path.
  QVERIFY(hasLabelWithText(page, QStringLiteral("No library folder set.")));
  // Review exists but is disabled until a controller hooks it up; the
  // update-check and import buttons stay hidden until gated on.
  QPushButton *review = buttonWithText(page, QStringLiteral("Scan & review proposals…"));
  QVERIFY(review);
  QVERIFY(!review->isEnabled());
  QVERIFY(buttonWithText(page, QStringLiteral("Check for updates…"))->isHidden());
  QVERIFY(buttonWithText(page, QStringLiteral("Import DAT zip…"))->isHidden());
  QVERIFY(buttonWithText(page, QStringLiteral("Import DAT folder…"))->isHidden());
}

void TestDatAuditLibraryPage::libraryPathTextRoundTripsThroughPlaceholder() {
  DatAuditLibraryPage page;
  page.setLibraryPathText(QStringLiteral("/library/dats"));
  QVERIFY(hasLabelWithText(page, QStringLiteral("/library/dats")));
  QVERIFY(!hasLabelWithText(page, QStringLiteral("No library folder set.")));
  // Clearing the path restores the placeholder rather than a blank label.
  page.setLibraryPathText(QString());
  QVERIFY(hasLabelWithText(page, QStringLiteral("No library folder set.")));
}

void TestDatAuditLibraryPage::checkUpdatesBusyDisablesAndRelabels() {
  DatAuditLibraryPage page;
  page.setCheckUpdatesVisible(true);
  QPushButton *check = buttonWithText(page, QStringLiteral("Check for updates…"));
  QVERIFY(check);
  QVERIFY(!check->isHidden());

  page.setCheckUpdatesBusy(true);
  QVERIFY(!check->isEnabled());
  QCOMPARE(check->text(), QStringLiteral("Checking…"));

  page.setCheckUpdatesBusy(false);
  QVERIFY(check->isEnabled());
  QCOMPARE(check->text(), QStringLiteral("Check for updates…"));
}

void TestDatAuditLibraryPage::visibilityAndEnableSettersDriveTheirWidgets() {
  DatAuditLibraryPage page;
  QPushButton *review = buttonWithText(page, QStringLiteral("Scan & review proposals…"));
  QPushButton *importZip = buttonWithText(page, QStringLiteral("Import DAT zip…"));
  QPushButton *importFolder = buttonWithText(page, QStringLiteral("Import DAT folder…"));
  QVERIFY(review && importZip && importFolder);

  page.setReviewEnabled(true);
  QVERIFY(review->isEnabled());
  page.setReviewEnabled(false);
  QVERIFY(!review->isEnabled());

  page.setImportButtonsVisible(true);
  QVERIFY(!importZip->isHidden());
  QVERIFY(!importFolder->isHidden());
  page.setImportButtonsVisible(false);
  QVERIFY(importZip->isHidden());
  QVERIFY(importFolder->isHidden());
}

void TestDatAuditLibraryPage::buttonsEmitTheirIntentSignals() {
  DatAuditLibraryPage page;
  // Review starts disabled (QAbstractButton::click() respects the enabled
  // state); the hidden buttons stay clickable — visibility is a layout
  // concern, not a gate.
  page.setReviewEnabled(true);
  QSignalSpy reviewSpy(&page, &DatAuditLibraryPage::reviewRequested);
  QSignalSpy checkSpy(&page, &DatAuditLibraryPage::checkUpdatesRequested);
  QSignalSpy zipSpy(&page, &DatAuditLibraryPage::importZipRequested);
  QSignalSpy folderSpy(&page, &DatAuditLibraryPage::importFolderRequested);

  buttonWithText(page, QStringLiteral("Scan & review proposals…"))->click();
  buttonWithText(page, QStringLiteral("Check for updates…"))->click();
  buttonWithText(page, QStringLiteral("Import DAT zip…"))->click();
  buttonWithText(page, QStringLiteral("Import DAT folder…"))->click();

  QCOMPARE(reviewSpy.count(), 1);
  QCOMPARE(checkSpy.count(), 1);
  QCOMPARE(zipSpy.count(), 1);
  QCOMPARE(folderSpy.count(), 1);
}

QTEST_MAIN(TestDatAuditLibraryPage)
#include "test_datauditlibrarypage.moc"
