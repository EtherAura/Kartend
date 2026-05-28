// Tests for ScreenScraperParser. Pure JSON → typed parsing — no
// network. Fixtures shaped to match the real jeuInfos.php response.
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>

#include "screenscraperparser.h"

class TestScreenScraperParser : public QObject {
  Q_OBJECT
private slots:
  void parseSearchResponse_returnsSingleCandidate();
  void parseSearchResponse_emptyJeuYieldsEmptyList();
  void parseSearchResponse_malformedJsonReturnsError();
  void parseDetailResponse_extractsTypedFields();
  void parseDetailResponse_picksUSRegionAndEnglishLanguage();
  void parseDetailResponse_collapsesMediaToOnePerType();
  void parseDetailResponse_renamesBox2DToFront();
  void parseDetailResponse_pullsEsrbContentRating();
  void parseDetailResponse_capturesAllClassifications();
  void parseDetailResponse_capturesGameLevelMetadata();
  void parseDetailResponse_capturesRomLevelMetadata();
  void parseDetailResponse_capturesControlsModesFamilies();
  void parseDetailResponse_mapsBox2DAndSstitleToStandardTypes();
  void parseDetailResponse_preservesNonStandardMediaTypes();
  void parseDetailResponse_recoversFromTrailingGarbage();
  void parseDetailResponse_titleAndDateFollowRomRegion();
  void parseDetailResponse_artworkFollowsRomRegion();
  void parseDetailResponse_fallbackRegionBackstopsUnknownRomRegion();
  void parseDetailResponse_filenameOverridePreemptsRomRegion();
  void parseDetailResponse_freeTextFollowsPreferredLanguage();
  void parseDetailResponse_collapsesSsToScreenshot();
};

namespace {

const QByteArray FIXTURE = R"json({
  "header": {"APIversion": "2"},
  "response": {
    "jeu": {
      "id": "1234",
      "cloneof": "5678",
      "notgame": "false",
      "noms": [
        {"region": "us", "text": "Game (US)"},
        {"region": "eu", "text": "Game (EU)"},
        {"region": "jp", "text": "Game (JP)"}
      ],
      "synopsis": [
        {"langue": "en", "text": "English description."},
        {"langue": "fr", "text": "French description."}
      ],
      "editeur": {"text": "Publisher Inc"},
      "developpeur": {"text": "Dev Studio"},
      "joueurs": {"text": "1-4"},
      "note": {"text": "18"},
      "topstaff": "1",
      "rotation": "0",
      "resolution": "320x240",
      "couleurs": "16",
      "dates": [
        {"region": "us", "text": "1991-09-25"},
        {"region": "jp", "text": "1991-08-13"}
      ],
      "genres": [
        {"noms": [{"langue": "en", "text": "Platform"}, {"langue": "fr", "text": "Plateformer"}]},
        {"noms": [{"langue": "en", "text": "Adventure"}]}
      ],
      "modes": [
        {"noms": [{"langue": "en", "text": "Single-player"}]},
        {"noms": [{"langue": "en", "text": "Co-op"}]}
      ],
      "familles": [
        {"noms": [{"langue": "en", "text": "TestFamily"}]}
      ],
      "controles": [{"text": "D-Pad"}, {"text": "Buttons"}],
      "classifications": [
        {"type": "PEGI", "text": "7"},
        {"type": "ESRB", "text": "E"},
        {"type": "USK", "text": "6"}
      ],
      "rom": {
        "romfilename": "game.smc",
        "romsize": "524288",
        "rommd5": "abc123",
        "romsha1": "def456",
        "romcrc": "12345678",
        "romtype": "rom",
        "romserial": "SNS-AB-USA",
        "regions": {"regions_id": ["46"], "regions_shortname": ["us"]},
        "langues": {"langues_id": ["1"], "langues_shortname": ["en"]}
      },
      "medias": [
        {"type": "box-2D", "region": "eu", "url": "https://example.com/box-eu.png"},
        {"type": "box-2D", "region": "us", "url": "https://example.com/box-us.png"},
        {"type": "screenshot", "region": "us", "url": "https://example.com/ss.png"},
        {"type": "sstitle", "region": "us", "url": "https://example.com/title.png"},
        {"type": "wheel", "region": "us", "url": "https://example.com/wheel.png"},
        {"type": "marquee", "region": "us", "url": "https://example.com/marquee.png"},
        {"type": "fanart", "region": "us", "url": "https://example.com/fanart.png"},
        {"type": "box-3D", "region": "us", "url": "https://example.com/box3d.png"}
      ]
    }
  }
})json";

