// Unit coverage for PlaylistMenuController — the playlist actions reachable
// from the right-click context menu: add-to-playlist (existing or new),
// smart-playlist create/edit round-trips, rename / delete-with-confirmation,
// and the JSON/M3U import/export flows. The controller issues no SQL of its
// own; it orchestrates IPlaylistManager + the owner-supplied dialog runners,
// so it is exercised against a capturing IPlaylistManager double plus stubbed
// DialogRunners (the established pattern, cf. test_itemmetadataactioncontroller
// — no modal ever opens headlessly).

#include <functional>
#include <optional>

#include <QList>
#include <QString>
#include <QStringList>
#include <QTest>

#include "applicationcontext.h"
#include "collection/collectionconfig.h"
#include "errorutils.h"
#include "iplaylistmanager.h"
#include "playlistmenucontroller.h"
#include "smartfilter.h"

namespace {

/// Capturing IPlaylistManager double: every mutation records its arguments;
/// return values are caller-configurable so both the success and error paths
/// can be driven. Loaders replay configured data.
class FakePlaylistManager : public IPlaylistManager {
public:
  bool initialize() override { return true; }

  ErrorUtils::Result<QString> createPlaylist(const QString &name,
                                             const QString &parentCollectionUuid = QString(),
                                             const QString &reservedKind = QString()) override {
    Q_UNUSED(parentCollectionUuid)
    Q_UNUSED(reservedKind)
    createdNames.append(name);
    if (failCreate) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::DatabaseQueryFailed,
                                             QStringLiteral("forced create failure"),
                                             QStringLiteral("FakePlaylistManager"));
    }
    return QStringLiteral("pl-new");
  }

  ErrorUtils::Result<QString>
  createSmartPlaylist(const QString &name, const SmartFilter::FilterSet &filterSet,
                      const QString &parentCollectionUuid = QString()) override {
    Q_UNUSED(parentCollectionUuid)
    createdSmartNames.append(name);
    lastSmartFilterSet = filterSet;
    if (failCreateSmart) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::DatabaseQueryFailed,
                                             QStringLiteral("forced smart-create failure"),
                                             QStringLiteral("FakePlaylistManager"));
    }
    return QStringLiteral("pl-smart");
  }

  bool updateSmartFilter(const QString &id, const SmartFilter::FilterSet &filterSet) override {
    updatedFilterIds.append(id);
    lastSmartFilterSet = filterSet;
    return updateFilterResult;
  }

  [[nodiscard]] ErrorUtils::Result<SmartFilter::FilterSet>
  loadSmartFilter(const QString &id) const override {
    Q_UNUSED(id)
    if (failLoadFilter) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::DatabaseQueryFailed,
                                             QStringLiteral("forced filter-load failure"),
                                             QStringLiteral("FakePlaylistManager"));
    }
    return storedFilterSet;
  }

  bool renamePlaylist(const QString &id, const QString &newName) override {
    renames.append({id, newName});
    return true;
  }

  bool deletePlaylist(const QString &id) override {
    deletedIds.append(id);
    return deleteResult;
  }

  bool addItem(const QString &playlistId, const QString &sourceCollectionUuid,
               const QString &sourcePath) override {
    addedItems.append({playlistId, sourceCollectionUuid, sourcePath});
    return true;
  }

  bool removeItem(const QString &playlistId, const QString &sourceCollectionUuid,
                  const QString &sourcePath) override {
    removedItems.append({playlistId, sourceCollectionUuid, sourcePath});
    return true;
  }

  [[nodiscard]] QList<PlaylistRow> loadAll() const override { return rows; }
  [[nodiscard]] QList<PlaylistItemRef> loadItems(const QString &) const override { return {}; }
  [[nodiscard]] bool containsItem(const QString &, const QString &,
                                  const QString &) const override {
    return false;
  }

  [[nodiscard]] ErrorUtils::Result<int> exportToJson(const QString &playlistId,
                                                     const QString &outPath) const override {
    exportedJsonPaths.append(outPath);
    Q_UNUSED(playlistId)
    if (failExport) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileWriteError,
                                             QStringLiteral("forced export failure"),
                                             QStringLiteral("FakePlaylistManager"));
    }
    return 3;
  }

  [[nodiscard]] ErrorUtils::Result<int> exportToM3U(const QString &playlistId,
                                                    const QString &outPath) const override {
    exportedM3uPaths.append(outPath);
    Q_UNUSED(playlistId)
    return 2;
  }

  [[nodiscard]] ErrorUtils::Result<QString> importFromJson(const QString &inPath,
                                                           const QString &nameOverride = QString(),
                                                           int *outSkipped = nullptr) override {
    Q_UNUSED(nameOverride)
    if (outSkipped) {
      *outSkipped = 0;
    }
    importedJsonPaths.append(inPath);
    return QStringLiteral("pl-imported-json");
  }

  [[nodiscard]] ErrorUtils::Result<QString> importFromM3U(const QString &inPath,
                                                          const QString &playlistName,
                                                          int *outSkipped = nullptr,
                                                          int *outAmbiguous = nullptr) override {
    importedM3uPaths.append(inPath);
    importedM3uNames.append(playlistName);
    if (outSkipped) {
      *outSkipped = m3uSkipped;
    }
    if (outAmbiguous) {
      *outAmbiguous = 0;
    }
    return QStringLiteral("pl-imported-m3u");
  }

  QString ensureFavoritesPlaylist(const QString & = QStringLiteral("Favorites")) override {
    return QStringLiteral("pl-fav");
  }
  [[nodiscard]] QString favoritesPlaylistId() const override { return QStringLiteral("pl-fav"); }

  struct ItemCall {
    QString playlistId;
    QString uuid;
    QString path;
  };
  struct RenameCall {
    QString id;
    QString name;
  };

  // Configurable behavior.
  bool failCreate = false;
  bool failCreateSmart = false;
  bool failLoadFilter = false;
  bool updateFilterResult = true;
  bool deleteResult = true;
  bool failExport = false;
  int m3uSkipped = 0;
  QList<PlaylistRow> rows;
  SmartFilter::FilterSet storedFilterSet{SmartFilter::MatchMode::All, {SmartFilter::Filter{}}};

  // Captured calls.
  QStringList createdNames;
  QStringList createdSmartNames;
  QStringList updatedFilterIds;
  SmartFilter::FilterSet lastSmartFilterSet;
  QList<RenameCall> renames;
  QStringList deletedIds;
  QList<ItemCall> addedItems;
  QList<ItemCall> removedItems;
  mutable QStringList exportedJsonPaths;
  mutable QStringList exportedM3uPaths;
  QStringList importedJsonPaths;
  QStringList importedM3uPaths;
  QStringList importedM3uNames;
};

