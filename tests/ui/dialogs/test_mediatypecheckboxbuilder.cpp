// Kartend-0eeuk: MediaTypeCheckboxBuilder construction + state mapping. The
// curated table in mediatypecheckboxbuilder.cpp is the source of truth for
// which ScreenScraper media types surface in the unified scrape panel; these
// tests pin the key-set contract the BatchScrapeRunner filter relies on:
// every key is lowercase (the runner matches `type.toLower()` against the
// set), the synthetic `_metadata` gate is present, the default-on set is
// exactly {_metadata, front}, and the Select all / Select none header
// buttons bulk-toggle every checkbox. Driven headlessly — never shown.

#include "mediatypecheckboxbuilder.h"

#include <QByteArray>
#include <QCheckBox>
#include <QGroupBox>
#include <QHash>
#include <QPushButton>
#include <QScopedPointer>
#include <QStringList>
#include <QTest>

#include "screenscraperparser.h"

namespace {

QPushButton *buttonWithText(const QWidget &root, const QString &text) {
  const auto buttons = root.findChildren<QPushButton *>();
  for (QPushButton *b : buttons) {
    if (b->text() == text) {
      return b;
    }
  }
  return nullptr;
}

} // namespace

class TestMediaTypeCheckboxBuilder : public QObject {
  Q_OBJECT

private slots:
  void buildsThirtyTwoUniqueLowercaseKeys();
  void everyMediaTypeInALiveResponseHasACheckbox();
  void defaultOnSetIsMetadataAndFrontOnly();
  void selectAllChecksEveryBox();
  void selectNoneClearsEveryBox();
  void applyProviderDefaultsReticksMediaButNotMetadata();
  void applyProviderDefaultsEmptySetIsNoOp();
};

