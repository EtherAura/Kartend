#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include "kartformat.h"
#include "kartmanifest.h"

class TestKartManifest : public QObject {
  Q_OBJECT

private slots:
  void testFormatMagicConstants();
  void testEmptyManifestRoundTrip();
  void testFullManifestRoundTrip();
  void testCollectionConfigAllFieldsRoundTrip();
  void testImportClampsOutOfRangeLayout();
  void testItemMetadataRoundTrip();
  void testParseRejectsInvalidJson();
  void testParseRejectsNonObjectRoot();
  void testParseRejectsMissingFormatVersion();
  void testParseRejectsMissingUuid();
  void testParseRejectsMissingName();
  void testParseRejectsNewerFormatVersion();
  void testParseRejectsOversizedLaunchersArray();
  void testParseRejectsOversizedItemsArray();
  void testParseRejectsOversizedPlaylistsArray();
  void testParseRejectsOversizedPlaylistItemsArray();
  void testParseAcceptsLaunchersAtCap();
  void testPlaylistRoundTrip();
  void testV1ManifestParsesWithEmptyPlaylists();
};

namespace {
// Minimal valid manifest JSON whose `field` array holds `count` copies of
// `element` — built textually so an over-cap array costs kilobytes of JSON
// instead of serializing hundreds of thousands of populated structs.
QByteArray manifestWithArray(const char *field, qsizetype count, const char *element) {
  QByteArray arr = (QByteArray(element) + ',').repeated(count);
  arr.chop(1); // trailing comma
  return QByteArray("{\"format_version\":1,\"uuid\":\"u\",\"name\":\"n\",\"") + field + "\":[" +
         arr + "]}";
}
} // namespace

void TestKartManifest::testFormatMagicConstants() {
  QCOMPARE(KartFormat::MAGIC_SIZE, 8);
  QCOMPARE(KartFormat::CURRENT_VERSION, 2u);
  QCOMPARE(KartFormat::SHA256_SIZE, 32);
  const char good[] = {'K', 'A', 'R', 'T', 0, 0, 0, 1};
  QVERIFY(KartFormat::hasMagic(good, 8));
  const char bad[] = {'F', 'A', 'I', 'L', 0, 0, 0, 1};
  QVERIFY(!KartFormat::hasMagic(bad, 8));
  QVERIFY(!KartFormat::hasMagic(good, 7));
  QVERIFY(!KartFormat::hasMagic(nullptr, 8));
}

void TestKartManifest::testEmptyManifestRoundTrip() {
  KartManifest::Manifest m;
  m.uuid = "11111111-2222-3333-4444-555555555555";
  m.name = "Empty";
  m.version = "1.0.0";
  m.createdAt = "2026-05-05T00:00:00Z";

  const QByteArray bytes = KartManifest::serialize(m);
  QVERIFY(!bytes.isEmpty());

  auto result = KartManifest::parse(bytes);
  QVERIFY2(result.isOk(), qPrintable(result.error().message));
  const auto &parsed = result.value();
  QCOMPARE(parsed.uuid, m.uuid);
  QCOMPARE(parsed.name, m.name);
  QCOMPARE(parsed.version, m.version);
  QCOMPARE(parsed.formatVersion, KartFormat::CURRENT_VERSION);
  QVERIFY(parsed.items.isEmpty());
  QVERIFY(parsed.launchers.isEmpty());
}