const QByteArray EMPTY_FIXTURE = R"json({"header":{"APIversion":"2"},"response":{}})json";

// Fixture for region/language selection: the matched ROM is a Japanese
// cartridge, and SS offers US / EU / JP variants of every region-keyed
// field plus en / fr / de variants of the free-text fields.
const QByteArray REGION_FIXTURE = R"json({
  "header": {"APIversion": "2"},
  "response": {
    "jeu": {
      "id": "9000",
      "noms": [
        {"region": "us", "text": "Game (US)"},
        {"region": "eu", "text": "Game (EU)"},
        {"region": "jp", "text": "Game (JP)"}
      ],
      "synopsis": [
        {"langue": "en", "text": "English description."},
        {"langue": "fr", "text": "French description."},
        {"langue": "de", "text": "German description."}
      ],
      "dates": [
        {"region": "us", "text": "1991-09-25"},
        {"region": "jp", "text": "1991-08-13"},
        {"region": "eu", "text": "1992-01-10"}
      ],
      "genres": [
        {"noms": [{"langue": "en", "text": "Platform"}, {"langue": "fr", "text": "Plateformer"}]}
      ],
      "rom": {
        "romfilename": "Game (Japan).nes",
        "romregions": "jp"
      },
      "medias": [
        {"type": "box-2D", "region": "us", "url": "https://example.com/box-us.png"},
        {"type": "box-2D", "region": "eu", "url": "https://example.com/box-eu.png"},
        {"type": "box-2D", "region": "jp", "url": "https://example.com/box-jp.png"}
      ]
    }
  }
})json";

} // namespace

void TestScreenScraperParser::parseSearchResponse_returnsSingleCandidate() {
  auto result = ScreenScraperParser::parseSearchResponse(FIXTURE);
  QVERIFY(result.isOk());
  QCOMPARE(result.value().size(), 1);
  QCOMPARE(result.value().first().providerSpecificId, QStringLiteral("1234"));
  QCOMPARE(result.value().first().displayName, QStringLiteral("Game (US)"));
}

void TestScreenScraperParser::parseSearchResponse_emptyJeuYieldsEmptyList() {
  // No `jeu` field = no match, NOT a parse error. Match the shape of
  // the other parsers so the dialog shows "No matches".
  auto result = ScreenScraperParser::parseSearchResponse(EMPTY_FIXTURE);
  QVERIFY(result.isOk());
  QCOMPARE(result.value().size(), 0);
}

void TestScreenScraperParser::parseSearchResponse_malformedJsonReturnsError() {
  auto result = ScreenScraperParser::parseSearchResponse(QByteArray("{not json"));
  QVERIFY(result.isError());
}

void TestScreenScraperParser::parseDetailResponse_extractsTypedFields() {
  auto result = ScreenScraperParser::parseDetailResponse(FIXTURE);
  QVERIFY(result.isOk());
  const auto item = result.value();
  QCOMPARE(item.title, QStringLiteral("Game (US)"));
  QCOMPARE(item.publisher, QStringLiteral("Publisher Inc"));
  QCOMPARE(item.developer, QStringLiteral("Dev Studio"));
  QCOMPARE(item.players, QStringLiteral("1-4"));
  QCOMPARE(item.releaseDate, QStringLiteral("1991-09-25"));
  QCOMPARE(item.genre, QStringLiteral("Platform, Adventure"));
  QCOMPARE(item.sourceProviderId, QStringLiteral("screenscraper"));
  QCOMPARE(item.customFields.value("screenscraper_id"), QStringLiteral("1234"));
}

void TestScreenScraperParser::parseDetailResponse_picksUSRegionAndEnglishLanguage() {
  auto result = ScreenScraperParser::parseDetailResponse(FIXTURE);
  QVERIFY(result.isOk());
  // Title prefers US over EU/JP per the region preference list.
  QCOMPARE(result.value().title, QStringLiteral("Game (US)"));
  // Description prefers en over fr per the language preference list.
  QCOMPARE(result.value().description, QStringLiteral("English description."));
}