namespace {

// Verbatim `medias[]` from a live anonymous-tier jeuInfos.php response —
// dev credentials only, NO ssid — captured 2026-08-31 for "Nova the Squirrel"
// (systemeid 3, jeuid 199992). Kept faithful on purpose: the real `parent` /
// `subparent` / `id` keys, the region-less entries, the `ss` region tag SS
// uses for its own homebrew box art, the duplicate `pictoliste` rows under
// different groupids, and the three distinct media hosts/endpoints
// (mediaJeu / mediaGroup / mediaVideoJeu). Only devpassword is redacted.
//
// The response is trimmed to ONE row per (type, parent) pair; the live payload
// had 30 rows over these 24, which differ only in groupid.
const QByteArray LIVE_ANON_RESPONSE = R"json({
  "header": {"APIversion": "2"},
  "response": {
    "ssuser": {"id": "", "niveau": "0", "maxthreads": "1"},
    "jeu": {
      "id": "199992",
      "noms": [{"region": "ss", "text": "Nova the Squirrel"}],
      "rom": {"romregions": "ss"},
      "medias": [
        {"type": "sstitle", "parent": "jeu", "region": "wor", "format": "png",
         "url": "https://neoclone.screenscraper.fr/api2/mediaJeu.php?devid=cedar&devpassword=X&softname=kartend&ssid=&sspassword=&systemeid=3&jeuid=199992&media=sstitle(wor)"},
        {"type": "ss", "parent": "jeu", "region": "wor", "format": "png",
         "url": "https://neoclone.screenscraper.fr/api2/mediaJeu.php?devid=cedar&devpassword=X&softname=kartend&ssid=&sspassword=&systemeid=3&jeuid=199992&media=ss(wor)"},
        {"type": "video", "parent": "jeu", "format": "mp4",
         "url": "https://neoclone.screenscraper.fr/api2/mediaVideoJeu.php?devid=cedar&devpassword=X&softname=kartend&ssid=&sspassword=&systemeid=3&jeuid=199992&media=video"},
        {"type": "video-normalized", "parent": "jeu", "format": "mp4",
         "url": "https://neoclone.screenscraper.fr/api2/mediaVideoJeu.php?devid=cedar&devpassword=X&softname=kartend&ssid=&sspassword=&systemeid=3&jeuid=199992&media=video-normalized"},
        {"type": "steamgrid", "parent": "jeu", "format": "jpg",
         "url": "https://neoclone.screenscraper.fr/api2/mediaJeu.php?devid=cedar&devpassword=X&softname=kartend&ssid=&sspassword=&systemeid=3&jeuid=199992&media=steamgrid"},
        {"type": "wheel", "parent": "jeu", "region": "wor", "format": "png",
         "url": "https://neoclone.screenscraper.fr/api2/mediaJeu.php?devid=cedar&devpassword=X&softname=kartend&ssid=&sspassword=&systemeid=3&jeuid=199992&media=wheel(wor)"},
        {"type": "wheel-carbon", "parent": "jeu", "region": "wor", "format": "png",
         "url": "https://neoclone.screenscraper.fr/api2/mediaJeu.php?devid=cedar&devpassword=X&softname=kartend&ssid=&sspassword=&systemeid=3&jeuid=199992&media=wheel-carbon(wor)"},
        {"type": "wheel-steel", "parent": "jeu", "region": "wor", "format": "png",
         "url": "https://neoclone.screenscraper.fr/api2/mediaJeu.php?devid=cedar&devpassword=X&softname=kartend&ssid=&sspassword=&systemeid=3&jeuid=199992&media=wheel-steel(wor)"},
        {"type": "screenmarquee", "parent": "jeu", "region": "wor", "format": "png",
         "url": "https://neoclone.screenscraper.fr/api2/mediaJeu.php?devid=cedar&devpassword=X&softname=kartend&ssid=&sspassword=&systemeid=3&jeuid=199992&media=screenmarquee(wor)"},
        {"type": "screenmarqueesmall", "parent": "jeu", "region": "wor", "format": "png",
         "url": "https://neoclone.screenscraper.fr/api2/mediaJeu.php?devid=cedar&devpassword=X&softname=kartend&ssid=&sspassword=&systemeid=3&jeuid=199992&media=screenmarqueesmall(wor)"},
        {"type": "box-2D", "parent": "jeu", "region": "ss", "format": "png",
         "url": "https://neoclone.screenscraper.fr/api2/mediaJeu.php?devid=cedar&devpassword=X&softname=kartend&ssid=&sspassword=&systemeid=3&jeuid=199992&media=box-2D(ss)"},
        {"type": "box-2D-side", "parent": "jeu", "region": "ss", "format": "png",
         "url": "https://neoclone.screenscraper.fr/api2/mediaJeu.php?devid=cedar&devpassword=X&softname=kartend&ssid=&sspassword=&systemeid=3&jeuid=199992&media=box-2D-side(ss)"},
        {"type": "box-2D-back", "parent": "jeu", "region": "ss", "format": "png",
         "url": "https://neoclone.screenscraper.fr/api2/mediaJeu.php?devid=cedar&devpassword=X&softname=kartend&ssid=&sspassword=&systemeid=3&jeuid=199992&media=box-2D-back(ss)"},
        {"type": "box-texture", "parent": "jeu", "region": "ss", "format": "png",
         "url": "https://neoclone.screenscraper.fr/api2/mediaJeu.php?devid=cedar&devpassword=X&softname=kartend&ssid=&sspassword=&systemeid=3&jeuid=199992&media=box-texture(ss)"},
        {"type": "box-3D", "parent": "jeu", "region": "ss", "format": "png",
         "url": "https://neoclone.screenscraper.fr/api2/mediaJeu.php?devid=cedar&devpassword=X&softname=kartend&ssid=&sspassword=&systemeid=3&jeuid=199992&media=box-3D(ss)"},
        {"type": "support-texture", "parent": "jeu", "region": "wor", "format": "png",
         "url": "https://neoclone.screenscraper.fr/api2/mediaJeu.php?devid=cedar&devpassword=X&softname=kartend&ssid=&sspassword=&systemeid=3&jeuid=199992&media=support-texture(wor)"},
        {"type": "support-2D", "parent": "jeu", "region": "wor", "format": "png",
         "url": "https://neoclone.screenscraper.fr/api2/mediaJeu.php?devid=cedar&devpassword=X&softname=kartend&ssid=&sspassword=&systemeid=3&jeuid=199992&media=support-2D(wor)"},
        {"type": "mixrbv1", "parent": "jeu", "region": "ss", "format": "png",
         "url": "https://neoclone.screenscraper.fr/api2/mediaJeu.php?devid=cedar&devpassword=X&softname=kartend&ssid=&sspassword=&systemeid=3&jeuid=199992&media=mixrbv1(ss)"},
        {"type": "mixrbv2", "parent": "jeu", "region": "ss", "format": "png",
         "url": "https://neoclone.screenscraper.fr/api2/mediaJeu.php?devid=cedar&devpassword=X&softname=kartend&ssid=&sspassword=&systemeid=3&jeuid=199992&media=mixrbv2(ss)"},
        {"type": "pictoliste", "parent": "joueurs", "format": "png",
         "url": "https://neoclone.screenscraper.fr/api2/mediaGroup.php?devid=cedar&devpassword=X&softname=kartend&ssid=&sspassword=&groupid=3309&media=pictoliste"},
        {"type": "pictoliste", "parent": "note", "format": "png",
         "url": "https://neoclone.screenscraper.fr/api2/mediaGroup.php?devid=cedar&devpassword=X&softname=kartend&ssid=&sspassword=&groupid=3553&media=pictoliste"},
        {"id": "2915", "type": "pictoliste", "parent": "genre", "subparent": "Plateforme / Run & Jump", "format": "png",
         "url": "https://neoclone.screenscraper.fr/api2/mediaGroup.php?devid=cedar&devpassword=X&softname=kartend&ssid=&sspassword=&groupid=2915&media=pictoliste"},
        {"id": "2915", "type": "pictomonochrome", "parent": "genre", "subparent": "Plateforme / Run & Jump", "format": "png",
         "url": "https://neoclone.screenscraper.fr/api2/mediaGroup.php?devid=cedar&devpassword=X&softname=kartend&ssid=&sspassword=&groupid=2915&media=logo-monochrome"},
        {"id": "7", "type": "background", "parent": "genre", "subparent": "Plateforme", "format": "jpg",
         "url": "https://neoclone.screenscraper.fr/api2/mediaGroup.php?devid=cedar&devpassword=X&softname=kartend&ssid=&sspassword=&groupid=7&media=background"}
      ]
    }
  }
})json";

} // namespace