void TestKartManifest::testFullManifestRoundTrip() {
  KartManifest::Manifest m;
  m.uuid = "abc";
  m.version = "2.1.0";
  m.createdAt = "2026-05-05T12:34:56Z";
  m.name = "Test Cart";
  m.author = "tester";
  m.description = "A test kart";
  m.license = "CC0";

  m.collectionConfig.name = "Genesis";
  m.collectionConfig.type = "Games";
  m.collectionConfig.mediaDirectory = "media";
  m.collectionConfig.artworkDirectory = "artwork";
  m.collectionConfig.gridLayout.gridWidth = 6;
  m.collectionConfig.viewType = ViewType::CoverFlow;
  m.collectionConfig.extensions = {"bin", "smd", "md"};

  LauncherPreset preset;
  preset.id = "preset-1";
  preset.name = "RetroArch";
  preset.launcherPath = "/usr/bin/retroarch";
  preset.corePath = "/cores/genesis_plus_gx_libretro.so";
  preset.launchParameters = "-L %CORE% %ROM%";
  m.launchers.append(preset);

  KartManifest::Item item;
  item.mediaPath = "media/Sonic.bin";
  item.artworkPath = "artwork/Sonic.png";
  item.videoPath = "video/Sonic.mp4";
  item.title = "Sonic the Hedgehog";
  item.metadata.title = "Sonic the Hedgehog";
  item.metadata.description = "Side-scrolling platformer";
  item.metadata.genre = "Platformer";
  item.metadata.developer = "Sonic Team";
  item.metadata.releaseDate = "1991-06-23";
  item.metadata.runtimeSeconds = 7200;
  item.metadata.tags = "[\"classic\"]";
  item.metadata.launcherIndex = 1;
  item.launcherIndex = 1;
  m.items.append(item);

  const QByteArray bytes = KartManifest::serialize(m);
  auto result = KartManifest::parse(bytes);
  QVERIFY2(result.isOk(), qPrintable(result.error().message));

  const auto &parsed = result.value();
  QCOMPARE(parsed, m);
}

