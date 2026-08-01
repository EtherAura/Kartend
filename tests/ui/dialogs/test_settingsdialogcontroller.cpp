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
#include "collection/typehelpers.h"
#include "isettingsdialog.h"
#include "settingsdialogcontroller.h"

#include "../../integration/mocks/mockdatabasemanager.h"
#include "../../integration/mocks/mocksettingsmanager.h"

#include <functional>
#include <memory>
#include <QDialog>
#include <QPair>
#include <QTest>

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
        [saveDuringExec, execResult](
            QWidget *, const QList<CollectionConfig> &initialCollections, int,
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
  SettingsDialogContext
  makeMutatedContext(std::function<void(QList<CollectionConfig> &)> mutate) {
    SettingsDialogContext context;
    context.collections = &collections;
    context.currentCollectionIndex = &currentIndex;
    context.databaseManager = &db;
    context.createSettingsDialog =
        [mutate = std::move(mutate)](QWidget *,
                                     const QList<CollectionConfig> &initialCollections, int,
                                     std::function<void(const QList<CollectionConfig> &)>,
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
};

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
  QCOMPARE(h.db.migrations[0].first, CollectionUtils::computeCollectionUuid(QStringLiteral("A"), dirA));
  QCOMPARE(h.db.migrations[0].second,
           CollectionUtils::computeCollectionUuid(QStringLiteral("A2"), dirA));
  QCOMPARE(h.db.migrations[1].first, CollectionUtils::computeCollectionUuid(QStringLiteral("B"), dirB));
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
