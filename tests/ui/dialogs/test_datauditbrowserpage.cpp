// Smoke test for DatAuditBrowserPage construction (Kartend-34lab). The page's
// buildUi() wires the filter checkboxes' toggled→applyFilters() signal; setting
// a checkbox state at the wrong moment used to fire applyFilters() while a
// sibling checkbox was still null, crashing on construction. This guards that
// the whole page builds cleanly. QTEST_MAIN (real QApplication) is required —
// the page is a QWidget.

#include <QApplication>
#include <QCheckBox>
#include <QTest>

#include "datauditbrowserpage.h"

class TestDatAuditBrowserPage : public QObject {
  Q_OBJECT
private slots:
  void constructsWithoutCrash();
};

void TestDatAuditBrowserPage::constructsWithoutCrash() {
  // Construction alone runs buildUi() — the path that previously crashed.
  DatAuditBrowserPage page;
  // Complete / Partial / Empty / Fixes / MIA.
  QCOMPARE(page.findChildren<QCheckBox *>().size(), 5);
}

QTEST_MAIN(TestDatAuditBrowserPage)
#include "test_datauditbrowserpage.moc"