void TestKartManifest::testCollectionConfigAllFieldsRoundTrip() {
  KartManifest::Manifest m;
  m.uuid = "u";
  m.name = "n";

  CollectionConfig &c = m.collectionConfig;
  c.name = "C";
  c.type = "T";
  c.launcher.launcherPath = "/lp";
  c.launcher.corePath = "/cp";
  c.launcher.launchParameters = "-p";
  c.launcher.launcherName = "Primary";
  // Two launchers exist (the primary above + the "Secondary" additional
  // launcher appended below), so index 1 is in range and round-trips. An
  // out-of-range value is intentionally clamped at the import boundary now
  // (Kartend-aep2e), so this must stay within [0, launcherCount()-1].
  c.launcher.defaultLauncherIndex = 1;
  c.mediaDirectory = "md";
  c.artworkDirectory = "ad";
  c.videoDirectory = "vd";
  c.manualDirectory = "mand";
  c.placeholderArtwork = "ph.png";
  c.collectionIcon = "icon.png";
  c.extensions = {"a", "b"};
  c.customArtworkTypes = {"flyer", "marquee"};
  c.gridLayout.gridWidth = 7;
  c.gridLayout.horizontalGridHeight = 4;
  c.sidebar.sidebarVisible = true;
  c.parentCollectionIndex = 3;
  c.isSubcollection = true;
  c.showAllSubcollectionItems = true;
  c.hideTitles = true;
  c.hideSubcollectionTitles = true;
  c.filter.titleExclusionPatterns = {"\\(USA\\)"};
  c.filter.titleExclusionEnabled = false;
  c.horizontalAlignment = HorizontalAlignment::Right;
  c.sidebar.sidebarMode = DetailsPaneMode::Expand;
  c.sidebar.sidebarPosition = DetailsPanePosition::Left;
  c.sidebar.sidebarBackgroundType = DetailsPaneBackgroundType::Pattern;
  c.sidebar.sidebarBackgroundColor = "#112233";
  c.sidebar.sidebarBackgroundImage = "bg.png";
  c.sidebar.sidebarPattern = DetailsPanePattern::Crosshatch;
  c.sidebar.sidebarPatternIntensity = 75;
  c.sidebar.sidebarPatternColor = "#ffffff";
  c.sidebar.sidebarTextColor = "#000000";
  c.sidebar.sidebarAccentColor = "#ff00ff";
  c.sidebar.sidebarHeaderBgColor = "#aabbcc";
  c.sidebar.sidebarSectionBgColor = "#ccbbaa";
  c.sidebar.sidebarHeaderBgOpacity = 100;
  c.sidebar.sidebarSectionBgOpacity = 80;
  c.sidebar.sidebarWidth = 320;
  c.sidebar.sidebarWidthLocked = false;
  c.sidebar.sidebarActiveTab = DetailsPaneTab::Collection;
  c.sidebar.sidebarFontFamily = "Inter";
  c.sidebar.sidebarFontPointSize = 12;
  c.viewType = ViewType::Horizontal;
  c.hideMissingArtwork = true;
  c.gridLayout.horizontalSpacing = 25;
  c.gridLayout.verticalSpacing = 30;
  c.gridLayout.hideHorizontalScrollbar = true;
  c.gridLayout.hideVerticalScrollbar = true;
  c.gridLayout.itemWidth = 200;
  c.gridLayout.itemHeight = 250;
  c.gridLayout.fontSize = 14;
  c.gridLayout.cornerRadius = 8;
  c.background.backgroundType = BackgroundType::Video;
  c.background.backgroundColor = "#1a1a2e";
  c.background.backgroundImage = "bg2.png";
  c.background.backgroundVideo = "bg.mp4";
  c.background.primaryColor = "#abcdef";
  c.background.tileColor = "#fedcba";
  c.background.selectionColor = "#00ff00";
  c.background.headerLogoImage = "logo.png";
  c.background.headerLogoPosition = HeaderLogoPosition::TopRight;
  c.background.vignetteEnabled = true;
  c.background.vignetteIntensity = 80;
  c.background.wallpaperParallax = true;
  c.background.parallaxStrength = 50;
  c.background.toolbarBackdropBlur = true;
  c.background.backdropBlurRadius = 20;
  c.archive.extractArchives = true;
  c.archive.extractedExtension = "iso";
  c.expandMode = true;
  c.importSource = "steam";
  c.folderBrowsing.includeContentSubfolders = true;
  c.folderBrowsing.includeArtworkSubfolders = true;
  c.folderBrowsing.showAllSubfolderItems = true;
  c.folderBrowsing.hideSubfolderTitles = true;
  c.folderBrowsing.showHiddenFolders = true;
  c.listView.listFontSize = 11;
  c.listView.listRowHeight = 28;
  c.listView.listRowColor = "#222222";
  c.listView.listAltRowColor = "#333333";
  c.customFontFamily = "Roboto";
  c.additionalParentNames = {"P1", "P2"};

  LauncherConfig lc;
  lc.name = "Secondary";
  lc.launcherPath = "/lp2";
  lc.corePath = "/cp2";
  lc.launchParameters = "-q";
  lc.presetId = "preset-x";
  c.launcher.additionalLaunchers.append(lc);

  const QByteArray bytes = KartManifest::serialize(m);
  auto result = KartManifest::parse(bytes);
  QVERIFY2(result.isOk(), qPrintable(result.error().message));
  QCOMPARE(result.value().collectionConfig, c);
}

void TestKartManifest::testImportClampsOutOfRangeLayout() {
  // An untrusted / corrupt .kart can carry absurd or negative numeric layout
  // fields. The importer must clamp them at the boundary (Kartend audit E-03)
  // to the same UI-enforced ranges the settings loader applies, so they can't
  // drive a degenerate layout or a huge allocation downstream.
  KartManifest::Manifest m;
  m.uuid = "u";
  m.name = "n";
  CollectionConfig &c = m.collectionConfig;
  c.name = "C";
  c.gridLayout.itemWidth = 9'000'000;
  c.gridLayout.itemHeight = -50;
  c.gridLayout.fontSize = 100'000;
  c.gridLayout.gridWidth = 0;
  c.background.vignetteIntensity = 999;
  c.background.parallaxStrength = -10;
  c.background.backdropBlurRadius = 9999;
  c.sidebar.sidebarWidth = -1;

  const QByteArray bytes = KartManifest::serialize(m);
  const auto result = KartManifest::parse(bytes);
  QVERIFY2(result.isOk(), qPrintable(result.error().message));
  const CollectionConfig &got = result.value().collectionConfig;

  // Parsed values match the canonical clamp the settings path applies.
  CollectionConfig expected = m.collectionConfig;
  expected.clampValues();
  QCOMPARE(got.gridLayout.itemWidth, expected.gridLayout.itemWidth);
  QCOMPARE(got.gridLayout.itemHeight, expected.gridLayout.itemHeight);
  QCOMPARE(got.gridLayout.fontSize, expected.gridLayout.fontSize);
  QCOMPARE(got.gridLayout.gridWidth, expected.gridLayout.gridWidth);
  QCOMPARE(got.sidebar.sidebarWidth, expected.sidebar.sidebarWidth);

  // The absurd inputs were actually narrowed (documented clamp bounds).
  QVERIFY(got.gridLayout.itemWidth < 9'000'000);
  QVERIFY(got.gridLayout.itemHeight > 0);
  QCOMPARE(got.background.vignetteIntensity, 100);
  QCOMPARE(got.background.parallaxStrength, 0);
  QCOMPARE(got.background.backdropBlurRadius, 32);
}

