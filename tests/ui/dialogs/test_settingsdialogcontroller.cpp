// SettingsDialogController post-dialog reconciliation: the dialog's
// persistent Save button, tree drag-drop reparent and recursive import
// persist collections mid-session through the collectionSaved callback. A
// later Cancel/Esc used to early-return before the reconciliation block, so
// a SAVED rename left its database rows stranded under the old uuid
// (uuid = hash(name, mediaDirectory)) and orphan rows unpurged. The
// controller must run uuid migration + orphan purge whenever the session
// persisted anything — accepted or not — and still skip everything when a
// cancelled session saved nothing. Driven through the createSettingsDialog
// factory seam with a fake ISettingsDialog; no widgets are shown.

#include "applicationcontext.h"
#include "collection/collectionconfig.h"
#include "collection/collectioncontext.h"
#include "collection/generalsettings.h"
#include "collection/typehelpers.h"
#include "isettingsdialog.h"
#include "scrollmanager.h"
#include "settingsdialogcontroller.h"

#include "../../integration/mocks/mockdatabasemanager.h"
#include "../../integration/mocks/mocksettingsmanager.h"

#include <functional>
#include <memory>
#include <QDialog>
#include <QPair>
#include <QScrollArea>
#include <QTest>
#include <QWidget>

namespace {

/// Records the DB-consistency calls the reconciliation block must issue.
class RecordingDatabaseManager : public KartendTest::MockDatabaseManager {
public:
  using KartendTest::MockDatabaseManager::MockDatabaseManager;

  QList<QPair<QString, QString>> migrations;
  int purgeCount = 0;
  QList<CollectionConfig> lastPurgeList;

  void migrateCollectionUuid(const QString &oldUuid, const QString &newUuid) override {
    migrations.append({oldUuid, newUuid});
  }
  void purgeOrphanCollectionData(const QList<CollectionConfig> &liveCollections) override {
    ++purgeCount;
    lastPurgeList = liveCollections;
  }
};

/// Scripted stand-in for the modal dialog: exec() optionally fires the
/// controller's onCollectionSaved callback (a mid-session save) and returns
/// the scripted dialog code.
class FakeSettingsDialog : public ISettingsDialog {
public:
  QList<CollectionConfig> collections;
  std::function<void()> onExec;
  int execResult = QDialog::Rejected;

  int exec() override {
    if (onExec) {
      onExec();
    }
    return execResult;
  }
  [[nodiscard]] const QList<CollectionConfig> &getCollections() const override {
    return collections;
  }
  void setInitialPage(SettingsPage) override {}
};

struct Harness {
  QList<CollectionConfig> collections;
  int currentIndex = 0;
  RecordingDatabaseManager db;
  KartendTest::MockSettingsManager sm;
  ApplicationContext ctx;
  SettingsDialogController controller{&ctx};

  Harness() {
    ctx.managers.settingsManager = &sm;
    CollectionConfig c;
    c.name = QStringLiteral("Old");
    c.mediaDirectory = QStringLiteral("/tmp/kartend-test-media");
    collections = {c};
  }

  /// Context wired for a headless run: no widgets, no managers beyond the
  /// database double the reconciliation block needs.
  SettingsDialogContext makeContext(bool saveDuringExec, int execResult) {
    SettingsDialogContext context;
    context.collections = &collections;
    context.currentCollectionIndex = &currentIndex;
    context.databaseManager = &db;
    context.createSettingsDialog =
        [saveDuringExec,
         execResult](QWidget *, const QList<CollectionConfig> &initialCollections, int,
                     std::function<void(const QList<CollectionConfig> &)> onCollectionSaved,
                     std::function<void(int)>) -> std::unique_ptr<ISettingsDialog> {
      auto dlg = std::make_unique<FakeSettingsDialog>();
      QList<CollectionConfig> renamed = initialCollections;
      renamed[0].name = QStringLiteral("New");
      dlg->collections = renamed;
      dlg->execResult = execResult;
      if (saveDuringExec) {
        dlg->onExec = [onCollectionSaved, renamed]() { onCollectionSaved(renamed); };
      }
      return dlg;
    };
    return context;
  }

