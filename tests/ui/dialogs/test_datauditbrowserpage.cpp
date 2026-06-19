// Smoke test for DatAuditBrowserPage construction (Kartend-34lab). The page's
// buildUi() wires the filter checkboxes' toggled→applyFilters() signal; setting
// a checkbox state at the wrong moment used to fire applyFilters() while a
// sibling checkbox was still null, crashing on construction. This guards that
// the whole page builds cleanly. QTEST_MAIN (real QApplication) is required —
// the page is a QWidget.

#include <QApplication>
#include <QCheckBox>
#include <QSettings>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTest>

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
};

void TestDatAuditBrowserPage::constructsWithoutCrash() {
  // Construction alone runs buildUi() — the path that previously crashed.
  DatAuditBrowserPage page;
  // Complete / Partial / Empty / Fixes / MIA.
  QCOMPARE(page.findChildren<QCheckBox *>().size(), 5);
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

QTEST_MAIN(TestDatAuditBrowserPage)
#include "test_datauditbrowserpage.moc"
