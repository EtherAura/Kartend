// Kartend-0eeuk: CollectionPickerDialog index/tree mapping. Covers the
// hierarchy mirror (parent links → nested tree rows), exclusion semantics
// (excluded indices vanish and their children re-attach at the nearest
// visible ancestor), the initialChecked seed → sorted selectedIndices()
// round trip, the bidirectional cascade (parent toggle flips the subtree,
// child disagreement leaves the parent partial and OUT of the selection),
// Select All / None, and orphaned parent links surfacing at the root.
// Driven headlessly — the dialog is never shown or exec()'d.

#include "collectionpickerdialog.h"

#include "collection/collectionconfig.h"

#include <QApplication>
#include <QPushButton>
#include <QTest>
#include <QTreeWidget>

namespace {

CollectionConfig makeCol(const QString &name, int parent) {
  CollectionConfig c;
  c.name = name;
  c.parentCollectionIndex = parent;
  return c;
}

/// A: { A1: { A1a }, A2 }, B — the canonical fixture for these tests.
QList<CollectionConfig> sampleTree() {
  return {makeCol(QStringLiteral("A"), -1), makeCol(QStringLiteral("A1"), 0),
          makeCol(QStringLiteral("A1a"), 1), makeCol(QStringLiteral("A2"), 0),
          makeCol(QStringLiteral("B"), -1)};
}

QTreeWidgetItem *findItem(QTreeWidgetItem *root, const QString &name) {
  if (root->text(0) == name) {
    return root;
  }
  for (int i = 0; i < root->childCount(); ++i) {
    if (QTreeWidgetItem *hit = findItem(root->child(i), name)) {
      return hit;
    }
  }
  return nullptr;
}

QTreeWidgetItem *findItem(const QTreeWidget *tree, const QString &name) {
  for (int i = 0; i < tree->topLevelItemCount(); ++i) {
    if (QTreeWidgetItem *hit = findItem(tree->topLevelItem(i), name)) {
      return hit;
    }
  }
  return nullptr;
}

QStringList childNames(const QTreeWidgetItem *item) {
  QStringList out;
  for (int i = 0; i < item->childCount(); ++i) {
    out.append(item->child(i)->text(0));
  }
  out.sort();
  return out;
}

QPushButton *buttonWithText(const QWidget &dlg, const QString &text) {
  const auto buttons = dlg.findChildren<QPushButton *>();
  for (QPushButton *b : buttons) {
    if (b->text() == text) {
      return b;
    }
  }
  return nullptr;
}

} // namespace

class TestCollectionPickerDialog : public QObject {
  Q_OBJECT

private slots:
  void treeMirrorsParentLinks();
  void excludedNodeReparentsChildrenToVisibleAncestor();
  void initialCheckedRoundTripsSorted();
  void parentToggleCascadesToWholeSubtree();
  void childDisagreementDropsPartialAncestorsFromSelection();
  void selectAllAndNoneButtons();
  void brokenParentLinkSurfacesAtRoot();
};

void TestCollectionPickerDialog::treeMirrorsParentLinks() {
  CollectionPickerDialog dlg(sampleTree(), {});
  auto *tree = dlg.findChild<QTreeWidget *>();
  QVERIFY(tree);

  QCOMPARE(tree->topLevelItemCount(), 2); // A, B
  QTreeWidgetItem *a = findItem(tree, QStringLiteral("A"));
  QVERIFY(a);
  QCOMPARE(childNames(a), QStringList({QStringLiteral("A1"), QStringLiteral("A2")}));
  QTreeWidgetItem *a1 = findItem(tree, QStringLiteral("A1"));
  QVERIFY(a1);
  QCOMPARE(childNames(a1), QStringList{QStringLiteral("A1a")});
  QTreeWidgetItem *b = findItem(tree, QStringLiteral("B"));
  QVERIFY(b);
  QCOMPARE(b->childCount(), 0);
}