/// One fully-wired controller + capturing collaborators per test slot.
/// The dialog runners are pre-stubbed to canned answers so no modal opens;
/// individual tests overwrite them before acting.
struct Harness {
  FakePlaylistManager playlists;
  ApplicationContext ctx;
  QList<CollectionConfig> collections;
  int currentIndex = 0;
  PlaylistMenuController controller;

  QStringList warnings;
  QStringList infos;

  Harness() {
    ctx.managers.playlistManager = &playlists;

    PlaylistMenuControllerSetup setup;
    setup.ctx = &ctx;
    setup.collections = &collections;
    setup.currentCollectionIndex = &currentIndex;
    setup.dialogs.warn = [this](const QString &title, const QString &) { warnings << title; };
    setup.dialogs.info = [this](const QString &title, const QString &) { infos << title; };
    setup.dialogs.confirm = [](const QString &, const QString &) { return true; };
    setup.dialogs.getText = [](const QString &, const QString &,
                               const QString &) -> std::optional<QString> { return std::nullopt; };
    setup.dialogs.getOpenFileName = [](const QString &, const QString &, const QString &) {
      return QString();
    };
    setup.dialogs.getSaveFileName = [](const QString &, const QString &, const QString &) {
      return QString();
    };
    m_setup = setup;
    controller.setupReferences(m_setup);
  }

  /// Re-applies the setup after a test tweaks a runner (setupReferences
  /// copies the DialogRunners by value, so edits must be pushed through).
  void rewire() { controller.setupReferences(m_setup); }

  PlaylistMenuControllerSetup m_setup;
};

} // namespace

class TestPlaylistMenuController : public QObject {
  Q_OBJECT
private slots:
  // addItemToNewPlaylist
  void newPlaylistCancelledCreatesNothing();
  void newPlaylistBlankNameCreatesNothing();
  void newPlaylistCreatesAndAddsFirstItem();
  void newPlaylistCreateFailureAddsNoItem();

  // addItemToPlaylist
  void addItemGuardsEmptyIds();
  void addItemForwardsTriple();