void TestMediaTypeCheckboxBuilder::buildsThirtyTwoUniqueLowercaseKeys() {
  QHash<QString, QCheckBox *> checks;
  // Null parent is documented as supported; own the result locally.
  QScopedPointer<QGroupBox> group(MediaTypeCheckboxBuilder::build(nullptr, checks));
  QVERIFY(group);

  // 32 curated entries (matches the class comment, corrected in
  // Kartend-29vam).
  // The QHash matching the checkbox count proves the keys are unique — a
  // duplicate key would orphan one checkbox outside the filter map.
  QCOMPARE(checks.size(), 32);
  QCOMPARE(group->findChildren<QCheckBox *>().size(), checks.size());

  // The synthetic metadata gate plus a few collapsed-alias anchors the
  // runtime filter set depends on (box-2D → front, sstitle → title,
  // manuel → manual).
  QVERIFY(checks.contains(QStringLiteral("_metadata")));
  QVERIFY(checks.contains(QStringLiteral("front")));
  QVERIFY(checks.contains(QStringLiteral("title")));
  QVERIFY(checks.contains(QStringLiteral("manual")));
  QVERIFY(checks.contains(QStringLiteral("screenshot")));

  // The runner lowercases asset types before matching — an uppercase key
  // here would silently never match.
  for (auto it = checks.constBegin(); it != checks.constEnd(); ++it) {
    QVERIFY2(it.key() == it.key().toLower(),
             qPrintable(QStringLiteral("key not lowercase: %1").arg(it.key())));
    QVERIFY(it.value() != nullptr);
  }
}

void TestMediaTypeCheckboxBuilder::everyMediaTypeInALiveResponseHasACheckbox() {
  // Kartend-lqfox: "scraped N items, 0 media" with every artwork type ticked.
  // The two halves of the media filter live in different modules and are only
  // joined at runtime — ScreenScraperParser canonicalizes each SS tag, and the
  // runner matches `asset.type.toLower()` against the key set built HERE. A
  // key the parser never produces is dead; a canonical type with no key is
  // silently unfetchable no matter what the user ticks. Nothing pinned that
  // correspondence against a REAL payload before, so the drift was invisible.
  //
  // Drives both halves off one captured anonymous-tier response so a change to
  // either side fails here. Also standing evidence for the issue: the
  // credential-free tier DOES carry a full medias[] — the empty-media theory
  // that was suspected is disproved by this fixture existing.
  QHash<QString, QCheckBox *> checks;
  QScopedPointer<QGroupBox> group(MediaTypeCheckboxBuilder::build(nullptr, checks));
  QVERIFY(group);

  const auto parsed = ScreenScraperParser::parseDetailResponse(LIVE_ANON_RESPONSE);
  QVERIFY2(parsed.isOk(), "live anonymous-tier payload must parse");
  const QList<Scraper::MediaAsset> media = parsed.value().media;

  // 24 rows collapse to 22 canonical types: ss → screenshot, sstitle → title,
  // box-2D → front, and the three pictoliste rows dedup to one.
  QCOMPARE(media.size(), 22);

  QStringList missing;
  for (const auto &asset : media) {
    if (!checks.contains(asset.type.toLower())) {
      missing.append(asset.type);
    }
  }
  QVERIFY2(missing.isEmpty(),
           qPrintable(QStringLiteral("canonical types with no checkbox (unfetchable however "
                                     "the user ticks the panel): %1")
                          .arg(missing.join(QStringLiteral(", ")))));

  // The alias collapses specifically: these are the keys the panel offers, so
  // the parser must emit the collapsed form and never the raw SS tag.
  QStringList types;
  types.reserve(media.size());
  for (const auto &asset : media) {
    types.append(asset.type);
  }
  QVERIFY2(types.contains(QStringLiteral("front")), qPrintable(types.join(QLatin1Char(','))));
  QVERIFY(types.contains(QStringLiteral("screenshot")));
  QVERIFY(types.contains(QStringLiteral("title")));
  QVERIFY(!types.contains(QStringLiteral("box-2D")));
  QVERIFY(!types.contains(QStringLiteral("ss")));
  QVERIFY(!types.contains(QStringLiteral("sstitle")));
}

