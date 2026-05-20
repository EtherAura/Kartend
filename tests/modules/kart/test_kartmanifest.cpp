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
  void testItemMetadataRoundTrip();
  void testParseRejectsInvalidJson();
  void testParseRejectsNonObjectRoot();
  void testParseRejectsMissingFormatVersion();
  void testParseRejectsMissingUuid();
  void testParseRejectsMissingName();
  void testParseRejectsNewerFormatVersion();
};

void TestKartManifest::testFormatMagicConstants() {
  QCOMPARE(KartFormat::MAGIC_SIZE, 8);
  QCOMPARE(KartFormat::CURRENT_VERSION, 1u);
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
  c.launcher.defaultLauncherIndex = 2;
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

QTEST_MAIN(TestKartManifest)
#include "test_kartmanifest.moc"
