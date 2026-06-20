// Smoke test for DatAuditBrowserPage construction (Kartend-34lab). The page's
// buildUi() wires the filter checkboxes' toggled→applyFilters() signal; setting
// a checkbox state at the wrong moment used to fire applyFilters() while a
// sibling checkbox was still null, crashing on construction. This guards that
// the whole page builds cleanly. QTEST_MAIN (real QApplication) is required —
// the page is a QWidget.

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTest>
#include <QTreeView>

#include "datauditbrowserpage.h"

class TestDatAuditBrowserPage : public QObject {
  Q_OBJECT
private slots:
  void constructsWithoutCrash();
  void auditProgressBarTogglesAndFills();
};

void TestDatAuditBrowserPage::constructsWithoutCrash() {
  // Construction alone runs buildUi() — the path that previously crashed.
  DatAuditBrowserPage page;
  // Complete / Partial / Empty / Fixes / MIA + Group-by-folder (Kartend-m6qsb.30)
  // + Group-by-category (Kartend-7iqhl.5).
  QCOMPARE(page.findChildren<QCheckBox *>().size(), 7);
  // Named view presets (Kartend-7iqhl.1): a combo of 4 slots loaded from
  // QSettings on construction, plus Save and Rename buttons. loadPresets()
  // blocks the combo while filling it, so construction must not recall a preset
  // (the page opens at its defaults) — a crash there would surface here too.
  const auto combos = page.findChildren<QComboBox *>();
  QCOMPARE(combos.size(), 1);
  QCOMPARE(combos.first()->count(), 4);
  QCOMPARE(page.findChildren<QPushButton *>().size(), 2); // Save + Rename
  // Write-actions (Kartend-7iqhl.2): the tree opts into a custom context menu so
  // right-click can offer profile-scoped Re-audit / Fix.
  const auto trees = page.findChildren<QTreeView *>();
  QCOMPARE(trees.size(), 1);
  QCOMPARE(trees.first()->contextMenuPolicy(), Qt::CustomContextMenu);
}

void TestDatAuditBrowserPage::auditProgressBarTogglesAndFills() {
  // The browser-initiated re-audit progress bar (Kartend-7iqhl.3): hidden by
  // default, shown while running, fills on ticks, hides when done. isHidden()
  // reflects the explicit hide flag (the page itself is never shown here).
  DatAuditBrowserPage page;
  const auto bars = page.findChildren<QProgressBar *>();
  QCOMPARE(bars.size(), 1);
  QProgressBar *bar = bars.first();
  QVERIFY(bar->isHidden());

  page.setAuditRunning(true);
  QVERIFY(!bar->isHidden());
  QCOMPARE(bar->maximum(), 0); // indeterminate until the first real tick

  page.setAuditProgress(3, 10);
  QCOMPARE(bar->maximum(), 10);
  QCOMPARE(bar->value(), 3);

  page.setAuditProgress(0, 0); // a 0-total tick must not blow away the range
  QCOMPARE(bar->maximum(), 10);

  page.setAuditRunning(false);
  QVERIFY(bar->isHidden());
}

QTEST_MAIN(TestDatAuditBrowserPage)
#include "test_datauditbrowserpage.moc"
