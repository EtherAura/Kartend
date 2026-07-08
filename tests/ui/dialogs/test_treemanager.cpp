// TreeManager: the SettingsDialog tree controller, driven headlessly with a
// real (never shown) CollectionTreeWidget and a fake SettingsTreeHost. Covers
// the tree-sync pipeline the dialog's four coarse integration scenarios never
// touch: rebuild hierarchy + index-map round-trips, stale-parent orphan
// repair, linked-appearance mirrors (dedup / self-skip / primary-parent skip /
// read-only flags), ancestor expansion, the rename commit path (lists +
// mirrors + alias propagation + dirty flag, blank-rename revert,
// rename-back-to-original), the selection switch guard, and the post-drop
// reparent resync (private slot driven via QMetaObject::invokeMethod, as the
// real drop path is a QDropEvent).

#include "treemanager.h"

#include "collection/collectionconfig.h"
#include "collectiontreewidget.h"
#include "settingstreehost.h"

#include <functional>

#include <QSignalSpy>
#include <QTest>
#include <QTreeWidgetItem>

namespace {

CollectionConfig makeCol(const QString &name, int parent = -1) {
  CollectionConfig c;
  c.name = name;
  c.parentCollectionIndex = parent;
  c.isSubcollection = (parent >= 0);
  return c;
}

/// Records the dialog-side callbacks the gesture handlers route through.
class FakeTreeHost : public SettingsTreeHost {
public:
  bool allowSwitch = true;
  bool unsaved = false;

  int resolveCalls = 0;
  /// Optional dialog-side effect to run inside resolveUnsavedChanges —
  /// stands in for handleSaveCollection's tree refresh + re-selection.
  std::function<void()> onResolve;
  QList<int> loadCalls;
  int saveStyleCalls = 0;
  int deleteStateCalls = 0;
  int contextHeaderCalls = 0;
  int duplicateCalls = 0;
  int removeCalls = 0;
  int copyFromCalls = 0;

  [[nodiscard]] bool resolveUnsavedChanges(const QString &, bool) override {
    ++resolveCalls;
    if (onResolve) {
      onResolve();
    }
    return allowSwitch;
  }
  void loadCollectionToUI(int index) override { loadCalls.append(index); }
  void updateSaveButtonStyle() override { ++saveStyleCalls; }
  void updateDeleteButtonState() override { ++deleteStateCalls; }
  void updateContextHeader() override { ++contextHeaderCalls; }
  [[nodiscard]] bool hasUnsavedChanges() const override { return unsaved; }
  void duplicateCollection() override { ++duplicateCalls; }
  void removeCollection() override { ++removeCalls; }
  void copySettingsFromOtherCollection() override { ++copyFromCalls; }
};

/// Wires a TreeManager exactly like SettingsDialog does: borrowed collection
/// lists + selection state, fake host, attached widget, initial rebuild.
struct Fixture {
  QList<CollectionConfig> live;
  QList<CollectionConfig> working;
  CollectionTreeWidget widget;
  FakeTreeHost host;
  int currentIndex = -1;
  QTreeWidgetItem *currentItem = nullptr;
  CollectionConfig original;
  bool saved = true;
  TreeManager manager;

  explicit Fixture(const QList<CollectionConfig> &cols)
      : live(cols), working(cols), manager(&widget, &live, &working) {
    manager.setSelectionState(&currentIndex, &currentItem, &original, &saved);
    manager.setHost(&host);
    manager.attachWidget();
    manager.rebuild();
  }
};

} // namespace

class TestTreeManager : public QObject {
  Q_OBJECT

private slots:
  void rebuild_buildsHierarchyAndIndexMaps();
  void rebuild_orphansStaleParentToRoot();
  void rebuild_linkedAppearanceMirrors();
  void expandPathTo_expandsAncestorsOnly();
  void wouldCreateCircularReference_walksAncestry();
  void rename_updatesListsMirrorsAndAliases();
  void rename_blankNameReverts();
  void rename_backToOriginalRestoresSavedFlag();
  void selectionChange_loadsRowAndGuardsUnsaved();
  void deselect_guardsUnsavedAndClearsState();
  void rearranged_resyncsParentsAndSignals();
};