void TestScreenScraperParser::parseDetailResponse_collapsesMediaToOnePerType() {
  auto result = ScreenScraperParser::parseDetailResponse(FIXTURE);
  QVERIFY(result.isOk());
  const auto media = result.value().media;
  // 8 media entries in fixture; two box-2D variants collapse into
  // one (US wins) so we expect 7 unique types in the output.
  QCOMPARE(media.size(), 7);
  // US-region URL won the box-2D race.
  bool sawUsBox = false;
  for (const auto &m : media) {
    if (m.url.toString().contains("box-us")) sawUsBox = true;
  }
  QVERIFY(sawUsBox);
}

void TestScreenScraperParser::parseDetailResponse_renamesBox2DToFront() {
  auto result = ScreenScraperParser::parseDetailResponse(FIXTURE);
  QVERIFY(result.isOk());
  const auto media = result.value().media;
  // box-2D should land as type "front" so the persistence layer
  // mirrors it to the primary cover slot.
  bool foundFront = false;
  for (const auto &m : media) {
    if (m.type == QStringLiteral("front")) foundFront = true;
    QVERIFY(m.type != QStringLiteral("box-2D"));
  }
  QVERIFY(foundFront);
}

void TestScreenScraperParser::parseDetailResponse_pullsEsrbContentRating() {
  auto result = ScreenScraperParser::parseDetailResponse(FIXTURE);
  QVERIFY(result.isOk());
  // Two classifications in fixture (PEGI 7 + ESRB E). ESRB wins per
  // the parser's preference.
  QCOMPARE(result.value().contentRating, QStringLiteral("E"));
}

void TestScreenScraperParser::parseDetailResponse_capturesAllClassifications() {
  // ESRB still wins for the typed contentRating field, but every
  // rating board (PEGI, USK, ...) lands in customFields under
  // classification_<board> so the user can see the full picture.
  auto result = ScreenScraperParser::parseDetailResponse(FIXTURE);
  QVERIFY(result.isOk());
  const auto fields = result.value().customFields;
  QCOMPARE(fields.value("classification_esrb"), QStringLiteral("E"));
  QCOMPARE(fields.value("classification_pegi"), QStringLiteral("7"));
  QCOMPARE(fields.value("classification_usk"), QStringLiteral("6"));
}

void TestScreenScraperParser::parseDetailResponse_capturesGameLevelMetadata() {
  auto result = ScreenScraperParser::parseDetailResponse(FIXTURE);
  QVERIFY(result.isOk());
  const auto fields = result.value().customFields;
  QCOMPARE(fields.value("screenscraper_id"), QStringLiteral("1234"));
  QCOMPARE(fields.value("cloneof"), QStringLiteral("5678"));
  QCOMPARE(fields.value("notgame"), QStringLiteral("false"));
  QCOMPARE(fields.value("rating"), QStringLiteral("18"));
  QCOMPARE(fields.value("topstaff"), QStringLiteral("1"));
  QCOMPARE(fields.value("rotation"), QStringLiteral("0"));
  QCOMPARE(fields.value("resolution"), QStringLiteral("320x240"));
  QCOMPARE(fields.value("colors"), QStringLiteral("16"));
}

void TestScreenScraperParser::parseDetailResponse_capturesRomLevelMetadata() {
  auto result = ScreenScraperParser::parseDetailResponse(FIXTURE);
  QVERIFY(result.isOk());
  const auto fields = result.value().customFields;
  QCOMPARE(fields.value("rom_filename"), QStringLiteral("game.smc"));
  QCOMPARE(fields.value("rom_size"), QStringLiteral("524288"));
  QCOMPARE(fields.value("rom_md5"), QStringLiteral("abc123"));
  QCOMPARE(fields.value("rom_sha1"), QStringLiteral("def456"));
  QCOMPARE(fields.value("rom_crc"), QStringLiteral("12345678"));
  QCOMPARE(fields.value("rom_type"), QStringLiteral("rom"));
  QCOMPARE(fields.value("rom_serial"), QStringLiteral("SNS-AB-USA"));
  QCOMPARE(fields.value("region"), QStringLiteral("us"));
  QCOMPARE(fields.value("languages"), QStringLiteral("en"));
}