void TestKartManifest::testItemMetadataRoundTrip() {
  KartManifest::Manifest m;
  m.uuid = "u";
  m.name = "n";

  KartManifest::Item it;
  it.mediaPath = "media/x.bin";
  it.title = "X";
  it.metadata.title = "Long X";
  it.metadata.description = "desc";
  it.metadata.genre = "g";
  it.metadata.developer = "dev";
  it.metadata.publisher = "pub";
  it.metadata.releaseDate = "2026";
  it.metadata.contentRating = "E";
  it.metadata.players = "1-2";
  it.metadata.runtimeSeconds = 120;
  it.metadata.tags = "[\"a\"]";
  it.metadata.customFields = "{\"k\":\"v\"}";
  it.metadata.manualPath = "manual/x.pdf";
  it.metadata.launcherIndex = 3;
  it.metadata.source = "user";
  it.launcherIndex = 3;
  m.items.append(it);

  const QByteArray bytes = KartManifest::serialize(m);
  auto result = KartManifest::parse(bytes);
  QVERIFY2(result.isOk(), qPrintable(result.error().message));
  QCOMPARE(result.value().items.size(), 1);
  QCOMPARE(result.value().items.first(), it);
}

void TestKartManifest::testParseRejectsInvalidJson() {
  auto result = KartManifest::parse("{not-valid-json");
  QVERIFY(result.isError());
  QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::KartManifestParseFailed));
}

void TestKartManifest::testParseRejectsNonObjectRoot() {
  auto result = KartManifest::parse("[1,2,3]");
  QVERIFY(result.isError());
  QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::KartManifestInvalid));
}

void TestKartManifest::testParseRejectsMissingFormatVersion() {
  auto result = KartManifest::parse("{\"uuid\":\"u\",\"name\":\"n\"}");
  QVERIFY(result.isError());
  QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::KartManifestInvalid));
}

void TestKartManifest::testParseRejectsMissingUuid() {
  auto result = KartManifest::parse("{\"format_version\":1,\"name\":\"n\"}");
  QVERIFY(result.isError());
  QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::KartManifestInvalid));
}

void TestKartManifest::testParseRejectsMissingName() {
  auto result = KartManifest::parse("{\"format_version\":1,\"uuid\":\"u\"}");
  QVERIFY(result.isError());
  QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::KartManifestInvalid));
}

void TestKartManifest::testParseRejectsNewerFormatVersion() {
  auto result = KartManifest::parse("{\"format_version\":99,\"uuid\":\"u\",\"name\":\"n\"}");
  QVERIFY(result.isError());
  QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::KartVersionUnsupported));
}

void TestKartManifest::testParseRejectsOversizedLaunchersArray() {
  // The 64 MiB manifest-size cap alone still admits arrays whose parsed form
  // expands far beyond the JSON text; parse must reject — not truncate — any
  // array past its ceiling, matching the extraction loop's contract.
  auto result = KartManifest::parse(
      manifestWithArray("launchers", KartFormat::MAX_MANIFEST_LAUNCHERS + 1, "{}"));
  QVERIFY(result.isError());
  QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::KartManifestInvalid));
}