void TestTreeManager::rebuild_buildsHierarchyAndIndexMaps() {
  Fixture f({makeCol(QStringLiteral("Root")), makeCol(QStringLiteral("Child"), 0),
             makeCol(QStringLiteral("Grandchild"), 1), makeCol(QStringLiteral("Other Root"))});

  QCOMPARE(f.widget.topLevelItemCount(), 2);
  QTreeWidgetItem *root = f.manager.itemAt(0);
  QTreeWidgetItem *child = f.manager.itemAt(1);
  QTreeWidgetItem *grandchild = f.manager.itemAt(2);
  QVERIFY(root && child && grandchild);
  QCOMPARE(root->text(0), QStringLiteral("Root"));
  QCOMPARE(child->parent(), root);
  QCOMPARE(grandchild->parent(), child);
  QCOMPARE(f.manager.itemAt(3)->parent(), nullptr);

  // Index-map round trips.
  for (int i = 0; i < f.live.size(); ++i) {
    QVERIFY(f.manager.contains(f.manager.itemAt(i)));
    QCOMPARE(f.manager.indexOf(f.manager.itemAt(i)), i);
  }
  QCOMPARE(f.manager.indexOf(nullptr), -1);
  QCOMPARE(f.manager.itemAt(99), nullptr);
}

void TestTreeManager::rebuild_orphansStaleParentToRoot() {
  Fixture f({makeCol(QStringLiteral("Root")), makeCol(QStringLiteral("Stale"), 99)});

  // The stale-parent row is repaired to a top-level entry...
  QCOMPARE(f.widget.topLevelItemCount(), 2);
  QTreeWidgetItem *stale = f.manager.itemAt(1);
  QVERIFY(stale);
  QCOMPARE(stale->parent(), nullptr);
  // ...and the live list is rewritten so the repair persists.
  QCOMPARE(f.live[1].parentCollectionIndex, -1);
  QVERIFY(!f.live[1].isSubcollection);
}

void TestTreeManager::rebuild_linkedAppearanceMirrors() {
  QList<CollectionConfig> cols{makeCol(QStringLiteral("P1")), makeCol(QStringLiteral("P2")),
                               makeCol(QStringLiteral("L"), 0)};
  // Alias list with a stutter (P2 twice), a self-reference, an unknown name,
  // and the primary parent — only ONE mirror (under P2) must materialise.
  cols[2].additionalParentNames =
      QStringList{QStringLiteral("P2"), QStringLiteral("P2"), QStringLiteral("L"),
                  QStringLiteral("Nope"), QStringLiteral("P1")};
  Fixture f(cols);

  const QList<QTreeWidgetItem *> mirrors = f.manager.linkedItemsFor(2);
  QCOMPARE(mirrors.size(), 1);
  QTreeWidgetItem *mirror = mirrors.first();
  QCOMPARE(mirror->parent(), f.manager.itemAt(1)); // hangs under P2
  QCOMPARE(mirror->text(0), QStringLiteral("L"));
  QVERIFY(mirror->font(0).italic());
  QVERIFY(mirror->data(0, Qt::UserRole).toBool());
  QVERIFY(!(mirror->flags() & Qt::ItemIsEditable));
  QVERIFY(!(mirror->flags() & Qt::ItemIsDragEnabled));
  QVERIFY(mirror->toolTip(0).contains(QStringLiteral("P1"))); // names the primary home
  // The mirror resolves to the canonical collection index.
  QCOMPARE(f.manager.indexOf(mirror), 2);
  // Collections without aliases expose an empty (not detached) list.
  QVERIFY(f.manager.linkedItemsFor(0).isEmpty());
}

void TestTreeManager::expandPathTo_expandsAncestorsOnly() {
  Fixture f({makeCol(QStringLiteral("Root")), makeCol(QStringLiteral("Child"), 0),
             makeCol(QStringLiteral("Grandchild"), 1)});

  QVERIFY(!f.manager.itemAt(0)->isExpanded());
  f.manager.expandPathTo(2);
  QVERIFY(f.manager.itemAt(0)->isExpanded());
  QVERIFY(f.manager.itemAt(1)->isExpanded());
  QVERIFY(!f.manager.itemAt(2)->isExpanded());

  // Out-of-range index: no crash, no expansion changes.
  f.manager.expandPathTo(42);
}

