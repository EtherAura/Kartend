// Kartend-0eeuk: CollectionRemover decision pipeline. The remover operates
// on a SettingsModel (plain in-memory collection lists — no DB is involved
// anywhere in this path) plus a CollectionRemoverHost; a fake host records
// every callback so the tests can assert the full contract: the no-selection
// precondition, the confirm-prompt gate (copy varies by root/descendants),
// subtree removal + parent-index remap via applyCollectionRemoval, the
// vanished-name scrub through propagateCollectionNameChange, expanded-state
// restoration translated through the old→new index map, follow-on selection,
// and the recreate-root flow when the last collection goes. The modal
// QMessageBox::question is answered by a zero-interval timer that runs
// inside the box's own exec loop.

#include "collectionremover.h"

#include "collection/collectionconfig.h"
#include "settingsmodel.h"

#include <QAbstractButton>
#include <QApplication>
#include <QMessageBox>
#include <QTest>
#include <QTimer>

namespace {

CollectionConfig makeCol(const QString &name, int parent) {
  CollectionConfig c;
  c.name = name;
  c.parentCollectionIndex = parent;
  return c;
}

/// Clicks the requested standard button on the next QMessageBox that goes
/// modal, capturing its text first. The timer only ever fires inside a
/// nested exec loop, so when no box appears it simply never triggers.
class ModalAnswerer : public QObject {
public:
  explicit ModalAnswerer(QMessageBox::StandardButton button) : m_button(button) {
    m_timer.setInterval(0);
    connect(&m_timer, &QTimer::timeout, this, [this] {
      auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
      if (!box) {
        return;
      }
      capturedText = box->text();
      triggered = true;
      m_timer.stop();
      if (QAbstractButton *b = box->button(m_button)) {
        b->click();
      }
    });
    m_timer.start();
  }

  QString capturedText;
  bool triggered = false;

private:
  QMessageBox::StandardButton m_button;
  QTimer m_timer;
};

/// Records every host callback; selection + expansion state are plain
/// fields the test sets up front.
class FakeHost : public CollectionRemoverHost {
public:
  // ── inputs ──
  int selectedIndex = -1;
  QList<int> expanded;
  std::function<void()> onEnsureRoot;

  // ── recorded calls ──
  QList<int> selectCalls;
  QList<int> expandAtCalls;
  QList<int> expandPathCalls;
  QList<int> loadCalls;
  QList<QPair<QString, QString>> nameChanges;
  QList<QList<CollectionConfig>> savedEmits;
  int updateTreeCalls = 0;
  int clearUiCalls = 0;
  int clearSelectionCalls = 0;
  int ensureRootCalls = 0;
  int saveStyleCalls = 0;
  int deleteStateCalls = 0;

  [[nodiscard]] int selectedCollectionIndex() const override { return selectedIndex; }
  [[nodiscard]] bool hasSelection() const override { return selectedIndex >= 0; }
  void selectCollection(int index) override { selectCalls.append(index); }
  void clearSelection() override { ++clearSelectionCalls; }
  [[nodiscard]] QList<int> expandedCollectionIndices() const override { return expanded; }
  void expandCollectionAtIndex(int index) override { expandAtCalls.append(index); }
  void expandPathToCollection(int index) override { expandPathCalls.append(index); }
  void loadCollectionToUI(int index) override { loadCalls.append(index); }
  void propagateCollectionNameChange(const QString &oldName, const QString &newName) override {
    nameChanges.append({oldName, newName});
  }
  void updateCollectionTreeWidget() override { ++updateTreeCalls; }
  void clearCollectionUI() override { ++clearUiCalls; }
  void ensureRootCollectionExists() override {
    ++ensureRootCalls;
    if (onEnsureRoot) {
      onEnsureRoot();
    }
  }
  void updateSaveButtonStyle() override { ++saveStyleCalls; }
  void updateDeleteButtonState() override { ++deleteStateCalls; }
  [[nodiscard]] QWidget *dialogWidget() override { return nullptr; }
  void emitCollectionSaved(const QList<CollectionConfig> &collections) override {
    savedEmits.append(collections);
  }
};

/// Owns the lists SettingsModel points at, mirroring how SettingsDialog
/// wires its own fields in.
struct ModelFixture {
  explicit ModelFixture(const QList<CollectionConfig> &cols) : collections(cols), working(cols) {
    model.collections = &collections;
    model.workingCollections = &working;
    model.originalCollection = &original;
    model.collectionSaved = &saved;
    model.currentIndex = &currentIndex;
  }

