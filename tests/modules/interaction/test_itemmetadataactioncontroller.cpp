// Unit coverage for ItemMetadataActionController — the user-curation mutation
// surface reachable from the right-click context menu (pin / hide / continue-
// later toggles, manual-path + launcher-override setters, the edit-metadata
// dialog round-trip). The controller issues no SQL of its own; it orchestrates
// IDatabaseManager + IDetailsPaneManager, so it is exercised against a
// capturing IDatabaseManager double (the established pattern, cf.
// test_scrapepersistence) plus a spy IDetailsPaneManager — no real SQLite
// needed for these orchestration assertions. (Kartend audit T-01.)
#include <functional>
#include <optional>

#include <QDir>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTest>

#include "../../integration/mocks/mockdatabasemanager.h"
#include "applicationcontext.h"
#include "collection/collectionconfig.h"
#include "collection/typehelpers.h"
#include "idetailspanemanager.h"
#include "itemmetadata.h"
#include "itemmetadataactioncontroller.h"
#include "pathutils.h"

namespace {

/// Records refreshSidebarMetadataImmediate() calls; every other
/// IDetailsPaneManager method is an inert stub.
class SpyDetailsPane : public IDetailsPaneManager {
public:
  void toggleSidebar() override {}
  void updateSidebarMetadata(ItemWidget *) override {}
  void updateSidebarMetadata(const QString &, const QString &) override {}
  void refreshSidebarMetadataImmediate() override { ++refreshCount; }
  void applySidebarStateForCollection(int, bool) override {}
  void updateSidebarLayout(int) override {}
  [[nodiscard]] bool isSidebarVisible() const override { return false; }
  [[nodiscard]] IDetailsPane *sidebarWidget() const override { return nullptr; }
  [[nodiscard]] const ItemContext &currentItemContext() const override { return m_ctx; }

  ItemContext m_ctx;
  int refreshCount = 0;
};

/// In-memory item_metadata store: loadItemMetadata replays previously-saved
/// rows (so toggle-flips and merge-with-existing work), saveItemMetadata
/// captures, and getCollectionIndexForFile is caller-controllable so both the
/// owning-collection and the current-collection fallback can be driven.
class FakeMetadataDb : public KartendTest::MockDatabaseManager {
public:
  [[nodiscard]] int getCollectionIndexForFile(const QString &filePath) const override {
    return forcedIndex.value(filePath, -1);
  }
  [[nodiscard]] ItemMetadataStore::ItemMetadata
  loadItemMetadata(const QString &uuid, const QString &path) const override {
    return rows.value(rowKey(uuid, path));
  }
  bool saveItemMetadata(const ItemMetadataStore::ItemMetadata &md) override {
    rows.insert(rowKey(md.collectionUuid, md.path), md);
    saves.append(md);
    return saveResult;
  }

  static QString rowKey(const QString &uuid, const QString &path) {
    return uuid + QLatin1Char('\x1f') + path;
  }

  QHash<QString, ItemMetadataStore::ItemMetadata> rows;
  QHash<QString, int> forcedIndex; // filePath -> owning collection index
  QList<ItemMetadataStore::ItemMetadata> saves;
  bool saveResult = true;
};

/// One fully-wired controller + its borrowed collaborators, alive for the
/// duration of a single test slot.
struct Harness {
  FakeMetadataDb db;
  SpyDetailsPane pane;
  ApplicationContext ctx;
  QList<CollectionConfig> collections;
  int currentIndex = 0;
  ItemMetadataActionController controller;

  using DialogRunner = std::function<std::optional<EditMetadataPayload>(
      const QString &, const EditMetadataPayload &)>;

  Harness(DialogRunner dialog = {}, int launcherCount = 1) {
    CollectionConfig c;
    c.name = QStringLiteral("Coll");
    c.mediaDirectory = QDir::tempPath() + QStringLiteral("/kart_t01_media");
    // launcherCount() always counts the primary slot (>= 1); append additional
    // launchers so the override-clamp upper bound is exercised.
    for (int i = 1; i < launcherCount; ++i) {
      c.launcher.additionalLaunchers.append(LauncherConfig{});
    }
    collections.append(c);

    ctx.managers.databaseManager = &db;
    ctx.managers.detailsPaneManager = &pane;

    ItemMetadataActionControllerSetup setup;
    setup.ctx = &ctx;
    setup.collections = &collections;
    setup.currentCollectionIndex = &currentIndex;
    setup.runEditMetadataDialog = std::move(dialog);
    controller.setupReferences(setup);
  }

