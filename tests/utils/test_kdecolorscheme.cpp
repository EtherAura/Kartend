// Tests for KdeColorScheme parsing + applyToCollection mapping. The
// loader is filesystem-friendly (QFile), so the tests write fixtures
// into QTemporaryDir and round-trip them. The bundled-discovery path
// reads from `:/themes/` resources — covered by a single discovery
// smoke test that verifies the three Kartend-bundled schemes show up
// (see test_resourceBundle).
#include <QDir>
#include <QFile>
#include <QObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include "collectionutils.h"
#include "kdecolorscheme.h"

class TestKdeColorScheme : public QObject {
  Q_OBJECT
private slots:
  void load_parsesNameAndColors();
  void load_emptyFileReturnsError();
  void load_missingFileReturnsError();
  void load_skipsMalformedRgbTriples();
  void applyToCollection_mapsAllStandardSlots();
  void applyToCollection_leavesUnsetSlotsUntouched();
  void applyToGeneralSettings_setsTitleBaseColorFromSelectionBackground();
  void discover_includesBundledSchemes();
};

namespace {

QString writeScheme(const QTemporaryDir &dir, const QString &name, const QString &body) {
  const QString path = dir.filePath(name);
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
    return {};
  }
  f.write(body.toUtf8());
  f.close();
  return path;
}

constexpr const char *MINIMAL_SCHEME = R"([General]
Name=Test Scheme

[Colors:View]
BackgroundNormal=10,20,30
BackgroundAlternate=40,50,60
ForegroundNormal=200,210,220

[Colors:Window]
BackgroundNormal=15,25,35
BackgroundAlternate=45,55,65
ForegroundNormal=205,215,225

[Colors:Selection]
BackgroundNormal=70,140,210
)";

} // namespace

void TestKdeColorScheme::load_parsesNameAndColors() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString path = writeScheme(tmp, "Test.colors", MINIMAL_SCHEME);
  QVERIFY(!path.isEmpty());

  auto result = KdeColorScheme::load(path);
  QVERIFY(result.isOk());
  const auto scheme = result.value();
  QCOMPARE(scheme.displayName, QStringLiteral("Test Scheme"));
  QVERIFY(scheme.has("View", "BackgroundNormal"));
  QCOMPARE(scheme.color("View", "BackgroundNormal"), QColor(10, 20, 30));
  QCOMPARE(scheme.color("Window", "ForegroundNormal"), QColor(205, 215, 225));
  QCOMPARE(scheme.color("Selection", "BackgroundNormal"), QColor(70, 140, 210));
}

void TestKdeColorScheme::load_emptyFileReturnsError() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString path = writeScheme(tmp, "Empty.colors", QString());
  QVERIFY(!path.isEmpty());

  auto result = KdeColorScheme::load(path);
  // Empty payload yields zero colors → loader returns InvalidArgument
  // so callers don't silently apply a no-op scheme.
  QVERIFY(result.isError());
}

void TestKdeColorScheme::load_missingFileReturnsError() {
  auto result = KdeColorScheme::load(QStringLiteral("/nonexistent/path/foo.colors"));
  QVERIFY(result.isError());
}

void TestKdeColorScheme::load_skipsMalformedRgbTriples() {
  // Mixed valid + invalid entries — invalid ones should be silently
  // skipped rather than rejecting the whole file. Real-world .colors
  // files occasionally have stray non-color keys (e.g. `Enable=true`
  // that somehow lives under [Colors:View]).
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString body =
      QStringLiteral("[General]\nName=Mixed\n[Colors:View]\n"
                     "BackgroundNormal=10,20,30\n"
                     "BadEntry=not,a,real,color,value\n"
                     "AlsoBad=true\n"
                     "ForegroundNormal=200,200,200\n");
  const QString path = writeScheme(tmp, "Mixed.colors", body);
  QVERIFY(!path.isEmpty());

  auto result = KdeColorScheme::load(path);
  QVERIFY(result.isOk());
  const auto scheme = result.value();
  QCOMPARE(scheme.color("View", "BackgroundNormal"), QColor(10, 20, 30));
  QCOMPARE(scheme.color("View", "ForegroundNormal"), QColor(200, 200, 200));
  QVERIFY(!scheme.has("View", "BadEntry"));
  QVERIFY(!scheme.has("View", "AlsoBad"));
}