  QList<CollectionConfig> collections;
  QList<CollectionConfig> working;
  CollectionConfig original;
  bool saved = false;
  int currentIndex = -1;
  SettingsModel model;
};

QStringList names(const QList<CollectionConfig> &cols) {
  QStringList out;
  for (const CollectionConfig &c : cols) {
    out.append(c.name);
  }
  return out;
}

/// A: { A1 }, B: { B1 } — two roots with one child each.
QList<CollectionConfig> twoFamilies() {
  return {makeCol(QStringLiteral("A"), -1), makeCol(QStringLiteral("A1"), 0),
          makeCol(QStringLiteral("B"), -1), makeCol(QStringLiteral("B1"), 2)};
}

} // namespace

class TestCollectionRemover : public QObject {
  Q_OBJECT

private slots:
  void noSelectionIsANoOpWithoutPrompt();
  void decliningThePromptChangesNothing();
  void removingLeafRemapsSurvivorsAndReselectsParent();
  void removingRootSubtreeScrubsVanishedNames();
  void expandedStatesRestoreThroughTheIndexRemap();
  void removingLastCollectionRunsRecreateFlow();
};

void TestCollectionRemover::noSelectionIsANoOpWithoutPrompt() {
  ModelFixture fx(twoFamilies());
  FakeHost host; // selectedIndex stays -1
  CollectionRemover remover(&fx.model, &host);

  ModalAnswerer answer(QMessageBox::Yes); // would unblock (and flag) a stray prompt
  remover.run();

  QVERIFY(!answer.triggered); // precondition failed before any prompt
  QCOMPARE(names(fx.collections), names(twoFamilies()));
  QVERIFY(host.savedEmits.isEmpty());
  QCOMPARE(host.updateTreeCalls, 0);
  QVERIFY(!fx.saved);
}

void TestCollectionRemover::decliningThePromptChangesNothing() {
  ModelFixture fx(twoFamilies());
  FakeHost host;
  host.selectedIndex = 1; // A1
  CollectionRemover remover(&fx.model, &host);

  ModalAnswerer answer(QMessageBox::No);
  remover.run();

  QVERIFY(answer.triggered);
  // Leaf removal prompt carries just the name — no descendant count.
  QCOMPARE(answer.capturedText, QStringLiteral("Remove \"A1\"?"));
  QCOMPARE(names(fx.collections), names(twoFamilies()));
  QVERIFY(host.savedEmits.isEmpty());
  QVERIFY(host.nameChanges.isEmpty());
  QCOMPARE(host.updateTreeCalls, 0);
  QVERIFY(!fx.saved);
}

void TestCollectionRemover::removingLeafRemapsSurvivorsAndReselectsParent() {
  ModelFixture fx(twoFamilies());
  FakeHost host;
  host.selectedIndex = 1; // A1 — everything after it shifts down by one
  CollectionRemover remover(&fx.model, &host);

  ModalAnswerer answer(QMessageBox::Yes);
  remover.run();
  QVERIFY(answer.triggered);

  QCOMPARE(names(fx.collections),
           QStringList({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("B1")}));
  // B1's parent link must follow B from index 2 to index 1 — the remap the
  // old removeAt loop never did.
  QCOMPARE(fx.collections[2].parentCollectionIndex, 1);
  QCOMPARE(names(fx.working), names(fx.collections)); // working list mirrors

  // Persisted exactly once, with the post-removal list.
  QCOMPARE(host.savedEmits.size(), 1);
  QCOMPARE(names(host.savedEmits.first()), names(fx.collections));

  // Vanished-name scrub covers just the leaf, with empty newName = delete.
  QCOMPARE(host.nameChanges, (QList<QPair<QString, QString>>{{QStringLiteral("A1"), QString()}}));

  // Follow-on selection lands on the deleted node's (remapped) parent A.
  QCOMPARE(host.selectCalls, QList<int>{0});
  QCOMPARE(host.expandPathCalls, QList<int>{0});
  QCOMPARE(host.loadCalls, QList<int>{0});
  QCOMPARE(fx.original.name, QStringLiteral("A"));

  QVERIFY(fx.saved);
  QCOMPARE(host.saveStyleCalls, 1);
  QCOMPARE(host.deleteStateCalls, 1);
}