  [[nodiscard]] QString uuidFor(int index) const {
    const CollectionConfig &c = collections[index];
    return CollectionUtils::computeCollectionUuid(
        c.name, PathUtils::validateAndExpandPath(c.mediaDirectory, c.name));
  }
  [[nodiscard]] QString currentUuid() const { return uuidFor(currentIndex); }
};

} // namespace

class TestItemMetadataActionController : public QObject {
  Q_OBJECT
private slots:
  void togglePinnedFlipsAndStampsUser();
  void toggleHiddenFlipsBackOnSecondCall();
  void toggleContinueLaterFlips();
  void setManualPathPersistsAndStampsUser();
  void launcherOverrideClampsAboveRange();
  void launcherOverrideNegativeClears();
  void resolvesOwningCollectionUuidWhenFileResolves();
  void fallsBackToCurrentCollectionWhenFileUnknown();
  void editMetadataAppliesDialogPayloadAndStampsUser();
  void editMetadataCancelledLeavesNoSaveAndNoRefresh();
  void saveFailureSkipsSidebarRefresh();
  void noOpWhenResolvedCollectionIndexInvalid();
};

void TestItemMetadataActionController::togglePinnedFlipsAndStampsUser() {
  Harness h;
  const QString file = QStringLiteral("/media/game.rom");

  h.controller.toggleItemPinned(file);

  QCOMPARE(h.db.saves.size(), 1);
  const auto &saved = h.db.saves.last();
  QVERIFY(saved.isPinned);
  QCOMPARE(saved.source, QStringLiteral("user"));
  QCOMPARE(saved.collectionUuid, h.currentUuid());
  QCOMPARE(saved.path, file);
  QCOMPARE(h.pane.refreshCount, 1); // sidebar refreshed on a successful save
}

void TestItemMetadataActionController::toggleHiddenFlipsBackOnSecondCall() {
  Harness h;
  const QString file = QStringLiteral("/media/game.rom");

  h.controller.toggleItemHidden(file);
  QVERIFY(h.db.saves.last().isHidden); // false -> true
  h.controller.toggleItemHidden(file);
  QVERIFY(!h.db.saves.last().isHidden); // reloads the saved row, true -> false
  QCOMPARE(h.db.saves.size(), 2);
}

void TestItemMetadataActionController::toggleContinueLaterFlips() {
  Harness h;
  h.controller.toggleItemContinueLater(QStringLiteral("/media/game.rom"));
  QVERIFY(h.db.saves.last().continueLater);
  QCOMPARE(h.db.saves.last().source, QStringLiteral("user"));
}

void TestItemMetadataActionController::setManualPathPersistsAndStampsUser() {
  Harness h;
  h.controller.setItemManualPath(QStringLiteral("/media/game.rom"),
                                 QStringLiteral("/docs/manual.pdf"));
  QCOMPARE(h.db.saves.size(), 1);
  QCOMPARE(h.db.saves.last().manualPath, QStringLiteral("/docs/manual.pdf"));
  QCOMPARE(h.db.saves.last().source, QStringLiteral("user"));
}

void TestItemMetadataActionController::launcherOverrideClampsAboveRange() {
  Harness h({}, /*launcherCount=*/2);
  const int maxIndex = h.collections[0].launcher.launcherCount() - 1;
  QVERIFY(maxIndex >= 1); // sanity: two launchers configured

  h.controller.setItemLauncherOverride(QStringLiteral("/media/game.rom"), maxIndex + 9);

  QCOMPARE(h.db.saves.size(), 1);
  QCOMPARE(h.db.saves.last().launcherIndex, maxIndex); // clamped, not pinned past the end
}

void TestItemMetadataActionController::launcherOverrideNegativeClears() {
  Harness h;
  h.controller.setItemLauncherOverride(QStringLiteral("/media/game.rom"), -3);
  QCOMPARE(h.db.saves.size(), 1);
  QCOMPARE(h.db.saves.last().launcherIndex, -1); // negative => clear the override
}