  /// Like makeContext, but the accepted dialog returns the initial list
  /// transformed by @p mutate — for driving the uuid-migration pairing with
  /// arbitrary edit sessions (multi-rename, shared directories, rename+move).
  SettingsDialogContext makeMutatedContext(std::function<void(QList<CollectionConfig> &)> mutate) {
    SettingsDialogContext context;
    context.collections = &collections;
    context.currentCollectionIndex = &currentIndex;
    context.databaseManager = &db;
    context.createSettingsDialog =
        [mutate = std::move(mutate)](QWidget *, const QList<CollectionConfig> &initialCollections,
                                     int, std::function<void(const QList<CollectionConfig> &)>,
                                     std::function<void(int)>) -> std::unique_ptr<ISettingsDialog> {
      auto dlg = std::make_unique<FakeSettingsDialog>();
      QList<CollectionConfig> edited = initialCollections;
      mutate(edited);
      dlg->collections = edited;
      dlg->execResult = QDialog::Accepted;
      return dlg;
    };
    return context;
  }
};

CollectionConfig makeCollection(const QString &name, const QString &mediaDir) {
  CollectionConfig c;
  c.name = name;
  c.mediaDirectory = mediaDir;
  return c;
}

/// Settings double that mirrors the real SettingsManager::saveCollections
/// hot-reload contract: every save diffs each collection's GridLayoutPreferences
/// against the previous save and emits gridLayoutChanged for the ones that
/// moved. That emission is what reaches ScrollManager::onGridLayoutChanged
/// when the dialog's persistent Save button fires mid-session.
class GridDiffingSettingsManager : public KartendTest::MockSettingsManager {
public:
  using KartendTest::MockSettingsManager::MockSettingsManager;

  QList<CollectionConfig> lastSaved;

  ErrorUtils::Result<void> saveCollections(const QList<CollectionConfig> &collections) override {
    for (int i = 0; i < collections.size(); ++i) {
      if (i >= lastSaved.size() || lastSaved[i].gridLayout != collections[i].gridLayout) {
        emit gridLayoutChanged(i, collections[i].gridLayout);
      }
    }
    lastSaved = collections;
    return KartendTest::MockSettingsManager::saveCollections(collections);
  }
};

/// Real ScrollManager over a headless grid container + scroll area, viewing
/// one Expand-mode collection (primary width 4). No database manager: the
/// Settings apply path under test never touches it.
struct GridHarness {
  QList<CollectionConfig> collections;
  int currentIndex = 0;
  GeneralSettings general;
  GridDiffingSettingsManager sm;
  QScrollArea scrollArea;
  QWidget *gridContainer = new QWidget;
  ApplicationContext ctx;
  ScrollManager scroll;
  SettingsDialogController controller{&ctx};

  explicit GridHarness(int hiddenWidth, bool paneHidden,
                       const std::function<void(CollectionConfig &)> &configure = {}) {
    CollectionConfig c = makeCollection(QStringLiteral("Grid"), QString());
    c.gridLayout.gridWidth = 4;
    c.gridLayout.gridWidthSidebarHidden = hiddenWidth;
    c.sidebar.sidebarMode = DetailsPaneMode::Expand;
    c.sidebar.sidebarVisible = !paneHidden;
    if (configure) {
      configure(c);
    }
    collections = {c};
    sm.lastSaved = collections;

    scrollArea.resize(1200, 800);
    scrollArea.setWidget(gridContainer);
    ctx.collection.collections = &collections;
    ctx.collection.currentCollectionIndex = &currentIndex;
    ctx.collection.generalSettings = &general;
    ctx.ui.itemScrollArea = &scrollArea;
    ctx.ui.gridContainer = gridContainer;
    ctx.managers.settingsManager = &sm;
    ctx.managers.seedScrollRoles(&scroll);
    scroll.setupReferences(ScrollManagerSetup{.ctx = &ctx});
    // The production wiring (MainWindow::connectManagers) for the same edge.
    QObject::connect(&sm, &ISettingsManager::gridLayoutChanged, &scroll,
                     &ScrollManager::onGridLayoutChanged);

    CollectionContext view;
    view.currentIndex = 0;
    view.config = c;
    // MainWindow::updateScrollManagerSidebarShrinking: Expand mode + hidden
    // pane (left/right dock, grid view) => the hidden override is live.
    scroll.setSidebarShrinkingActive(paneHidden);
    scroll.setupVirtualScrolling(40, view);
  }

