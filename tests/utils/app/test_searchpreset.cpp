/**
 * @file test_searchpreset.cpp
 * @brief Unit tests for saved search presets (Kartend-jklv4) — JSON round
 * trip, forward-compatibility refusals, apply/snapshot against ViewSettings,
 * and the name-keyed registry helpers.
 */

#include "collection/searchpreset.h"
#include "collection/view_settings.h"

#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

class TestSearchPreset : public QObject {
  Q_OBJECT

private slots:
  void json_roundTripsEveryField();
  void fromJson_rejectsUnnamedPreset();
  void fromJson_rejectsNewerSchemaVersion();
  void fromJson_rejectsUnknownSortMode();

  void applyTo_writesFilterFieldsAndLeavesTheRestAlone();
  void fromViewSettings_snapshotsAndDefaultsTheName();

  void registry_roundTripsThroughAFile();
  void registry_missingFileIsEmptyNotAnError();
  void registry_skipsOneBadEntryButKeepsTheRest();
  void addOrReplace_replacesByNameCaseInsensitivelyInPlace();
  void removeByName_isCaseInsensitive();
};

void TestSearchPreset::json_roundTripsEveryField() {
  SearchPreset preset;
  preset.name = QStringLiteral("Unwatched talks");
  preset.description = QStringLiteral("things I keep meaning to get to");
  preset.searchText = QStringLiteral("played:false tag:talk keynote");
  preset.sortMode = SortMode::DateDescending;
  preset.excludeSubfoldersFromSort = true;
  preset.collectionTypeFilter = QStringLiteral("Video");
  preset.hideSubcollectionTiles = true;

  const auto parsed = SearchPresetIO::fromJson(SearchPresetIO::toJson(preset));
  QVERIFY(parsed.isOk());
  // Defaulted operator== keeps this field-complete: a new field that is not
  // serialised fails here rather than going missing silently.
  QCOMPARE(parsed.value(), preset);
}

void TestSearchPreset::fromJson_rejectsUnnamedPreset() {
  // The registry is name-keyed, so an unnamed preset could never be picked,
  // replaced or removed. Refuse it at the door instead of storing a row the
  // UI cannot address.
  QJsonObject obj = SearchPresetIO::toJson(SearchPreset{});
  obj["name"] = QStringLiteral("   ");
  QVERIFY(SearchPresetIO::fromJson(obj).isError());
}

void TestSearchPreset::fromJson_rejectsNewerSchemaVersion() {
  SearchPreset preset;
  preset.name = QStringLiteral("From the future");
  QJsonObject obj = SearchPresetIO::toJson(preset);
  obj["schemaVersion"] = 99;
  // Applying a half-understood preset would silently give the user a
  // different view from the one they saved.
  QVERIFY(SearchPresetIO::fromJson(obj).isError());
}

void TestSearchPreset::fromJson_rejectsUnknownSortMode() {
  SearchPreset preset;
  preset.name = QStringLiteral("Odd sort");
  QJsonObject obj = SearchPresetIO::toJson(preset);
  obj["sortMode"] = 999;
  // Clamping to A-Z would leave a preset that quietly sorts wrongly.
  QVERIFY(SearchPresetIO::fromJson(obj).isError());

  obj["sortMode"] = -1;
  QVERIFY(SearchPresetIO::fromJson(obj).isError());
}

void TestSearchPreset::applyTo_writesFilterFieldsAndLeavesTheRestAlone() {
  SearchPreset preset;
  preset.name = QStringLiteral("Wide");
  preset.sortMode = SortMode::SizeDescending;
  preset.excludeSubfoldersFromSort = true;
  preset.collectionTypeFilter = QStringLiteral("Audio");
  preset.hideSubcollectionTiles = true;

  ViewSettings view;
  view.listCollectionColumnWidth = 321;
  view.showMenuBar = false;
  view.fullscreen = true;
  view.showTitleInPlaceholder = true;

  SearchPresetIO::applyTo(preset, view);

  QCOMPARE(view.sortMode, SortMode::SizeDescending);
  QVERIFY(view.excludeSubfoldersFromSort);
  QCOMPARE(view.collectionTypeFilter, QStringLiteral("Audio"));
  QVERIFY(view.hideSubcollectionTiles);
  // Applying a saved query must not rearrange the window: chrome toggles,
  // column widths and the placeholder-title overlay are outside the filter
  // surface and stay exactly as the user left them.
  QCOMPARE(view.listCollectionColumnWidth, 321);
  QCOMPARE(view.showMenuBar, false);
  QCOMPARE(view.fullscreen, true);
  QCOMPARE(view.showTitleInPlaceholder, true);
}