  // renamePlaylistDialog
  void renameCancelledDoesNothing();
  void renameUnchangedNameDoesNothing();
  void renameForwardsNewName();

  // deletePlaylistConfirm
  void deleteDeclinedKeepsPlaylist();
  void deleteConfirmedDeletes();

  // exportPlaylistToFile
  void exportCancelledExportsNothing();
  void exportAppendsMissingJsonExtension();
  void exportKeepsExistingExtensionCaseInsensitive();
  void exportM3uShowsLossyInteropNote();
  void exportFailureWarns();

  // importPlaylistFromFile
  void importSniffsJsonByExtension();
  void importDefaultsToM3uForOtherExtensions();
  void importM3uReportsSkippedEntries();

  // smart playlists
  void smartCreateCancelledCreatesNothing();
  void smartCreateForwardsNameAndFilter();
  void smartCreateFailureWarns();
  void smartEditLoadFailureWarnsAndSkipsDialog();
  void smartEditUpdatesFilterAndRenamesOnNameChange();
  void smartEditKeepsNameWhenUnchanged();
};

// ------------------------------ addItemToNewPlaylist ------------------------

void TestPlaylistMenuController::newPlaylistCancelledCreatesNothing() {
  Harness h; // default getText returns nullopt (cancel)
  h.controller.addItemToNewPlaylist(QStringLiteral("uuid-a"), QStringLiteral("/m/a.mp4"));
  QVERIFY(h.playlists.createdNames.isEmpty());
  QVERIFY(h.playlists.addedItems.isEmpty());
}

void TestPlaylistMenuController::newPlaylistBlankNameCreatesNothing() {
  Harness h;
  h.m_setup.dialogs.getText = [](const QString &, const QString &,
                                 const QString &) -> std::optional<QString> {
    return QStringLiteral("   "); // accepted but whitespace-only
  };
  h.rewire();
  h.controller.addItemToNewPlaylist(QStringLiteral("uuid-a"), QStringLiteral("/m/a.mp4"));
  QVERIFY(h.playlists.createdNames.isEmpty());
}

void TestPlaylistMenuController::newPlaylistCreatesAndAddsFirstItem() {
  Harness h;
  h.m_setup.dialogs.getText = [](const QString &, const QString &,
                                 const QString &) -> std::optional<QString> {
    return QStringLiteral("Road Trip");
  };
  h.rewire();
  h.controller.addItemToNewPlaylist(QStringLiteral("uuid-a"), QStringLiteral("/m/a.mp4"));
  QCOMPARE(h.playlists.createdNames, QStringList{QStringLiteral("Road Trip")});
  QCOMPARE(h.playlists.addedItems.size(), 1);
  QCOMPARE(h.playlists.addedItems[0].playlistId, QStringLiteral("pl-new"));
  QCOMPARE(h.playlists.addedItems[0].uuid, QStringLiteral("uuid-a"));
  QCOMPARE(h.playlists.addedItems[0].path, QStringLiteral("/m/a.mp4"));
}

void TestPlaylistMenuController::newPlaylistCreateFailureAddsNoItem() {
  Harness h;
  h.playlists.failCreate = true;
  h.m_setup.dialogs.getText = [](const QString &, const QString &,
                                 const QString &) -> std::optional<QString> {
    return QStringLiteral("Doomed");
  };
  h.rewire();
  h.controller.addItemToNewPlaylist(QStringLiteral("uuid-a"), QStringLiteral("/m/a.mp4"));
  QCOMPARE(h.playlists.createdNames.size(), 1);
  QVERIFY(h.playlists.addedItems.isEmpty());
}

// ------------------------------ addItemToPlaylist ---------------------------

void TestPlaylistMenuController::addItemGuardsEmptyIds() {
  Harness h;
  h.controller.addItemToPlaylist(QString(), QStringLiteral("uuid-a"), QStringLiteral("/m/a.mp4"));
  h.controller.addItemToPlaylist(QStringLiteral("pl-1"), QStringLiteral("uuid-a"), QString());
  QVERIFY(h.playlists.addedItems.isEmpty());
}

void TestPlaylistMenuController::addItemForwardsTriple() {
  Harness h;
  h.controller.addItemToPlaylist(QStringLiteral("pl-1"), QStringLiteral("uuid-a"),
                                 QStringLiteral("/m/a.mp4"));
  QCOMPARE(h.playlists.addedItems.size(), 1);
  QCOMPARE(h.playlists.addedItems[0].playlistId, QStringLiteral("pl-1"));
}