  /// Accepted dialog whose session presses Save once after @p edit, then OK.
  /// @p atSave runs right after the mid-session Save returned, before OK.
  SettingsDialogContext makeContext(std::function<void(CollectionConfig &)> edit,
                                    std::function<void()> atSave) {
    SettingsDialogContext context;
    context.collections = &collections;
    context.currentCollectionIndex = &currentIndex;
    context.scrollManager = &scroll;
    context.createSettingsDialog =
        [edit = std::move(edit), atSave = std::move(atSave)](
            QWidget *, const QList<CollectionConfig> &initialCollections, int,
            std::function<void(const QList<CollectionConfig> &)> onCollectionSaved,
            std::function<void(int)>) -> std::unique_ptr<ISettingsDialog> {
      auto dlg = std::make_unique<FakeSettingsDialog>();
      QList<CollectionConfig> edited = initialCollections;
      edit(edited[0]);
      dlg->collections = edited;
      dlg->execResult = QDialog::Accepted;
      dlg->onExec = [onCollectionSaved, edited, atSave]() {
        onCollectionSaved(edited);
        atSave();
      };
      return dlg;
    };
    return context;
  }

  [[nodiscard]] int appliedColumns() const { return scroll.getMetrics().itemsPerRow; }
};

} // namespace

class TestSettingsDialogController : public QObject {
  Q_OBJECT

private slots:
  void cancelAfterMidSessionSaveStillMigratesAndPurges();
  void cancelWithoutAnySaveSkipsReconciliation();
  void acceptedSessionStillMigratesAndPurges();
  void renameWithSharedDirectorySiblingMigratesOnlyThatRow();
  void multiRenameMigratesEachRow();
  void renamePlusDirectoryChangeStillMigrates();
  void cornerRadiusSaveKeepsHiddenPaneWidth();
  void cornerRadiusSaveWithPaneOpenKeepsPrimaryWidth();
  void cornerRadiusSaveWithoutHiddenOverrideKeepsPrimaryWidth();
  void gridWidthEditWithPaneHiddenKeepsHiddenPaneWidth();
  void horizontalSaveWithPaneHiddenKeepsBaseRowCount();
};