void TestCollectionPickerDialog::excludedNodeReparentsChildrenToVisibleAncestor() {
  // Exclude A1: its child A1a must re-attach under A (the nearest visible
  // ancestor), not vanish with its parent or strand at the root.
  CollectionPickerDialog dlg(sampleTree(), {1});
  auto *tree = dlg.findChild<QTreeWidget *>();
  QVERIFY(tree);

  QVERIFY(!findItem(tree, QStringLiteral("A1"))); // hidden
  QTreeWidgetItem *a = findItem(tree, QStringLiteral("A"));
  QVERIFY(a);
  QCOMPARE(childNames(a), QStringList({QStringLiteral("A1a"), QStringLiteral("A2")}));

  // The excluded index must also be unreachable through Select All.
  buttonWithText(dlg, QStringLiteral("Select All"))->click();
  QCOMPARE(dlg.selectedIndices(), QList<int>({0, 2, 3, 4}));
}

void TestCollectionPickerDialog::initialCheckedRoundTripsSorted() {
  // Seed out of order, plus an index that does not exist — the bogus entry
  // must be ignored, and the result comes back ascending. Checking A1a (2)
  // also reads back its parent A1 (1): with Qt::ItemIsAutoTristate a parent
  // whose entire subtree agrees reports the children's state, and the tree
  // renders it checked — selectedIndices() mirrors exactly what the user
  // sees. A (0) stays partial (A2 unchecked) and is NOT selected.
  CollectionPickerDialog dlg(sampleTree(), {}, {4, 2, 99});
  QCOMPARE(dlg.selectedIndices(), QList<int>({1, 2, 4}));

  // A leaf without a checked sibling chain stays alone: B (4) has no
  // children, so nothing else gets promoted.
  CollectionPickerDialog leafOnly(sampleTree(), {}, {3});
  QCOMPARE(leafOnly.selectedIndices(), QList<int>{3});
}

void TestCollectionPickerDialog::parentToggleCascadesToWholeSubtree() {
  CollectionPickerDialog dlg(sampleTree(), {});
  auto *tree = dlg.findChild<QTreeWidget *>();
  QVERIFY(tree);

  findItem(tree, QStringLiteral("A"))->setCheckState(0, Qt::Checked);
  QCOMPARE(dlg.selectedIndices(), QList<int>({0, 1, 2, 3})); // B untouched

  findItem(tree, QStringLiteral("A"))->setCheckState(0, Qt::Unchecked);
  QCOMPARE(dlg.selectedIndices(), QList<int>{});
}

void TestCollectionPickerDialog::childDisagreementDropsPartialAncestorsFromSelection() {
  CollectionPickerDialog dlg(sampleTree(), {});
  auto *tree = dlg.findChild<QTreeWidget *>();
  QVERIFY(tree);

  findItem(tree, QStringLiteral("A"))->setCheckState(0, Qt::Checked);
  // Withdraw the grandchild: A1 (its only parent) auto-unchecks, A drops to
  // partial — partial means "not selected", so only A2 remains.
  findItem(tree, QStringLiteral("A1a"))->setCheckState(0, Qt::Unchecked);

  QCOMPARE(findItem(tree, QStringLiteral("A"))->checkState(0), Qt::PartiallyChecked);
  QCOMPARE(dlg.selectedIndices(), QList<int>{3});
}

void TestCollectionPickerDialog::selectAllAndNoneButtons() {
  CollectionPickerDialog dlg(sampleTree(), {});
  buttonWithText(dlg, QStringLiteral("Select All"))->click();
  QCOMPARE(dlg.selectedIndices(), QList<int>({0, 1, 2, 3, 4}));
  buttonWithText(dlg, QStringLiteral("Select None"))->click();
  QCOMPARE(dlg.selectedIndices(), QList<int>{});
}

void TestCollectionPickerDialog::brokenParentLinkSurfacesAtRoot() {
  // "Lost" points at a parent index that does not exist; it must still be
  // shown (at the root) so the user can target it.
  const QList<CollectionConfig> cols{makeCol(QStringLiteral("X"), -1),
                                     makeCol(QStringLiteral("Lost"), 7)};
  CollectionPickerDialog dlg(cols, {});
  auto *tree = dlg.findChild<QTreeWidget *>();
  QVERIFY(tree);
  QCOMPARE(tree->topLevelItemCount(), 2);

  QTreeWidgetItem *lost = findItem(tree, QStringLiteral("Lost"));
  QVERIFY(lost);
  lost->setCheckState(0, Qt::Checked);
  QCOMPARE(dlg.selectedIndices(), QList<int>{1});
}

QTEST_MAIN(TestCollectionPickerDialog)
#include "test_collectionpickerdialog.moc"