void TestItemMetadataActionController::resolvesOwningCollectionUuidWhenFileResolves() {
  Harness h;
  // A second collection owns the file; getCollectionIndexForFile points at it,
  // so the metadata row must be keyed by the OWNING collection's uuid (not the
  // displayed/current one) — the showAllSubcollectionItems correctness path.
  CollectionConfig other;
  other.name = QStringLiteral("Other");
  other.mediaDirectory = QDir::tempPath() + QStringLiteral("/kart_t01_other");
  h.collections.append(other);
  const QString file = QStringLiteral("/other/game.rom");
  h.db.forcedIndex[file] = 1;

  h.controller.toggleItemPinned(file);

  QCOMPARE(h.db.saves.size(), 1);
  QCOMPARE(h.db.saves.last().collectionUuid, h.uuidFor(1));
  QVERIFY(h.db.saves.last().collectionUuid != h.uuidFor(0));
}

void TestItemMetadataActionController::fallsBackToCurrentCollectionWhenFileUnknown() {
  Harness h;
  // getCollectionIndexForFile returns -1 (unknown file) -> fall back to the
  // current collection index.
  h.controller.toggleItemPinned(QStringLiteral("/unknown/file.rom"));
  QCOMPARE(h.db.saves.size(), 1);
  QCOMPARE(h.db.saves.last().collectionUuid, h.currentUuid());
}

void TestItemMetadataActionController::editMetadataAppliesDialogPayloadAndStampsUser() {
  EditMetadataPayload returned;
  returned.notes = QStringLiteral("hand-written notes");
  returned.tags = {QStringLiteral("rpg"), QStringLiteral("favourite")};
  returned.rating = 4;
  returned.sourceUrl = QStringLiteral("https://example.com/game");

  QString seenTitle;
  Harness h(
      [&](const QString &title, const EditMetadataPayload &) -> std::optional<EditMetadataPayload> {
        seenTitle = title;
        return returned;
      });

  h.controller.editItemMetadata(QStringLiteral("/media/game.rom"), QStringLiteral("Great Game"));

  QCOMPARE(seenTitle, QStringLiteral("Great Game"));
  QCOMPARE(h.db.saves.size(), 1);
  const auto &saved = h.db.saves.last();
  QCOMPARE(saved.notes, QStringLiteral("hand-written notes"));
  QCOMPARE(ItemMetadataStore::parseTags(saved.tags),
           QStringList({QStringLiteral("rpg"), QStringLiteral("favourite")}));
  QCOMPARE(saved.rating, 4);
  QCOMPARE(saved.sourceUrl, QStringLiteral("https://example.com/game"));
  QCOMPARE(saved.source, QStringLiteral("user"));
  QCOMPARE(h.pane.refreshCount, 1);
}

void TestItemMetadataActionController::editMetadataCancelledLeavesNoSaveAndNoRefresh() {
  Harness h([](const QString &, const EditMetadataPayload &) -> std::optional<EditMetadataPayload> {
    return std::nullopt; // user dismissed the dialog
  });
  h.controller.editItemMetadata(QStringLiteral("/media/game.rom"), QStringLiteral("Game"));
  QCOMPARE(h.db.saves.size(), 0);
  QCOMPARE(h.pane.refreshCount, 0);
}

void TestItemMetadataActionController::saveFailureSkipsSidebarRefresh() {
  Harness h;
  h.db.saveResult = false;
  h.controller.toggleItemPinned(QStringLiteral("/media/game.rom"));
  QCOMPARE(h.db.saves.size(), 1);   // a save was attempted
  QCOMPARE(h.pane.refreshCount, 0); // but the failed save must not refresh the sidebar
}

void TestItemMetadataActionController::noOpWhenResolvedCollectionIndexInvalid() {
  Harness h;
  h.currentIndex = 5; // out of range; unknown file -> fallback to 5 -> invalid -> empty uuid
  h.controller.toggleItemPinned(QStringLiteral("/media/game.rom"));
  QCOMPARE(h.db.saves.size(), 0); // resolveOwningUuid bails, no write
  QCOMPARE(h.pane.refreshCount, 0);
}

QTEST_GUILESS_MAIN(TestItemMetadataActionController)
#include "test_itemmetadataactioncontroller.moc"
