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
#include <QSet>
#include <QSettings>
#include <QSplitter>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeView>

#include "datauditbrowserpage.h"

namespace {
// The page exposes no getters; locate each filter checkbox by its label.
QCheckBox *checkbox(const QWidget &page, const QString &text) {
  const auto boxes = page.findChildren<QCheckBox *>();
  for (auto *box : boxes) {
    if (box->text() == text) {
      return box;
    }
  }
  return nullptr;
}
} // namespace

class TestDatAuditBrowserPage : public QObject {
  Q_OBJECT
private slots:
  void constructsWithoutCrash();
  void persistsAndRestoresLayout();
  void roundTripsExpandedTreeNodes();
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

// Kartend-o46gy: persistState()/restoreState_() round-trip the filter checkbox
// states and write the splitter/column layout keys, so the page reopens with
// the user's adjustments instead of default proportions.
void TestDatAuditBrowserPage::persistsAndRestoresLayout() {
  // Redirect QSettings to a throwaway dir so the test never touches the real
  // user config (the page uses QSettings("kartend", "ui-state")).
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, tmp.path());

  // Flip every checkbox off its build-time default (Complete/Partial/Empty on,
  // Fixes/MIA off) so a restore can't pass by coincidence.
  {
    DatAuditBrowserPage page;
    checkbox(page, "Complete")->setChecked(false);
    checkbox(page, "Partial")->setChecked(false);
    checkbox(page, "Empty")->setChecked(false);
    checkbox(page, "Fixes")->setChecked(true);
    checkbox(page, "MIA")->setChecked(true);
    page.persistState();
  }

  // A fresh page starts at the defaults; restoreState_() must reload the flips.
  {
    DatAuditBrowserPage page;
    QCOMPARE(checkbox(page, "Complete")->isChecked(), true); // default pre-restore
    page.restoreState_();
    QCOMPARE(checkbox(page, "Complete")->isChecked(), false);
    QCOMPARE(checkbox(page, "Partial")->isChecked(), false);
    QCOMPARE(checkbox(page, "Empty")->isChecked(), false);
    QCOMPARE(checkbox(page, "Fixes")->isChecked(), true);
    QCOMPARE(checkbox(page, "MIA")->isChecked(), true);
  }

  // The splitter/column layout keys are written and non-empty (geometry values
  // depend on the shown size, so assert presence rather than exact bytes).
  QSettings settings(QStringLiteral("kartend"), QStringLiteral("ui-state"));
  settings.beginGroup(QStringLiteral("DatManagerWindow"));
  for (const auto &key :
       {QStringLiteral("browserHSplitter"), QStringLiteral("browserVSplitter"),
        QStringLiteral("browserGameHeader"), QStringLiteral("browserRomHeader")}) {
    QVERIFY2(!settings.value(key).toByteArray().isEmpty(), qPrintable(key));
  }
}

// Kartend-q66m4: the expanded tree-node set (profile ids) round-trips through
// QSettings. Re-applying it to a live tree needs a DB-populated multi-source
// tree (runtime-verified); here we cover the load/serialize logic — that the
// key name matches and the QStringList<->QSet<qint64> conversion survives a
// restore->persist cycle.
void TestDatAuditBrowserPage::roundTripsExpandedTreeNodes() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, tmp.path());

  // Seed two expanded profile ids directly.
  {
    QSettings settings(QStringLiteral("kartend"), QStringLiteral("ui-state"));
    settings.beginGroup(QStringLiteral("DatManagerWindow"));
    settings.setValue(QStringLiteral("browserExpandedProfiles"), QStringList{"7", "42"});
    settings.endGroup();
  }

  // restoreState_() parses them into the set; persistState() writes them back.
  {
    DatAuditBrowserPage page;
    page.restoreState_();
    page.persistState();
  }

  QSettings settings(QStringLiteral("kartend"), QStringLiteral("ui-state"));
  settings.beginGroup(QStringLiteral("DatManagerWindow"));
  const QStringList ids = settings.value(QStringLiteral("browserExpandedProfiles")).toStringList();
  // QSet iteration order is unspecified, so compare as sets.
  QCOMPARE(QSet<QString>(ids.begin(), ids.end()), (QSet<QString>{"7", "42"}));
}

QTEST_MAIN(TestDatAuditBrowserPage)
#include "test_datauditbrowserpage.moc"
