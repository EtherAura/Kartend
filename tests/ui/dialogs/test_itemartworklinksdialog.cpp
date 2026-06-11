// Kartend-0eeuk: ItemArtworkLinksDialog link-resolution mapping. The dialog
// is an in-memory snapshot editor (persistence is the caller's job), so the
// tests cover the map semantics: setOverrides/overrides() round trip with
// blank + whitespace rows dropped, auto-resolved paths fill only empty rows
// and are NOT promoted to overrides on read-back (freezing them would stop
// auto-discovery from following renames), a user edit over an auto value
// does become an override, the Clear action drops the row, and standard
// types render their friendly display name while custom types render raw.
// Driven headlessly — the dialog is never shown or exec()'d.

#include "itemartworklinksdialog.h"

#include "itemartwork.h"

#include <QApplication>
#include <QTableWidget>
#include <QTest>
#include <QToolButton>

namespace {

constexpr int kColumnLabel = 0;
constexpr int kColumnPath = 1;
constexpr int kColumnClear = 3;

QTableWidget *table(const QWidget &dlg) {
  return dlg.findChild<QTableWidget *>();
}

int rowWithLabel(const QTableWidget *t, const QString &label) {
  for (int row = 0; row < t->rowCount(); ++row) {
    if (t->item(row, kColumnLabel) && t->item(row, kColumnLabel)->text() == label) {
      return row;
    }
  }
  return -1;
}

} // namespace

class TestItemArtworkLinksDialog : public QObject {
  Q_OBJECT

private slots:
  void overridesRoundTripAndBlanksDrop();
  void autoResolvedPathsFillEmptyRowsButAreNotPromoted();
  void editingAnAutoFilledRowCreatesARealOverride();
  void clearButtonDropsTheOverride();
  void standardTypesRenderDisplayNamesCustomRenderRaw();
};

void TestItemArtworkLinksDialog::overridesRoundTripAndBlanksDrop() {
  ItemArtworkLinksDialog dlg;
  dlg.setTypeRows({QStringLiteral("front"), QStringLiteral("box")}, {QStringLiteral("disc-scan")});
  dlg.setOverrides({{QStringLiteral("front"), QStringLiteral("/art/front/a.png")},
                    {QStringLiteral("disc-scan"), QStringLiteral("/art/disc/a.png")}});

  QHash<QString, QString> out = dlg.overrides();
  QCOMPARE(out.size(), 2);
  QCOMPARE(out.value(QStringLiteral("front")), QStringLiteral("/art/front/a.png"));
  QCOMPARE(out.value(QStringLiteral("disc-scan")), QStringLiteral("/art/disc/a.png"));
  QVERIFY(!out.contains(QStringLiteral("box"))); // never set → absent, not empty

  // A whitespace-only edit counts as cleared — the caller diffs against the
  // original map, so a phantom "   " override would create a bogus DB row.
  QTableWidget *t = table(dlg);
  QVERIFY(t);
  const int frontRow =
      rowWithLabel(t, ItemArtworkStore::standardTypeDisplayName(QStringLiteral("front")));
  QVERIFY(frontRow >= 0);
  t->item(frontRow, kColumnPath)->setText(QStringLiteral("   "));
  out = dlg.overrides();
  QVERIFY(!out.contains(QStringLiteral("front")));
  QCOMPARE(out.size(), 1);
}

void TestItemArtworkLinksDialog::autoResolvedPathsFillEmptyRowsButAreNotPromoted() {
  ItemArtworkLinksDialog dlg;
  dlg.setTypeRows({QStringLiteral("front"), QStringLiteral("screenshot")}, {});
  dlg.setOverrides({{QStringLiteral("front"), QStringLiteral("/override/front.png")}});
  dlg.setAutoResolvedPaths(
      {{QStringLiteral("front"), QStringLiteral("/auto/front.png")},
       {QStringLiteral("screenshot"), QStringLiteral("/auto/screenshot.png")}});

  QTableWidget *t = table(dlg);
  QVERIFY(t);
  const int frontRow =
      rowWithLabel(t, ItemArtworkStore::standardTypeDisplayName(QStringLiteral("front")));
  const int shotRow =
      rowWithLabel(t, ItemArtworkStore::standardTypeDisplayName(QStringLiteral("screenshot")));
  QVERIFY(frontRow >= 0 && shotRow >= 0);

  // The saved override is never overwritten by the auto hint…
  QCOMPARE(t->item(frontRow, kColumnPath)->text(), QStringLiteral("/override/front.png"));
  // …while the empty row displays the auto value (italic hint).
  QCOMPARE(t->item(shotRow, kColumnPath)->text(), QStringLiteral("/auto/screenshot.png"));
  QVERIFY(t->item(shotRow, kColumnPath)->font().italic());

  // Read-back: the untouched auto row must NOT become a frozen override.
  const QHash<QString, QString> out = dlg.overrides();
  QCOMPARE(out.size(), 1);
  QCOMPARE(out.value(QStringLiteral("front")), QStringLiteral("/override/front.png"));
}

void TestItemArtworkLinksDialog::editingAnAutoFilledRowCreatesARealOverride() {
  ItemArtworkLinksDialog dlg;
  dlg.setTypeRows({QStringLiteral("screenshot")}, {});
  dlg.setOverrides({});
  dlg.setAutoResolvedPaths(
      {{QStringLiteral("screenshot"), QStringLiteral("/auto/screenshot.png")}});

  QTableWidget *t = table(dlg);
  QVERIFY(t);
  t->item(0, kColumnPath)->setText(QStringLiteral("/custom/screenshot.png"));

  const QHash<QString, QString> out = dlg.overrides();
  QCOMPARE(out.size(), 1);
  QCOMPARE(out.value(QStringLiteral("screenshot")), QStringLiteral("/custom/screenshot.png"));
}

void TestItemArtworkLinksDialog::clearButtonDropsTheOverride() {
  ItemArtworkLinksDialog dlg;
  dlg.setTypeRows({QStringLiteral("front")}, {});
  dlg.setOverrides({{QStringLiteral("front"), QStringLiteral("/art/front/a.png")}});
  QCOMPARE(dlg.overrides().size(), 1);

  QTableWidget *t = table(dlg);
  QVERIFY(t);
  auto *clear = qobject_cast<QToolButton *>(t->cellWidget(0, kColumnClear));
  QVERIFY(clear);
  clear->click();

  QVERIFY(dlg.overrides().isEmpty());
  QCOMPARE(t->item(0, kColumnPath)->text(), QString());
}

void TestItemArtworkLinksDialog::standardTypesRenderDisplayNamesCustomRenderRaw() {
  ItemArtworkLinksDialog dlg;
  dlg.setTypeRows({QStringLiteral("front")}, {QStringLiteral("disc-scan")});

  QTableWidget *t = table(dlg);
  QVERIFY(t);
  QCOMPARE(t->rowCount(), 2);

  const QString frontDisplay = ItemArtworkStore::standardTypeDisplayName(QStringLiteral("front"));
  QVERIFY(!frontDisplay.isEmpty());
  QCOMPARE(t->item(0, kColumnLabel)->text(), frontDisplay);
  // Custom types have no friendly-name registry yet — rendered as-is, with
  // the "no auto-discovery" hint on the label.
  QCOMPARE(t->item(1, kColumnLabel)->text(), QStringLiteral("disc-scan"));
  QVERIFY(t->item(1, kColumnLabel)->toolTip().contains(QStringLiteral("Custom type")));
}

QTEST_MAIN(TestItemArtworkLinksDialog)
#include "test_itemartworklinksdialog.moc"