void TestTreeManager::wouldCreateCircularReference_walksAncestry() {
  Fixture f({makeCol(QStringLiteral("Root")), makeCol(QStringLiteral("Child"), 0),
             makeCol(QStringLiteral("Other"))});

  QVERIFY(f.manager.wouldCreateCircularReference(0, 1));  // descendant as parent
  QVERIFY(f.manager.wouldCreateCircularReference(0, 0));  // self
  QVERIFY(!f.manager.wouldCreateCircularReference(0, 2)); // unrelated
  QVERIFY(f.manager.wouldCreateCircularReference(-1, 2)); // invalid child rejected
}

void TestTreeManager::rename_updatesListsMirrorsAndAliases() {
  QList<CollectionConfig> cols{makeCol(QStringLiteral("Parent")),
                               makeCol(QStringLiteral("Linked"), -1)};
  cols[1].additionalParentNames = QStringList{QStringLiteral("Parent")};
  Fixture f(cols);
  QCOMPARE(f.manager.linkedItemsFor(1).size(), 1);

  // Renaming the mirrored collection retitles its mirror row.
  f.saved = true;
  f.manager.itemAt(1)->setText(0, QStringLiteral("Linked Renamed"));
  QCOMPARE(f.live[1].name, QStringLiteral("Linked Renamed"));
  QCOMPARE(f.working[1].name, QStringLiteral("Linked Renamed"));
  QCOMPARE(f.manager.linkedItemsFor(1).first()->text(0), QStringLiteral("Linked Renamed"));
  QVERIFY(!f.saved); // rename is an unsaved change
  QVERIFY(f.host.saveStyleCalls > 0);

  // Renaming the alias parent rewrites additionalParentNames in both lists.
  f.manager.itemAt(0)->setText(0, QStringLiteral("Parent Renamed"));
  QCOMPARE(f.live[0].name, QStringLiteral("Parent Renamed"));
  QCOMPARE(f.live[1].additionalParentNames, QStringList{QStringLiteral("Parent Renamed")});
  QCOMPARE(f.working[1].additionalParentNames, QStringList{QStringLiteral("Parent Renamed")});
}

void TestTreeManager::rename_blankNameReverts() {
  Fixture f({makeCol(QStringLiteral("Keep Me"))});

  f.manager.itemAt(0)->setText(0, QStringLiteral("   "));

  QCOMPARE(f.manager.itemAt(0)->text(0), QStringLiteral("Keep Me"));
  QCOMPARE(f.live[0].name, QStringLiteral("Keep Me"));
  QCOMPARE(f.working[0].name, QStringLiteral("Keep Me"));
}

void TestTreeManager::rename_backToOriginalRestoresSavedFlag() {
  Fixture f({makeCol(QStringLiteral("Original"))});
  f.currentIndex = 0;
  f.original = f.working[0];
  f.host.unsaved = false;

  f.manager.itemAt(0)->setText(0, QStringLiteral("Changed"));
  QVERIFY(!f.saved);

  // Typing the original name back is treated as a revert when the host
  // reports no other pending edits.
  f.manager.itemAt(0)->setText(0, QStringLiteral("Original"));
  QVERIFY(f.saved);
}

void TestTreeManager::selectionChange_loadsRowAndGuardsUnsaved() {
  Fixture f({makeCol(QStringLiteral("A")), makeCol(QStringLiteral("B"))});

  // First selection: no previous row, so no prompt — state + load sync.
  f.widget.setCurrentItem(f.manager.itemAt(1));
  QCOMPARE(f.currentIndex, 1);
  QCOMPARE(f.currentItem, f.manager.itemAt(1));
  QCOMPARE(f.original.name, QStringLiteral("B"));
  QVERIFY(f.saved);
  QCOMPARE(f.host.loadCalls, QList<int>{1});

  // Guard: the host vetoes the switch — selection snaps back, no load fires.
  f.host.allowSwitch = false;
  f.widget.setCurrentItem(f.manager.itemAt(0));
  QCOMPARE(f.currentIndex, 1);
  QCOMPARE(f.widget.currentItem(), f.manager.itemAt(1));
  QCOMPARE(f.host.loadCalls, QList<int>{1});

  // Host allows it again — the switch completes.
  f.host.allowSwitch = true;
  f.widget.setCurrentItem(f.manager.itemAt(0));
  QCOMPARE(f.currentIndex, 0);
  QCOMPARE(f.host.loadCalls, (QList<int>{1, 0}));
}