// ------------------------------ renamePlaylistDialog ------------------------

void TestPlaylistMenuController::renameCancelledDoesNothing() {
  Harness h; // default getText cancels
  h.controller.renamePlaylistDialog(QStringLiteral("pl-1"), QStringLiteral("Old"));
  QVERIFY(h.playlists.renames.isEmpty());
}

void TestPlaylistMenuController::renameUnchangedNameDoesNothing() {
  Harness h;
  h.m_setup.dialogs.getText = [](const QString &, const QString &,
                                 const QString &initial) -> std::optional<QString> {
    return initial; // user hit OK without editing
  };
  h.rewire();
  h.controller.renamePlaylistDialog(QStringLiteral("pl-1"), QStringLiteral("Old"));
  QVERIFY(h.playlists.renames.isEmpty());
}

void TestPlaylistMenuController::renameForwardsNewName() {
  Harness h;
  h.m_setup.dialogs.getText = [](const QString &, const QString &,
                                 const QString &) -> std::optional<QString> {
    return QStringLiteral("New Name");
  };
  h.rewire();
  h.controller.renamePlaylistDialog(QStringLiteral("pl-1"), QStringLiteral("Old"));
  QCOMPARE(h.playlists.renames.size(), 1);
  QCOMPARE(h.playlists.renames[0].id, QStringLiteral("pl-1"));
  QCOMPARE(h.playlists.renames[0].name, QStringLiteral("New Name"));
}

// ------------------------------ deletePlaylistConfirm -----------------------

void TestPlaylistMenuController::deleteDeclinedKeepsPlaylist() {
  Harness h;
  h.m_setup.dialogs.confirm = [](const QString &, const QString &) { return false; };
  h.rewire();
  h.controller.deletePlaylistConfirm(QStringLiteral("pl-1"), QStringLiteral("Mixtape"));
  QVERIFY(h.playlists.deletedIds.isEmpty());
}

void TestPlaylistMenuController::deleteConfirmedDeletes() {
  Harness h; // default confirm returns true
  h.controller.deletePlaylistConfirm(QStringLiteral("pl-1"), QStringLiteral("Mixtape"));
  QCOMPARE(h.playlists.deletedIds, QStringList{QStringLiteral("pl-1")});
}

// ------------------------------ exportPlaylistToFile ------------------------

void TestPlaylistMenuController::exportCancelledExportsNothing() {
  Harness h; // default getSaveFileName returns empty (cancel)
  h.controller.exportPlaylistToFile(QStringLiteral("pl-1"), QStringLiteral("Mix"), /*asJson=*/true);
  QVERIFY(h.playlists.exportedJsonPaths.isEmpty());
  QVERIFY(h.playlists.exportedM3uPaths.isEmpty());
}

void TestPlaylistMenuController::exportAppendsMissingJsonExtension() {
  Harness h;
  h.m_setup.dialogs.getSaveFileName = [](const QString &, const QString &, const QString &) {
    return QStringLiteral("/tmp/kart_pl_export/mix");
  };
  h.rewire();
  h.controller.exportPlaylistToFile(QStringLiteral("pl-1"), QStringLiteral("Mix"), /*asJson=*/true);
  QCOMPARE(h.playlists.exportedJsonPaths,
           QStringList{QStringLiteral("/tmp/kart_pl_export/mix.json")});
  QCOMPARE(h.infos.size(), 1); // completion dialog
}

void TestPlaylistMenuController::exportKeepsExistingExtensionCaseInsensitive() {
  Harness h;
  h.m_setup.dialogs.getSaveFileName = [](const QString &, const QString &, const QString &) {
    return QStringLiteral("/tmp/kart_pl_export/mix.JSON");
  };
  h.rewire();
  h.controller.exportPlaylistToFile(QStringLiteral("pl-1"), QStringLiteral("Mix"), /*asJson=*/true);
  QCOMPARE(h.playlists.exportedJsonPaths,
           QStringList{QStringLiteral("/tmp/kart_pl_export/mix.JSON")});
}