void TestKartManifest::testParseRejectsOversizedItemsArray() {
  auto result =
      KartManifest::parse(manifestWithArray("items", KartFormat::MAX_MANIFEST_ITEMS + 1, "{}"));
  QVERIFY(result.isError());
  QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::KartManifestInvalid));
}

void TestKartManifest::testParseRejectsOversizedPlaylistsArray() {
  auto result = KartManifest::parse(
      manifestWithArray("playlists", KartFormat::MAX_MANIFEST_PLAYLISTS + 1, "{}"));
  QVERIFY(result.isError());
  QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::KartManifestInvalid));
}

void TestKartManifest::testParseRejectsOversizedPlaylistItemsArray() {
  // One playlist whose own items array crosses the ceiling — the per-playlist
  // list gets the same bound as the top-level items array.
  QByteArray items = QByteArray("{},").repeated(KartFormat::MAX_MANIFEST_ITEMS + 1);
  items.chop(1);
  const QByteArray json =
      QByteArray(
          "{\"format_version\":2,\"uuid\":\"u\",\"name\":\"n\",\"playlists\":[{\"items\":[") +
      items + "]}]}";
  auto result = KartManifest::parse(json);
  QVERIFY(result.isError());
  QVERIFY(result.hasErrorCode(ErrorUtils::ErrorCode::KartManifestInvalid));
}

void TestKartManifest::testParseAcceptsLaunchersAtCap() {
  // The ceiling is exclusive-over, not at: exactly MAX_MANIFEST_LAUNCHERS
  // entries must still parse.
  auto result =
      KartManifest::parse(manifestWithArray("launchers", KartFormat::MAX_MANIFEST_LAUNCHERS, "{}"));
  QVERIFY2(result.isOk(), qPrintable(result.error().message));
  QCOMPARE(result.value().launchers.size(), KartFormat::MAX_MANIFEST_LAUNCHERS);
}

void TestKartManifest::testPlaylistRoundTrip() {
  KartManifest::Manifest m;
  m.uuid = "11111111-2222-3333-4444-555555555555";
  m.name = "Demos";

  KartManifest::PlaylistEntry staticPl;
  staticPl.name = "Favorites";
  staticPl.icon = "star";
  staticPl.reservedKind = "favorites";
  staticPl.isSmart = false;
  KartManifest::PlaylistItemEntry it;
  it.mediaPath = "media/x.mkv";
  it.position = 0;
  staticPl.items.append(it);
  m.playlists.append(staticPl);

  KartManifest::PlaylistEntry smartPl;
  smartPl.name = "Recently played";
  smartPl.isSmart = true;
  smartPl.smartFilterJson = "{\"kind\":\"played\",\"limit\":50}";
  m.playlists.append(smartPl);

  const QByteArray json = KartManifest::serialize(m);
  auto parsed = KartManifest::parse(json);
  QVERIFY(parsed.isOk());
  QCOMPARE(parsed.value().playlists.size(), 2);
  QCOMPARE(parsed.value().playlists.at(0).name, QStringLiteral("Favorites"));
  QCOMPARE(parsed.value().playlists.at(0).items.size(), 1);
  QCOMPARE(parsed.value().playlists.at(0).items.first().mediaPath, QStringLiteral("media/x.mkv"));
  QVERIFY(parsed.value().playlists.at(1).isSmart);
  QCOMPARE(parsed.value().playlists.at(1).smartFilterJson,
           QStringLiteral("{\"kind\":\"played\",\"limit\":50}"));
}

void TestKartManifest::testV1ManifestParsesWithEmptyPlaylists() {
  // A v1 bundle that pre-dated playlist bundling omits the field
  // entirely. Parse must accept the manifest (v1 is <= CURRENT_VERSION)
  // and emit an empty playlists list rather than failing.
  const QByteArray v1Json = R"({"format_version":1,"uuid":"u","name":"n"})";
  auto result = KartManifest::parse(v1Json);
  QVERIFY(result.isOk());
  QVERIFY(result.value().playlists.isEmpty());
}

QTEST_MAIN(TestKartManifest)
#include "test_kartmanifest.moc"