void TestMediaTypeCheckboxBuilder::defaultOnSetIsMetadataAndFrontOnly() {
  QHash<QString, QCheckBox *> checks;
  QScopedPointer<QGroupBox> group(MediaTypeCheckboxBuilder::build(nullptr, checks));
  QVERIFY(group);

  for (auto it = checks.constBegin(); it != checks.constEnd(); ++it) {
    const bool expectedOn =
        it.key() == QStringLiteral("_metadata") || it.key() == QStringLiteral("front");
    QVERIFY2(it.value()->isChecked() == expectedOn,
             qPrintable(QStringLiteral("default for %1 wrong").arg(it.key())));
  }
}

void TestMediaTypeCheckboxBuilder::selectAllChecksEveryBox() {
  QHash<QString, QCheckBox *> checks;
  QScopedPointer<QGroupBox> group(MediaTypeCheckboxBuilder::build(nullptr, checks));
  QVERIFY(group);

  QPushButton *all = buttonWithText(*group, QStringLiteral("Select all"));
  QVERIFY(all);
  all->click();
  for (auto it = checks.constBegin(); it != checks.constEnd(); ++it) {
    QVERIFY2(it.value()->isChecked(),
             qPrintable(QStringLiteral("%1 not checked after Select all").arg(it.key())));
  }
}

void TestMediaTypeCheckboxBuilder::selectNoneClearsEveryBox() {
  QHash<QString, QCheckBox *> checks;
  QScopedPointer<QGroupBox> group(MediaTypeCheckboxBuilder::build(nullptr, checks));
  QVERIFY(group);

  QPushButton *none = buttonWithText(*group, QStringLiteral("Select none"));
  QVERIFY(none);
  none->click(); // clears the two default-on boxes too
  for (auto it = checks.constBegin(); it != checks.constEnd(); ++it) {
    QVERIFY2(!it.value()->isChecked(),
             qPrintable(QStringLiteral("%1 still checked after Select none").arg(it.key())));
  }
}

void TestMediaTypeCheckboxBuilder::applyProviderDefaultsReticksMediaButNotMetadata() {
  // Kartend-6e90v: a provider's curated set re-ticks the media grid — keys
  // in the set on, every other media key off — while the synthetic
  // `_metadata` gate keeps its current state (the curated sets describe
  // media palettes, not whether text fields are wanted).
  QHash<QString, QCheckBox *> checks;
  QScopedPointer<QGroupBox> group(MediaTypeCheckboxBuilder::build(nullptr, checks));
  QVERIFY(group);

  const QStringList steamish = {QStringLiteral("front"), QStringLiteral("screenshot"),
                                QStringLiteral("background"), QStringLiteral("video")};
  MediaTypeCheckboxBuilder::applyProviderDefaults(checks, steamish);

  for (auto it = checks.constBegin(); it != checks.constEnd(); ++it) {
    if (it.key() == QStringLiteral("_metadata")) {
      QVERIFY(it.value()->isChecked()); // untouched table default
      continue;
    }
    QVERIFY2(it.value()->isChecked() == steamish.contains(it.key()),
             qPrintable(QStringLiteral("tick for %1 wrong after provider defaults").arg(it.key())));
  }
}

void TestMediaTypeCheckboxBuilder::applyProviderDefaultsEmptySetIsNoOp() {
  // Providers without a curated set must leave the table defaults alone —
  // an empty list re-ticking everything off would nuke the front default.
  QHash<QString, QCheckBox *> checks;
  QScopedPointer<QGroupBox> group(MediaTypeCheckboxBuilder::build(nullptr, checks));
  QVERIFY(group);

  MediaTypeCheckboxBuilder::applyProviderDefaults(checks, {});

  for (auto it = checks.constBegin(); it != checks.constEnd(); ++it) {
    const bool expectedOn =
        it.key() == QStringLiteral("_metadata") || it.key() == QStringLiteral("front");
    QCOMPARE(it.value()->isChecked(), expectedOn);
  }
}

QTEST_MAIN(TestMediaTypeCheckboxBuilder)
#include "test_mediatypecheckboxbuilder.moc"