void TestKdeColorScheme::applyToCollection_mapsAllStandardSlots() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString path = writeScheme(tmp, "Test.colors", MINIMAL_SCHEME);
  auto result = KdeColorScheme::load(path);
  QVERIFY(result.isOk());

  CollectionConfig cfg;
  KdeColorScheme::applyToCollection(result.value(), cfg);

  // Spot-check the three primary mappings — full mapping table is
  // documented in kdecolorscheme.cpp's applyToCollection().
  QCOMPARE(cfg.background.backgroundColor, QColor(10, 20, 30).name());
  QCOMPARE(cfg.background.primaryColor, QColor(15, 25, 35).name());
  QCOMPARE(cfg.background.selectionColor, QColor(70, 140, 210).name());
  QCOMPARE(cfg.background.tileColor, QColor(40, 50, 60).name());
  QCOMPARE(cfg.sidebar.sidebarTextColor, QColor(205, 215, 225).name());
}

void TestKdeColorScheme::applyToCollection_leavesUnsetSlotsUntouched() {
  // A scheme that only has [Colors:Window] entries should leave the
  // View/Selection-driven fields on the CollectionConfig untouched
  // (additive merge, not destructive overwrite).
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString partial =
      QStringLiteral("[General]\nName=Partial\n[Colors:Window]\n"
                     "BackgroundNormal=11,22,33\n");
  const QString path = writeScheme(tmp, "Partial.colors", partial);
  auto result = KdeColorScheme::load(path);
  QVERIFY(result.isOk());

  CollectionConfig cfg;
  cfg.background.backgroundColor = QStringLiteral("#aabbcc"); // pre-existing value
  cfg.background.selectionColor = QStringLiteral("#ddeeff");
  KdeColorScheme::applyToCollection(result.value(), cfg);

  // primaryColor came from Window/BackgroundNormal — applied.
  QCOMPARE(cfg.background.primaryColor, QColor(11, 22, 33).name());
  // View-backed and Selection-backed fields stayed put.
  QCOMPARE(cfg.background.backgroundColor, QStringLiteral("#aabbcc"));
  QCOMPARE(cfg.background.selectionColor, QStringLiteral("#ddeeff"));
}

void TestKdeColorScheme::applyToGeneralSettings_setsTitleBaseColorFromSelectionBackground() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString path = writeScheme(tmp, "Test.colors", MINIMAL_SCHEME);
  auto result = KdeColorScheme::load(path);
  QVERIFY(result.isOk());

  GeneralSettings settings;
  settings.titleBaseColor = QStringLiteral("#000000"); // pre-existing
  KdeColorScheme::applyToGeneralSettings(result.value(), settings);
  QCOMPARE(settings.titleBaseColor, QColor(70, 140, 210).name());
}

void TestKdeColorScheme::discover_includesBundledSchemes() {
  // The three Kartend-bundled .colors files MUST always show up first
  // in the picker. discover() can also surface system-installed schemes
  // (only on KDE Plasma test boxes) — we don't assert on those, just on
  // the bundled trio's presence + their `isBundled=true` flag.
  const auto schemes = KdeColorScheme::discover();
  QStringList bundledNames;
  for (const auto &s : schemes) {
    if (s.isBundled) {
      bundledNames.append(s.displayName);
    }
  }
  QVERIFY2(bundledNames.contains(QStringLiteral("Kartend Dark")),
           qPrintable(QStringLiteral("Got: %1").arg(bundledNames.join(','))));
  QVERIFY(bundledNames.contains(QStringLiteral("Kartend Light")));
  QVERIFY(bundledNames.contains(QStringLiteral("Kartend Neon")));

  // Bundled entries must come before any system entries — the picker
  // surfaces them at the top so non-KDE installs always have something
  // visible.
  bool sawSystemEntry = false;
  for (const auto &s : schemes) {
    if (!s.isBundled) {
      sawSystemEntry = true;
    } else if (sawSystemEntry) {
      QFAIL("Bundled scheme appeared after a system-installed entry");
    }
  }
}

QTEST_MAIN(TestKdeColorScheme)
#include "test_kdecolorscheme.moc"