void TestScreenScraperParser::parseDetailResponse_capturesControlsModesFamilies() {
  auto result = ScreenScraperParser::parseDetailResponse(FIXTURE);
  QVERIFY(result.isOk());
  const auto fields = result.value().customFields;
  QCOMPARE(fields.value("controls"), QStringLiteral("D-Pad, Buttons"));
  QCOMPARE(fields.value("modes"), QStringLiteral("Single-player, Co-op"));
  QCOMPARE(fields.value("families"), QStringLiteral("TestFamily"));
}

void TestScreenScraperParser::parseDetailResponse_mapsBox2DAndSstitleToStandardTypes() {
  // SS exposes its own type vocabulary. Only types with a *unique*
  // canonical Kartend name get translated (box-2D → front, sstitle →
  // title); visual variants like wheel/wheel-hd/wheel-carbon stay
  // distinct so they get separate on-disk subdirs and never collide.
  auto result = ScreenScraperParser::parseDetailResponse(FIXTURE);
  QVERIFY(result.isOk());
  QStringList types;
  for (const auto &m : result.value().media) {
    types.append(m.type);
  }
  // Translated to Kartend canonical names.
  QVERIFY(types.contains(QStringLiteral("front"))); // box-2D → front
  QVERIFY(types.contains(QStringLiteral("title"))); // sstitle → title
  // Names that pass through unchanged — either Kartend's canonical
  // term already matches SS's (screenshot/marquee/fanart) or the
  // variant must stay distinct (wheel and siblings).
  QVERIFY(types.contains(QStringLiteral("screenshot")));
  QVERIFY(types.contains(QStringLiteral("marquee")));
  QVERIFY(types.contains(QStringLiteral("fanart")));
  QVERIFY(types.contains(QStringLiteral("wheel")));
  // The SS-specific names we *do* translate must not leak through.
  QVERIFY(!types.contains(QStringLiteral("sstitle")));
  QVERIFY(!types.contains(QStringLiteral("box-2D")));
}

void TestScreenScraperParser::parseDetailResponse_preservesNonStandardMediaTypes() {
  // Types Kartend doesn't translate (box-3D / box-back / support-2D /
  // etc.) keep their original SS name so the persistence layer's
  // item_artwork-row branch picks them up.
  auto result = ScreenScraperParser::parseDetailResponse(FIXTURE);
  QVERIFY(result.isOk());
  bool sawBox3d = false;
  for (const auto &m : result.value().media) {
    if (m.type == QStringLiteral("box-3D")) sawBox3d = true;
  }
  QVERIFY(sawBox3d);
}

void TestScreenScraperParser::parseDetailResponse_recoversFromTrailingGarbage() {
  // Regression: SS occasionally appends a PHP warning/notice/stack
  // trace after the closing `}` of an otherwise-valid response. Qt
  // reports `GarbageAtEnd`; we should retry against the prefix up to
  // the error offset and still extract the game data. Real-world
  // example reported on "Lost World, The - Jurassic Park - Special
  // Edition (USA)".
  QByteArray polluted = FIXTURE;
  polluted.append("\n<br />Notice: Undefined index ...\nat line 42\n");
  auto result = ScreenScraperParser::parseDetailResponse(polluted);
  QVERIFY(result.isOk());
  QCOMPARE(result.value().title, QStringLiteral("Game (US)"));
}

void TestScreenScraperParser::parseDetailResponse_titleAndDateFollowRomRegion() {
  // The matched ROM is Japanese, so the region-keyed title and release
  // date must come back as the JP variants — not collapsed to US.
  auto result = ScreenScraperParser::parseDetailResponse(REGION_FIXTURE);
  QVERIFY(result.isOk());
  QCOMPARE(result.value().title, QStringLiteral("Game (JP)"));
  QCOMPARE(result.value().releaseDate, QStringLiteral("1991-08-13"));
}

void TestScreenScraperParser::parseDetailResponse_artworkFollowsRomRegion() {
  // Box art is region-keyed too: a Japanese ROM keeps the JP box even
  // when US / EU variants are also offered.
  auto result = ScreenScraperParser::parseDetailResponse(REGION_FIXTURE);
  QVERIFY(result.isOk());
  QString frontUrl;
  for (const auto &m : result.value().media) {
    if (m.type == QStringLiteral("front")) frontUrl = m.url.toString();
  }
  QCOMPARE(frontUrl, QStringLiteral("https://example.com/box-jp.png"));
}