void TestTreeManager::deselect_guardsUnsavedAndClearsState() {
  Fixture f({makeCol(QStringLiteral("A")), makeCol(QStringLiteral("B"))});

  // Establish a selection so the deselect has a previous row to protect.
  f.widget.setCurrentItem(f.manager.itemAt(0));
  QCOMPARE(f.currentIndex, 0);
  const int resolveCallsAfterSelect = f.host.resolveCalls;

  // Whitespace-click deselect with the host vetoing (Cancel): the gate must
  // fire once, the previous row must come back, and the borrowed state must
  // stay on the old collection — no silent drop into index -1.
  f.host.allowSwitch = false;
  f.widget.clearSelection();
  QCOMPARE(f.host.resolveCalls, resolveCallsAfterSelect + 1);
  QCOMPARE(f.currentIndex, 0);
  QCOMPARE(f.currentItem, f.manager.itemAt(0));
  QCOMPARE(f.widget.currentItem(), f.manager.itemAt(0));
  QVERIFY(f.manager.itemAt(0)->isSelected());
  // The blocked restore must not re-enter the handler and double-prompt.
  QCOMPARE(f.host.resolveCalls, resolveCallsAfterSelect + 1);

  // Host allows it (Save/Discard resolved): the deselect completes and the
  // selection state clears.
  f.host.allowSwitch = true;
  f.widget.clearSelection();
  QCOMPARE(f.currentIndex, -1);
  QCOMPARE(f.currentItem, nullptr);
  QVERIFY(f.widget.selectedItems().isEmpty());

  // Deselecting with nothing selected must not prompt at all.
  const int resolveCallsAfterClear = f.host.resolveCalls;
  f.widget.clearSelection();
  QCOMPARE(f.host.resolveCalls, resolveCallsAfterClear);

  // Save path: handleSaveCollection's tree refresh re-selects the saved row
  // (signals blocked) from inside the resolver. The deselect must still win
  // — state cleared AND the restored selection cleared, no extra prompt.
  f.widget.setCurrentItem(f.manager.itemAt(1));
  QCOMPARE(f.currentIndex, 1);
  f.host.onResolve = [&f]() {
    QSignalBlocker blocker(&f.widget);
    f.widget.setCurrentItem(f.manager.itemAt(1));
    f.manager.itemAt(1)->setSelected(true);
  };
  const int resolveCallsBeforeSave = f.host.resolveCalls;
  f.widget.clearSelection();
  QCOMPARE(f.host.resolveCalls, resolveCallsBeforeSave + 1);
  QCOMPARE(f.currentIndex, -1);
  QCOMPARE(f.currentItem, nullptr);
  QVERIFY(f.widget.selectedItems().isEmpty());
}

void TestTreeManager::rearranged_resyncsParentsAndSignals() {
  Fixture f({makeCol(QStringLiteral("A")), makeCol(QStringLiteral("B")),
             makeCol(QStringLiteral("C"), 1)});
  QSignalSpy reordered(&f.manager, &TreeManager::collectionsReordered);
  QVERIFY(reordered.isValid());

  // Simulate a completed drop: move B (with its child C) under A. The real
  // path runs through CollectionTreeWidget::dropEvent, which Qt Test can't
  // synthesise faithfully, so the resync slot is invoked directly.
  QTreeWidgetItem *itemB = f.manager.itemAt(1);
  f.widget.takeTopLevelItem(f.widget.indexOfTopLevelItem(itemB));
  f.manager.itemAt(0)->addChild(itemB);
  f.saved = false;

  QVERIFY(QMetaObject::invokeMethod(&f.manager, "onWidgetRearranged", Qt::DirectConnection));

  QCOMPARE(f.live[1].parentCollectionIndex, 0);
  QVERIFY(f.live[1].isSubcollection);
  QCOMPARE(f.live[2].parentCollectionIndex, 1); // C stays under B
  QCOMPARE(f.working[1].parentCollectionIndex, 0);
  QCOMPARE(reordered.count(), 1);
  QVERIFY(f.saved); // reparent persists immediately via the host
  QVERIFY(f.host.saveStyleCalls > 0);
}

QTEST_MAIN(TestTreeManager)
#include "test_treemanager.moc"
