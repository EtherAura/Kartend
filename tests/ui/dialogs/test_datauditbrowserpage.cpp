// Smoke test for DatAuditBrowserPage construction (Kartend-34lab). The page's
// buildUi() wires the filter checkboxes' toggled→applyFilters() signal; setting
// a checkbox state at the wrong moment used to fire applyFilters() while a
// sibling checkbox was still null, crashing on construction. This guards that
// the whole page builds cleanly. QTEST_MAIN (real QApplication) is required —
// the page is a QWidget.

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QTest>
#include <QTreeView>

#include "datauditbrowserpage.h"

class TestDatAuditBrowserPage : public QObject {
  Q_OBJECT
private slots:
  void constructsWithoutCrash();
};

void TestDatAuditBrowserPage::constructsWithoutCrash() {
  // Construction alone runs buildUi() — the path that previously crashed.
  DatAuditBrowserPage page;
  // Complete / Partial / Empty / Fixes / MIA + the Group-by-folder toggle
  // (Kartend-m6qsb.30).
  QCOMPARE(page.findChildren<QCheckBox *>().size(), 6);
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

QTEST_MAIN(TestDatAuditBrowserPage)
#include "test_datauditbrowserpage.moc"