void TestSettingsDialogController::horizontalSaveWithPaneHiddenKeepsBaseRowCount() {
  // Kartend-utd3p is a Grid Width fix only. Horizontal view with Horizontal
  // Height 3 / Horizontal Height (pane hidden) 5 and the pane hidden: a Corner
  // Radius-only Save keeps the row count the untouched base gives, i.e. the
  // primary height pushed through updateHorizontalGridHeight. The width
  // override is left unset so the width fix cannot take part here.
  const auto horizontal = [](CollectionConfig &c) {
    c.viewType = ViewType::Horizontal;
    c.gridLayout.horizontalGridHeight = 3;
    c.gridLayout.horizontalGridHeightSidebarHidden = 5;
  };
  GridHarness h(0, /*paneHidden=*/true, horizontal);
  QCOMPARE(h.appliedColumns(), 5);
  int rowsAtSave = -1;
  h.controller.openSettingsDialog(
      h.makeContext([](CollectionConfig &c) { c.gridLayout.cornerRadius += 4; },
                    [&h, &rowsAtSave]() { rowsAtSave = h.appliedColumns(); }));

  // Expectation from the normal Horizontal path with the base call: a twin
  // harness that receives the same cluster through onGridLayoutChanged.
  GridHarness twin(0, /*paneHidden=*/true, horizontal);
  GridLayoutPreferences saved = twin.collections[0].gridLayout;
  saved.cornerRadius += 4;
  twin.scroll.onGridLayoutChanged(0, saved);
  QCOMPARE(rowsAtSave, twin.appliedColumns());
  QCOMPARE(rowsAtSave, 3);
  // Both stored heights are untouched by the Save.
  QCOMPARE(h.collections[0].gridLayout.horizontalGridHeight, 3);
  QCOMPARE(h.collections[0].gridLayout.horizontalGridHeightSidebarHidden, 5);
  QCOMPARE(h.sm.lastSaved[0].gridLayout.horizontalGridHeight, 3);
  QCOMPARE(h.sm.lastSaved[0].gridLayout.horizontalGridHeightSidebarHidden, 5);
}

void TestSettingsDialogController::cornerRadiusSaveKeepsHiddenPaneWidth() {
  // Pane hidden in Expand mode, Grid Width 4 / Grid Width (pane hidden) 6:
  // only Corner Radius changes, Save, then OK. The grid must stay at 6 both
  // right after Save and after the post-dialog apply — never reflow to 4.
  GridHarness h(6, /*paneHidden=*/true);
  QCOMPARE(h.appliedColumns(), 6);
  int columnsAtSave = -1;
  h.controller.openSettingsDialog(
      h.makeContext([](CollectionConfig &c) { c.gridLayout.cornerRadius += 4; },
                    [&h, &columnsAtSave]() { columnsAtSave = h.appliedColumns(); }));
  QCOMPARE(columnsAtSave, 6);
  QCOMPARE(h.appliedColumns(), 6);
  QCOMPARE(h.scroll.getCurrentGridWidth(), 6);
  QVERIFY(h.scroll.sidebarShrinkingActive());
  // Neither stored width was rewritten.
  QCOMPARE(h.collections[0].gridLayout.gridWidth, 4);
  QCOMPARE(h.collections[0].gridLayout.gridWidthSidebarHidden, 6);
  QCOMPARE(h.sm.lastSaved[0].gridLayout.gridWidth, 4);
  QCOMPARE(h.sm.lastSaved[0].gridLayout.gridWidthSidebarHidden, 6);
  // Opening the pane afterwards still lands on the usual width.
  h.scroll.setSidebarShrinkingActive(false);
  QCOMPARE(h.scroll.getCurrentGridWidth(), 4);
}

void TestSettingsDialogController::cornerRadiusSaveWithPaneOpenKeepsPrimaryWidth() {
  GridHarness h(6, /*paneHidden=*/false);
  QCOMPARE(h.appliedColumns(), 4);
  int columnsAtSave = -1;
  h.controller.openSettingsDialog(
      h.makeContext([](CollectionConfig &c) { c.gridLayout.cornerRadius += 4; },
                    [&h, &columnsAtSave]() { columnsAtSave = h.appliedColumns(); }));
  QCOMPARE(columnsAtSave, 4);
  QCOMPARE(h.appliedColumns(), 4);
  QCOMPARE(h.scroll.getCurrentGridWidth(), 4);
}