void TestPlaylistMenuController::exportM3uShowsLossyInteropNote() {
  Harness h;
  h.m_setup.dialogs.getSaveFileName = [](const QString &, const QString &, const QString &) {
    return QStringLiteral("/tmp/kart_pl_export/mix.m3u");
  };
  QString infoBody;
  h.m_setup.dialogs.info = [&infoBody](const QString &, const QString &text) { infoBody = text; };
  h.rewire();
  h.controller.exportPlaylistToFile(QStringLiteral("pl-1"), QStringLiteral("Mix"),
                                    /*asJson=*/false);
  QCOMPARE(h.playlists.exportedM3uPaths,
           QStringList{QStringLiteral("/tmp/kart_pl_export/mix.m3u")});
  // The M3U completion dialog must carry the lossy-round-trip note
  // (Kartend-o84pt) pointing at the JSON format.
  QVERIFY(infoBody.contains(QStringLiteral("M3U")));
  QVERIFY(infoBody.contains(QStringLiteral(".json")));
}

void TestPlaylistMenuController::exportFailureWarns() {
  Harness h;
  h.playlists.failExport = true;
  h.m_setup.dialogs.getSaveFileName = [](const QString &, const QString &, const QString &) {
    return QStringLiteral("/tmp/kart_pl_export/mix.json");
  };
  h.rewire();
  h.controller.exportPlaylistToFile(QStringLiteral("pl-1"), QStringLiteral("Mix"), /*asJson=*/true);
  QCOMPARE(h.warnings.size(), 1);
  QVERIFY(h.infos.isEmpty());
}

// ------------------------------ importPlaylistFromFile ----------------------

void TestPlaylistMenuController::importSniffsJsonByExtension() {
  Harness h;
  h.m_setup.dialogs.getOpenFileName = [](const QString &, const QString &, const QString &) {
    return QStringLiteral("/tmp/kart_pl_import/list.json");
  };
  h.rewire();
  h.controller.importPlaylistFromFile();
  QCOMPARE(h.playlists.importedJsonPaths,
           QStringList{QStringLiteral("/tmp/kart_pl_import/list.json")});
  QVERIFY(h.playlists.importedM3uPaths.isEmpty());
}

void TestPlaylistMenuController::importDefaultsToM3uForOtherExtensions() {
  // Unusual extensions (e.g. .pls) at least try the path-per-line parser
  // instead of failing out; the playlist name defaults to the file base name.
  Harness h;
  h.m_setup.dialogs.getOpenFileName = [](const QString &, const QString &, const QString &) {
    return QStringLiteral("/tmp/kart_pl_import/oldies.pls");
  };
  h.rewire();
  h.controller.importPlaylistFromFile();
  QCOMPARE(h.playlists.importedM3uPaths,
           QStringList{QStringLiteral("/tmp/kart_pl_import/oldies.pls")});
  QCOMPARE(h.playlists.importedM3uNames, QStringList{QStringLiteral("oldies")});
  QVERIFY(h.playlists.importedJsonPaths.isEmpty());
}

void TestPlaylistMenuController::importM3uReportsSkippedEntries() {
  Harness h;
  h.playlists.m3uSkipped = 4;
  h.m_setup.dialogs.getOpenFileName = [](const QString &, const QString &, const QString &) {
    return QStringLiteral("/tmp/kart_pl_import/mix.m3u");
  };
  QString infoBody;
  h.m_setup.dialogs.info = [&infoBody](const QString &, const QString &text) { infoBody = text; };
  h.rewire();
  h.controller.importPlaylistFromFile();
  // The single completion dialog surfaces the skipped count so the user
  // knows the imported playlist may be shorter than the source.
  QVERIFY(infoBody.contains(QStringLiteral("4")));
}

// ------------------------------ smart playlists -----------------------------

void TestPlaylistMenuController::smartCreateCancelledCreatesNothing() {
  Harness h;
  h.m_setup.runSmartPlaylistDialog =
      [](const QString &, const std::optional<SmartFilter::FilterSet> &,
         const SmartPlaylistCollectionEntries &) -> std::optional<SmartPlaylistEdit> {
    return std::nullopt;
  };
  h.rewire();
  h.controller.createSmartPlaylistDialog();
  QVERIFY(h.playlists.createdSmartNames.isEmpty());
}