void TestSearchPreset::fromViewSettings_snapshotsAndDefaultsTheName() {
  ViewSettings view;
  view.sortMode = SortMode::Random;
  view.collectionTypeFilter = QStringLiteral("Reference");
  view.hideSubcollectionTiles = true;

  const auto named = SearchPresetIO::fromViewSettings(view, QStringLiteral("favorite:true"),
                                                      QStringLiteral("Dip"));
  QCOMPARE(named.name, QStringLiteral("Dip"));
  QCOMPARE(named.searchText, QStringLiteral("favorite:true"));
  QCOMPARE(named.sortMode, SortMode::Random);
  QCOMPARE(named.collectionTypeFilter, QStringLiteral("Reference"));
  QVERIFY(named.hideSubcollectionTiles);

  // An empty name would be rejected by fromJson, so the snapshot supplies a
  // usable default rather than handing back something unsavable.
  QCOMPARE(SearchPresetIO::fromViewSettings(view, QString(), QString()).name,
           QStringLiteral("Current"));
}

void TestSearchPreset::registry_roundTripsThroughAFile() {
  QTemporaryDir dir;
  const QString path = dir.filePath(QStringLiteral("search_presets.json"));

  SearchPreset first;
  first.name = QStringLiteral("Backlog");
  first.searchText = QStringLiteral("played:false");
  SearchPreset second;
  second.name = QStringLiteral("Favourites");
  second.searchText = QStringLiteral("favorite:true");
  second.sortMode = SortMode::DateAscending;

  QVERIFY(SearchPresetIO::saveRegistry({first, second}, path).isOk());
  const auto loaded = SearchPresetIO::loadRegistry(path);
  QVERIFY(loaded.isOk());
  QCOMPARE(loaded.value().size(), 2);
  QCOMPARE(loaded.value().at(0), first);
  QCOMPARE(loaded.value().at(1), second);
}

void TestSearchPreset::registry_missingFileIsEmptyNotAnError() {
  QTemporaryDir dir;
  // First run has no registry. That is the normal state, not a failure the
  // caller should have to special-case.
  const auto loaded = SearchPresetIO::loadRegistry(dir.filePath(QStringLiteral("absent.json")));
  QVERIFY(loaded.isOk());
  QVERIFY(loaded.value().isEmpty());
}

void TestSearchPreset::registry_skipsOneBadEntryButKeepsTheRest() {
  QTemporaryDir dir;
  const QString path = dir.filePath(QStringLiteral("mixed.json"));
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly));
  // Middle entry has no name and cannot be addressed by the picker. Presets
  // are independent of one another, so losing the whole list over a single
  // hand-edited row would be the harsher outcome — skip just that one.
  file.write(R"([
    {"name":"Good one","searchText":"played:false","sortMode":0},
    {"name":"","searchText":"broken","sortMode":0},
    {"name":"Another","searchText":"favorite:true","sortMode":0}
  ])");
  file.close();

  const auto loaded = SearchPresetIO::loadRegistry(path);
  QVERIFY(loaded.isOk());
  QCOMPARE(loaded.value().size(), 2);
  QCOMPARE(loaded.value().at(0).name, QStringLiteral("Good one"));
  QCOMPARE(loaded.value().at(1).name, QStringLiteral("Another"));
}

void TestSearchPreset::addOrReplace_replacesByNameCaseInsensitivelyInPlace() {
  SearchPreset original;
  original.name = QStringLiteral("Backlog");
  original.searchText = QStringLiteral("played:false");
  SearchPreset other;
  other.name = QStringLiteral("Favourites");

  SearchPreset updated;
  updated.name = QStringLiteral("BACKLOG"); // same preset, different casing
  updated.searchText = QStringLiteral("played:false tag:short");

  const auto result = SearchPresetIO::addOrReplace({original, other}, updated);
  // Replaced in place, not appended: two entries the picker cannot tell apart
  // would be worse than either outcome, and the ordering the user built stays.
  QCOMPARE(result.size(), 2);
  QCOMPARE(result.at(0).searchText, QStringLiteral("played:false tag:short"));
  QCOMPARE(result.at(1).name, QStringLiteral("Favourites"));

  SearchPreset fresh;
  fresh.name = QStringLiteral("New");
  QCOMPARE(SearchPresetIO::addOrReplace({original}, fresh).size(), 2);
}

void TestSearchPreset::removeByName_isCaseInsensitive() {
  SearchPreset a;
  a.name = QStringLiteral("Backlog");
  SearchPreset b;
  b.name = QStringLiteral("Favourites");

  const auto result = SearchPresetIO::removeByName({a, b}, QStringLiteral("bAcKlOg"));
  QCOMPARE(result.size(), 1);
  QCOMPARE(result.first().name, QStringLiteral("Favourites"));

  // Removing something absent leaves the registry untouched.
  QCOMPARE(SearchPresetIO::removeByName({a, b}, QStringLiteral("nope")).size(), 2);
}

QTEST_APPLESS_MAIN(TestSearchPreset)
#include "test_searchpreset.moc"