void TestSettingsDialogController::cornerRadiusSaveWithoutHiddenOverrideKeepsPrimaryWidth() {
  // Hidden override unset (0 = inherit): the pane-hidden grid uses Grid Width.
  GridHarness h(0, /*paneHidden=*/true);
  QCOMPARE(h.appliedColumns(), 4);
  int columnsAtSave = -1;
  h.controller.openSettingsDialog(
      h.makeContext([](CollectionConfig &c) { c.gridLayout.cornerRadius += 4; },
                    [&h, &columnsAtSave]() { columnsAtSave = h.appliedColumns(); }));
  QCOMPARE(columnsAtSave, 4);
  QCOMPARE(h.appliedColumns(), 4);
  QCOMPARE(h.collections[0].gridLayout.gridWidthSidebarHidden, 0);
}

void TestSettingsDialogController::gridWidthEditWithPaneHiddenKeepsHiddenPaneWidth() {
  // The direct width-edit branch: Grid Width 4 -> 5 while the pane is hidden
  // with the override at 6. The live grid keeps 6; the new primary applies
  // once the pane opens.
  GridHarness h(6, /*paneHidden=*/true);
  int columnsAtSave = -1;
  h.controller.openSettingsDialog(
      h.makeContext([](CollectionConfig &c) { c.gridLayout.gridWidth = 5; },
                    [&h, &columnsAtSave]() { columnsAtSave = h.appliedColumns(); }));
  QCOMPARE(columnsAtSave, 6);
  QCOMPARE(h.appliedColumns(), 6);
  QCOMPARE(h.scroll.getCurrentGridWidth(), 6);
  QCOMPARE(h.collections[0].gridLayout.gridWidth, 5);
  QCOMPARE(h.collections[0].gridLayout.gridWidthSidebarHidden, 6);
  h.scroll.setSidebarShrinkingActive(false);
  QCOMPARE(h.scroll.getCurrentGridWidth(), 5);
}

void TestSettingsDialogController::cancelAfterMidSessionSaveStillMigratesAndPurges() {
  Harness h;
  h.controller.openSettingsDialog(h.makeContext(true, QDialog::Rejected));

  // The saved rename must be reconciled even though the dialog was cancelled:
  // old-uuid rows migrate to the new uuid, then orphans are purged against
  // the persisted list.
  QCOMPARE(h.db.migrations.size(), 1);
  QCOMPARE(h.db.migrations[0].first,
           CollectionUtils::computeCollectionUuid(QStringLiteral("Old"),
                                                  QStringLiteral("/tmp/kartend-test-media")));
  QCOMPARE(h.db.migrations[0].second,
           CollectionUtils::computeCollectionUuid(QStringLiteral("New"),
                                                  QStringLiteral("/tmp/kartend-test-media")));
  QCOMPARE(h.db.purgeCount, 1);
  QCOMPARE(h.db.lastPurgeList.size(), 1);
  QCOMPARE(h.db.lastPurgeList[0].name, QStringLiteral("New"));
  // The live list keeps the persisted state.
  QCOMPARE(h.collections[0].name, QStringLiteral("New"));
  QCOMPARE(h.currentIndex, 0);
}

void TestSettingsDialogController::cancelWithoutAnySaveSkipsReconciliation() {
  Harness h;
  h.controller.openSettingsDialog(h.makeContext(false, QDialog::Rejected));

  // Nothing was persisted during the session, so the cancel must remain a
  // pure no-op: no migration, no purge, live list untouched.
  QVERIFY(h.db.migrations.isEmpty());
  QCOMPARE(h.db.purgeCount, 0);
  QCOMPARE(h.collections[0].name, QStringLiteral("Old"));
}

void TestSettingsDialogController::acceptedSessionStillMigratesAndPurges() {
  Harness h;
  h.controller.openSettingsDialog(h.makeContext(false, QDialog::Accepted));

  // Regression guard for the accepted path: identical reconciliation.
  QCOMPARE(h.db.migrations.size(), 1);
  QCOMPARE(h.db.purgeCount, 1);
  QCOMPARE(h.collections[0].name, QStringLiteral("New"));
}