void TestCollectionRemover::removingRootSubtreeScrubsVanishedNames() {
  // A: { A1: { A1a } }, B — removing A takes the whole subtree with it.
  ModelFixture fx({makeCol(QStringLiteral("A"), -1), makeCol(QStringLiteral("A1"), 0),
                   makeCol(QStringLiteral("A1a"), 1), makeCol(QStringLiteral("B"), -1)});
  FakeHost host;
  host.selectedIndex = 0;
  CollectionRemover remover(&fx.model, &host);

  ModalAnswerer answer(QMessageBox::Yes);
  remover.run();
  QVERIFY(answer.triggered);

  // Root-with-descendants prompt names the blast radius and warns.
  QVERIFY(answer.capturedText.contains(QStringLiteral("\"A\"")));
  QVERIFY(answer.capturedText.contains(QStringLiteral("2")));
  QVERIFY(answer.capturedText.contains(QStringLiteral("cannot be undone")));

  QCOMPARE(names(fx.collections), QStringList{QStringLiteral("B")});
  QCOMPARE(fx.collections[0].parentCollectionIndex, -1);

  // Every name in the removed subtree gets scrubbed from link references.
  QList<QPair<QString, QString>> expected{{QStringLiteral("A"), QString()},
                                          {QStringLiteral("A1"), QString()},
                                          {QStringLiteral("A1a"), QString()}};
  QCOMPARE(host.nameChanges, expected);

  // Deleted node was a root → fall back to the first collection.
  QCOMPARE(host.selectCalls, QList<int>{0});
  QCOMPARE(fx.original.name, QStringLiteral("B"));
}

void TestCollectionRemover::expandedStatesRestoreThroughTheIndexRemap() {
  ModelFixture fx(twoFamilies());
  FakeHost host;
  host.selectedIndex = 1;    // remove A1
  host.expanded = {0, 1, 3}; // A, A1 (about to vanish), B1
  CollectionRemover remover(&fx.model, &host);

  ModalAnswerer answer(QMessageBox::Yes);
  remover.run();

  // 0 → 0, 1 → removed (skipped), 3 → 2 through the old→new map.
  QCOMPARE(host.expandAtCalls, (QList<int>{0, 2}));
}

void TestCollectionRemover::removingLastCollectionRunsRecreateFlow() {
  ModelFixture fx({makeCol(QStringLiteral("Solo"), -1)});
  FakeHost host;
  host.selectedIndex = 0;
  // The real host's ensureRootCollectionExists seeds a fresh root into the
  // model — mirror that so the post-recreate branch runs.
  host.onEnsureRoot = [&fx] {
    fx.collections.append(makeCol(QStringLiteral("Fresh"), -1));
    fx.working.append(makeCol(QStringLiteral("Fresh"), -1));
  };
  CollectionRemover remover(&fx.model, &host);

  ModalAnswerer answer(QMessageBox::Yes);
  remover.run();
  QVERIFY(answer.triggered);

  QCOMPARE(host.clearUiCalls, 1);
  QCOMPARE(host.clearSelectionCalls, 1);
  QCOMPARE(host.ensureRootCalls, 1);
  // Saved twice: once for the now-empty list, once after the recreate.
  QCOMPARE(host.savedEmits.size(), 2);
  QVERIFY(host.savedEmits.first().isEmpty());
  QCOMPARE(names(host.savedEmits.last()), QStringList{QStringLiteral("Fresh")});
  // Tree refreshed for both states, and the fresh root becomes current.
  QCOMPARE(host.updateTreeCalls, 2);
  QCOMPARE(host.selectCalls, QList<int>{0});
  QCOMPARE(host.loadCalls, QList<int>{0});
  QCOMPARE(fx.original.name, QStringLiteral("Fresh"));
  QVERIFY(fx.saved);
}

QTEST_MAIN(TestCollectionRemover)
#include "test_collectionremover.moc"
