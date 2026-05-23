// Tests for ThemePresetIO — JSON round-trip, file I/O via QTemporaryDir,
// schema-version handling, and the apply / describeChanges helpers.
#include <QFile>
#include <QJsonObject>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include "collection/collectionconfig.h"
#include "collection/themepreset.h"

class TestThemePreset : public QObject {
  Q_OBJECT
private slots:
  void roundTripsThroughJson();
  void rejectsMissingSchemaVersion();
  void rejectsNewerSchemaVersion();
  void exportAndImportRoundTripsViaFile();
  void applyToOverwritesAppearanceFields();
  void applyToLeavesNonAppearanceFieldsAlone();
  void applyToClampsOutOfRangeNumericFields();
  void describeChangesEmptyWhenIdentical();
  void describeChangesReportsModifiedClusters();
};

void TestThemePreset::roundTripsThroughJson() {
  ThemePreset in;
  in.name = "Midnight Concert";
  in.description = "Dark theme tuned for music collections.";
  in.background.backgroundColor = "#101015";
  in.background.vignetteEnabled = true;
  in.background.vignetteIntensity = 75;
  in.sidebar.sidebarTextColor = "#eeeeee";
  in.sidebar.sidebarAccentColor = "#7aa2f7";
  in.gridLayout.gridWidth = 8;
  in.gridLayout.itemWidth = 200;
  in.listView.listFontSize = 14;
  in.customFontFamily = "Iosevka";

  const auto json = ThemePresetIO::toJson(in);
  auto parsed = ThemePresetIO::fromJson(json);
  QVERIFY(parsed.isOk());
  const ThemePreset out = parsed.value();
  QCOMPARE(out.name, in.name);
  QCOMPARE(out.description, in.description);
  QCOMPARE(out.background.backgroundColor, in.background.backgroundColor);
  QVERIFY(out.background.vignetteEnabled);
  QCOMPARE(out.background.vignetteIntensity, 75);
  QCOMPARE(out.sidebar.sidebarTextColor, in.sidebar.sidebarTextColor);
  QCOMPARE(out.sidebar.sidebarAccentColor, in.sidebar.sidebarAccentColor);
  QCOMPARE(out.gridLayout.gridWidth, 8);
  QCOMPARE(out.gridLayout.itemWidth, 200);
  QCOMPARE(out.listView.listFontSize, 14);
  QCOMPARE(out.customFontFamily, QStringLiteral("Iosevka"));
}

void TestThemePreset::rejectsMissingSchemaVersion() {
  QJsonObject obj;
  obj["name"] = "Whatever";
  // No schemaVersion key — must error rather than silently apply
  // potentially-wrong defaults.
  auto r = ThemePresetIO::fromJson(obj);
  QVERIFY(r.isError());
}

void TestThemePreset::rejectsNewerSchemaVersion() {
  QJsonObject obj;
  obj["schemaVersion"] = 99; // Pretend a future build wrote this.
  obj["name"] = "Future Theme";
  auto r = ThemePresetIO::fromJson(obj);
  QVERIFY(r.isError());
  // The error message should hint at the version mismatch so users can
  // upgrade their build instead of guessing.
  QVERIFY(r.error().details.contains("schema"));
}

void TestThemePreset::exportAndImportRoundTripsViaFile() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  ThemePreset in;
  in.name = "Concert Hall";
  in.gridLayout.itemHeight = 180;
  in.sidebar.sidebarAccentColor = "#ffaa00";

  const QString path = dir.filePath("concert.kartend-theme.json");
  auto exported = ThemePresetIO::exportToFile(in, path);
  QVERIFY(exported.isOk());

  // File exists and isn't empty.
  QFile f(path);
  QVERIFY(f.open(QIODevice::ReadOnly));
  const QByteArray bytes = f.readAll();
  QVERIFY(!bytes.isEmpty());
  f.close();

  auto imported = ThemePresetIO::importFromFile(path);
  QVERIFY(imported.isOk());
  QCOMPARE(imported.value().name, in.name);
  QCOMPARE(imported.value().gridLayout.itemHeight, 180);
  QCOMPARE(imported.value().sidebar.sidebarAccentColor, QStringLiteral("#ffaa00"));
}

void TestThemePreset::applyToOverwritesAppearanceFields() {
  CollectionConfig target;
  target.name = "Live Recordings";
  target.gridLayout.gridWidth = 4;
  target.sidebar.sidebarTextColor = "#000000";

  ThemePreset preset;
  preset.gridLayout.gridWidth = 8;
  preset.sidebar.sidebarTextColor = "#cccccc";

  ThemePresetIO::applyTo(preset, target);
  QCOMPARE(target.gridLayout.gridWidth, 8);
  QCOMPARE(target.sidebar.sidebarTextColor, QStringLiteral("#cccccc"));
}

void TestThemePreset::applyToLeavesNonAppearanceFieldsAlone() {
  // The preset must NOT touch paths, launcher config, scraper overrides,
  // or any non-appearance scalars — that's what makes it safe to share a
  // theme across collections with different media.
  CollectionConfig target;
  target.name = "Live Recordings";
  target.mediaDirectory = "/home/user/Music/Live";
  target.launcher.launcherPath = "mpv";
  target.extensions = {"mp3", "flac"};
  target.scraperOverrides.scraperProviderId = "musicbrainz";

  ThemePreset preset;
  preset.gridLayout.gridWidth = 8;
  ThemePresetIO::applyTo(preset, target);

  QCOMPARE(target.name, QStringLiteral("Live Recordings"));
  QCOMPARE(target.mediaDirectory, QStringLiteral("/home/user/Music/Live"));
  QCOMPARE(target.launcher.launcherPath, QStringLiteral("mpv"));
  QCOMPARE(target.extensions, (QStringList{"mp3", "flac"}));
  QCOMPARE(target.scraperOverrides.scraperProviderId, QStringLiteral("musicbrainz"));
}

void TestThemePreset::applyToClampsOutOfRangeNumericFields() {
  // A hand-edited preset with a font size below the floor must NOT pin
  // the collection at an unreadable size — clampValues handles the safety
  // net (called inside applyTo).
  CollectionConfig target;
  target.name = "Archive";
  ThemePreset preset;
  preset.gridLayout.fontSize = -50;     // way below MIN_FONT_SIZE
  preset.gridLayout.cornerRadius = 999; // way above MAX_CORNER_RADIUS

  ThemePresetIO::applyTo(preset, target);
  QVERIFY(target.gridLayout.fontSize > 0);
  QVERIFY(target.gridLayout.cornerRadius < 999);
}

void TestThemePreset::describeChangesEmptyWhenIdentical() {
  CollectionConfig cfg;
  cfg.name = "Cinema";
  // Build a preset from cfg's current appearance — describeChanges should
  // report no differences.
  const ThemePreset preset = ThemePresetIO::fromCollection(cfg);
  const auto changes = ThemePresetIO::describeChanges(preset, cfg);
  QVERIFY(changes.isEmpty());
}

void TestThemePreset::describeChangesReportsModifiedClusters() {
  CollectionConfig cfg;
  cfg.name = "Cinema";
  ThemePreset preset = ThemePresetIO::fromCollection(cfg);
  preset.gridLayout.gridWidth += 1;           // touch grid cluster
  preset.background.backgroundColor = "#222"; // touch background cluster
  const auto changes = ThemePresetIO::describeChanges(preset, cfg);
  QCOMPARE(changes.size(), 2);
}

QTEST_MAIN(TestThemePreset)
#include "test_themepreset.moc"