void TestSettingsDialogController::renameWithSharedDirectorySiblingMigratesOnlyThatRow() {
  // Two collections share one media directory; the SECOND is renamed. The
  // old first-mediaDirectory-match pairing latched onto the first sibling
  // and migrated ITS uuid onto the renamed row's new uuid, corrupting the
  // untouched sibling's history. Index pairing must migrate exactly the
  // renamed row.
  Harness h;
  const QString sharedDir = QStringLiteral("/tmp/kartend-test-shared");
  h.collections = {makeCollection(QStringLiteral("SiblingA"), sharedDir),
                   makeCollection(QStringLiteral("SiblingB"), sharedDir)};

  h.controller.openSettingsDialog(h.makeMutatedContext(
      [](QList<CollectionConfig> &list) { list[1].name = QStringLiteral("SiblingB2"); }));

  QCOMPARE(h.db.migrations.size(), 1);
  QCOMPARE(h.db.migrations[0].first,
           CollectionUtils::computeCollectionUuid(QStringLiteral("SiblingB"), sharedDir));
  QCOMPARE(h.db.migrations[0].second,
           CollectionUtils::computeCollectionUuid(QStringLiteral("SiblingB2"), sharedDir));
}

void TestSettingsDialogController::multiRenameMigratesEachRow() {
  // Two renames in one session: the old pairing broke out of the inner loop
  // after the first directory match, so only one row could ever migrate.
  Harness h;
  const QString dirA = QStringLiteral("/tmp/kartend-test-a");
  const QString dirB = QStringLiteral("/tmp/kartend-test-b");
  h.collections = {makeCollection(QStringLiteral("A"), dirA),
                   makeCollection(QStringLiteral("B"), dirB)};

  h.controller.openSettingsDialog(h.makeMutatedContext([](QList<CollectionConfig> &list) {
    list[0].name = QStringLiteral("A2");
    list[1].name = QStringLiteral("B2");
  }));

  QCOMPARE(h.db.migrations.size(), 2);
  QCOMPARE(h.db.migrations[0].first,
           CollectionUtils::computeCollectionUuid(QStringLiteral("A"), dirA));
  QCOMPARE(h.db.migrations[0].second,
           CollectionUtils::computeCollectionUuid(QStringLiteral("A2"), dirA));
  QCOMPARE(h.db.migrations[1].first,
           CollectionUtils::computeCollectionUuid(QStringLiteral("B"), dirB));
  QCOMPARE(h.db.migrations[1].second,
           CollectionUtils::computeCollectionUuid(QStringLiteral("B2"), dirB));
}

void TestSettingsDialogController::renamePlusDirectoryChangeStillMigrates() {
  // Renaming AND moving a collection in one session changes both uuid
  // inputs; the old directory-anchored pairing skipped it entirely and the
  // orphan purge dropped the history. With index pairing (and no structural
  // shift) the row still migrates.
  Harness h;
  h.collections = {makeCollection(QStringLiteral("Old"), QStringLiteral("/tmp/kartend-test-old")),
                   makeCollection(QStringLiteral("Stable"), QStringLiteral("/tmp/kartend-test-s"))};

  h.controller.openSettingsDialog(h.makeMutatedContext([](QList<CollectionConfig> &list) {
    list[0].name = QStringLiteral("New");
    list[0].mediaDirectory = QStringLiteral("/tmp/kartend-test-new");
  }));

  QCOMPARE(h.db.migrations.size(), 1);
  QCOMPARE(h.db.migrations[0].first,
           CollectionUtils::computeCollectionUuid(QStringLiteral("Old"),
                                                  QStringLiteral("/tmp/kartend-test-old")));
  QCOMPARE(h.db.migrations[0].second,
           CollectionUtils::computeCollectionUuid(QStringLiteral("New"),
                                                  QStringLiteral("/tmp/kartend-test-new")));
}

QTEST_MAIN(TestSettingsDialogController)
#include "test_settingsdialogcontroller.moc"