void TestScreenScraperParser::parseDetailResponse_fallbackRegionBackstopsUnknownRomRegion() {
  // When the matched ROM's region has no entry in the region-keyed
  // arrays, the configured fallback region decides — and with no
  // fallback set, the fixed English-leaning chain picks US.
  QByteArray unknownRegion = REGION_FIXTURE;
  unknownRegion.replace("\"romregions\": \"jp\"", "\"romregions\": \"kr\"");

  ScreenScraperParser::ParseOptions euOpts;
  euOpts.preferredRegion = QStringLiteral("eu");
  auto euResult = ScreenScraperParser::parseDetailResponse(unknownRegion, euOpts);
  QVERIFY(euResult.isOk());
  QCOMPARE(euResult.value().title, QStringLiteral("Game (EU)"));

  auto defaultResult = ScreenScraperParser::parseDetailResponse(unknownRegion);
  QVERIFY(defaultResult.isOk());
  QCOMPARE(defaultResult.value().title, QStringLiteral("Game (US)"));
}

void TestScreenScraperParser::parseDetailResponse_filenameOverridePreemptsRomRegion() {
  // Kartend-ou0a regression: when the caller supplies a filenameRegionOverride
  // (typically because a hash-failure forced filename-only matching at SS),
  // that override must win against the matched ROM's region — otherwise a
  // JP-named-file matched to SS's US jeu record collapses to US art.
  QByteArray usRomFixture = REGION_FIXTURE;
  usRomFixture.replace("\"romregions\": \"jp\"", "\"romregions\": \"us\"");

  // Baseline (no override): SS reports the matched ROM as US, so US art wins.
  auto baseline = ScreenScraperParser::parseDetailResponse(usRomFixture);
  QVERIFY(baseline.isOk());
  QCOMPARE(baseline.value().title, QStringLiteral("Game (US)"));

  // With filenameRegionOverride='jp' (e.g. parsed from "(Japan)" in the user's
  // ROM filename), JP must come back even though SS still says us.
  ScreenScraperParser::ParseOptions opts;
  opts.filenameRegionOverride = QStringLiteral("jp");
  auto overridden = ScreenScraperParser::parseDetailResponse(usRomFixture, opts);
  QVERIFY(overridden.isOk());
  QCOMPARE(overridden.value().title, QStringLiteral("Game (JP)"));
  QCOMPARE(overridden.value().releaseDate, QStringLiteral("1991-08-13"));
  QString frontUrl;
  for (const auto &m : overridden.value().media) {
    if (m.type == QStringLiteral("front")) frontUrl = m.url.toString();
  }
  QCOMPARE(frontUrl, QStringLiteral("https://example.com/box-jp.png"));
}

void TestScreenScraperParser::parseDetailResponse_freeTextFollowsPreferredLanguage() {
  // Free-text fields follow the application language, independent of
  // the ROM's region: a Japanese ROM with a French UI yields the
  // French description and genre while the title stays JP.
  ScreenScraperParser::ParseOptions frOpts;
  frOpts.preferredLanguage = QStringLiteral("fr");
  auto result = ScreenScraperParser::parseDetailResponse(REGION_FIXTURE, frOpts);
  QVERIFY(result.isOk());
  QCOMPARE(result.value().description, QStringLiteral("French description."));
  QCOMPARE(result.value().genre, QStringLiteral("Plateformer"));
  QCOMPARE(result.value().title, QStringLiteral("Game (JP)"));
}

void TestScreenScraperParser::parseDetailResponse_collapsesSsToScreenshot() {
  // SS serves the in-game screenshot under the short tag `ss`; it must
  // collapse to the canonical `screenshot` type so it lines up with
  // the `screenshot` media-type filter checkbox.
  const QByteArray json = R"json({
    "response": {"jeu": {
      "id": "1",
      "noms": [{"region": "us", "text": "Game"}],
      "rom": {"romregions": "us"},
      "medias": [{"type": "ss", "region": "us", "url": "https://example.com/ss.png"}]
    }}
  })json";
  auto result = ScreenScraperParser::parseDetailResponse(json);
  QVERIFY(result.isOk());
  QStringList types;
  for (const auto &m : result.value().media) {
    types.append(m.type);
  }
  QVERIFY(types.contains(QStringLiteral("screenshot")));
  QVERIFY(!types.contains(QStringLiteral("ss")));
}

QTEST_MAIN(TestScreenScraperParser)
#include "test_screenscraperparser.moc"