void TestPlaylistMenuController::smartCreateForwardsNameAndFilter() {
  Harness h;
  h.m_setup.runSmartPlaylistDialog =
      [](const QString &, const std::optional<SmartFilter::FilterSet> &,
         const SmartPlaylistCollectionEntries &) -> std::optional<SmartPlaylistEdit> {
    SmartPlaylistEdit edit;
    edit.name = QStringLiteral("Recent Reels");
    SmartFilter::Filter rule;
    rule.kind = SmartFilter::Kind::ByDateAdded;
    rule.days = 14;
    edit.filterSet.rules = {rule};
    return edit;
  };
  h.rewire();
  h.controller.createSmartPlaylistDialog();
  QCOMPARE(h.playlists.createdSmartNames, QStringList{QStringLiteral("Recent Reels")});
  QCOMPARE(h.playlists.lastSmartFilterSet.rules.size(), 1);
  QCOMPARE(h.playlists.lastSmartFilterSet.rules[0].kind, SmartFilter::Kind::ByDateAdded);
  QCOMPARE(h.playlists.lastSmartFilterSet.rules[0].days, 14);
  QVERIFY(h.warnings.isEmpty());
}

void TestPlaylistMenuController::smartCreateFailureWarns() {
  Harness h;
  h.playlists.failCreateSmart = true;
  h.m_setup.runSmartPlaylistDialog =
      [](const QString &, const std::optional<SmartFilter::FilterSet> &,
         const SmartPlaylistCollectionEntries &) -> std::optional<SmartPlaylistEdit> {
    SmartPlaylistEdit edit;
    edit.name = QStringLiteral("Doomed");
    return edit;
  };
  h.rewire();
  h.controller.createSmartPlaylistDialog();
  QCOMPARE(h.warnings.size(), 1);
}

void TestPlaylistMenuController::smartEditLoadFailureWarnsAndSkipsDialog() {
  Harness h;
  h.playlists.failLoadFilter = true;
  bool dialogRan = false;
  h.m_setup.runSmartPlaylistDialog =
      [&dialogRan](const QString &, const std::optional<SmartFilter::FilterSet> &,
                   const SmartPlaylistCollectionEntries &) -> std::optional<SmartPlaylistEdit> {
    dialogRan = true;
    return std::nullopt;
  };
  h.rewire();
  h.controller.editSmartPlaylistDialog(QStringLiteral("pl-smart"), QStringLiteral("Recent"));
  QCOMPARE(h.warnings.size(), 1);
  QVERIFY(!dialogRan);
  QVERIFY(h.playlists.updatedFilterIds.isEmpty());
}

void TestPlaylistMenuController::smartEditUpdatesFilterAndRenamesOnNameChange() {
  Harness h;
  h.m_setup.runSmartPlaylistDialog =
      [](const QString &, const std::optional<SmartFilter::FilterSet> &,
         const SmartPlaylistCollectionEntries &) -> std::optional<SmartPlaylistEdit> {
    SmartPlaylistEdit edit;
    edit.name = QStringLiteral("Renamed");
    SmartFilter::Filter rule;
    rule.kind = SmartFilter::Kind::TopPlayed;
    edit.filterSet.rules = {rule};
    return edit;
  };
  h.rewire();
  h.controller.editSmartPlaylistDialog(QStringLiteral("pl-smart"), QStringLiteral("Recent"));
  QCOMPARE(h.playlists.updatedFilterIds, QStringList{QStringLiteral("pl-smart")});
  QCOMPARE(h.playlists.lastSmartFilterSet.rules.size(), 1);
  QCOMPARE(h.playlists.lastSmartFilterSet.rules[0].kind, SmartFilter::Kind::TopPlayed);
  QCOMPARE(h.playlists.renames.size(), 1);
  QCOMPARE(h.playlists.renames[0].name, QStringLiteral("Renamed"));
}

void TestPlaylistMenuController::smartEditKeepsNameWhenUnchanged() {
  Harness h;
  h.m_setup.runSmartPlaylistDialog =
      [](const QString &currentName, const std::optional<SmartFilter::FilterSet> &,
         const SmartPlaylistCollectionEntries &) -> std::optional<SmartPlaylistEdit> {
    SmartPlaylistEdit edit;
    edit.name = currentName; // unchanged
    SmartFilter::Filter rule;
    rule.kind = SmartFilter::Kind::NeverPlayed;
    edit.filterSet.rules = {rule};
    return edit;
  };
  h.rewire();
  h.controller.editSmartPlaylistDialog(QStringLiteral("pl-smart"), QStringLiteral("Recent"));
  QCOMPARE(h.playlists.updatedFilterIds.size(), 1);
  QVERIFY(h.playlists.renames.isEmpty());
}

QTEST_GUILESS_MAIN(TestPlaylistMenuController)
#include "test_playlistmenucontroller.moc"
