// Kartend-twq6j: the `_shared/provenance.json` record of what Kartend wrote
// and the catalogue-hash fetch plan a platform re-scrape runs before touching
// the network. Pure temp-dir tests: no service, no provider, no threads.

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include "scrapeartprovenance.h"
#include "scrapertypes.h"

namespace {

QString md5Of(const QByteArray &bytes) {
  return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Md5).toHex());
}

bool writeFile(const QString &path, const QByteArray &bytes) {
  if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly)) return false;
  return f.write(bytes) == bytes.size();
}

QByteArray readFile(const QString &path) {
  QFile f(path);
  return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

Scraper::MediaAsset platformAsset(const QString &type, const QString &catalogMd5) {
  Scraper::MediaAsset a;
  a.type = type;
  a.scope = Scraper::MediaScope::Platform;
  a.scopeKey = QStringLiteral("4");
  a.url = QUrl(QStringLiteral("https://example.test/%1.png").arg(type));
  a.catalogMd5 = catalogMd5;
  return a;
}

} // namespace

class TestScrapeArtProvenance : public QObject {
  Q_OBJECT

private slots:
  void sharedRelativePath_onlyInsideSharedDir();
  void recordAndLoad_roundTripsAndMerges();
  void recordWrittenFiles_hashesOnlySharedPaths();
  void load_toleratesMissingAndCorruptRecords();
  void plan_fetchesWhenNothingOnDiskOrNoCatalogHash();
  void plan_skipsFileMatchingCatalogAndConfirmsIt();
  void plan_refetchesOurFileWhenCatalogMoved();
  void plan_keepsFileMatchingNeither();
  void plan_findsSvgOnDisk();
};

void TestScrapeArtProvenance::sharedRelativePath_onlyInsideSharedDir() {
  const QString art = QStringLiteral("/art/snes");
  QCOMPARE(Scraper::ArtProvenance::sharedRelativePath(
               art, QStringLiteral("/art/snes/_shared/wheel/platform_4.png")),
           QStringLiteral("wheel/platform_4.png"));
  // Per-game art, a sibling collection, the _shared dir itself: not recorded.
  QVERIFY(
      Scraper::ArtProvenance::sharedRelativePath(art, QStringLiteral("/art/snes/front/Alpha.png"))
          .isEmpty());
  QVERIFY(Scraper::ArtProvenance::sharedRelativePath(
              art, QStringLiteral("/art/nes/_shared/wheel/platform_3.png"))
              .isEmpty());
  QVERIFY(Scraper::ArtProvenance::sharedRelativePath(art, QStringLiteral("/art/snes/_shared"))
              .isEmpty());
  QVERIFY(Scraper::ArtProvenance::sharedRelativePath(QString(), QStringLiteral("/x")).isEmpty());
  QVERIFY(Scraper::ArtProvenance::sharedRelativePath(art, QString()).isEmpty());
}

void TestScrapeArtProvenance::recordAndLoad_roundTripsAndMerges() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(Scraper::ArtProvenance::loadRecordedHashes(tmp.path()).isEmpty());
  QVERIFY(Scraper::ArtProvenance::recordHashes(tmp.path(), {})); // empty: no-op, no file
  QVERIFY(!QFile::exists(Scraper::ArtProvenance::provenanceFilePath(tmp.path())));

  QVERIFY(Scraper::ArtProvenance::recordHashes(
      tmp.path(), {{QStringLiteral("wheel/platform_4.png"), QStringLiteral("AABB")}}));
  QVERIFY(QFile::exists(Scraper::ArtProvenance::provenanceFilePath(tmp.path())));
  // A second record merges: the first entry survives, hashes are lowercased.
  QVERIFY(Scraper::ArtProvenance::recordHashes(
      tmp.path(), {{QStringLiteral("icon/platform_4.png"), QStringLiteral("ccdd")}}));
  const auto loaded = Scraper::ArtProvenance::loadRecordedHashes(tmp.path());
  QCOMPARE(loaded.size(), 2);
  QCOMPARE(loaded.value(QStringLiteral("wheel/platform_4.png")), QStringLiteral("aabb"));
  QCOMPARE(loaded.value(QStringLiteral("icon/platform_4.png")), QStringLiteral("ccdd"));
  // Re-recording a path replaces its hash.
  QVERIFY(Scraper::ArtProvenance::recordHashes(
      tmp.path(), {{QStringLiteral("wheel/platform_4.png"), QStringLiteral("eeff")}}));
  QCOMPARE(Scraper::ArtProvenance::loadRecordedHashes(tmp.path())
               .value(QStringLiteral("wheel/platform_4.png")),
           QStringLiteral("eeff"));
  // The file is the documented shape.
  const QJsonObject root =
      QJsonDocument::fromJson(readFile(Scraper::ArtProvenance::provenanceFilePath(tmp.path())))
          .object();
  QCOMPARE(root.value(QStringLiteral("version")).toInt(), 1);
  QVERIFY(root.value(QStringLiteral("assets"))
              .toObject()
              .contains(QStringLiteral("icon/platform_4.png")));
}

void TestScrapeArtProvenance::recordWrittenFiles_hashesOnlySharedPaths() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString shared = QDir(tmp.path()).filePath(QStringLiteral("_shared/wheel/platform_4.png"));
  const QString perGame = QDir(tmp.path()).filePath(QStringLiteral("front/Alpha.png"));
  QVERIFY(writeFile(shared, "WHEEL"));
  QVERIFY(writeFile(perGame, "FRONT"));
  QVERIFY(Scraper::ArtProvenance::recordWrittenFiles(
      tmp.path(),
      {shared, perGame, QDir(tmp.path()).filePath(QStringLiteral("_shared/missing.png"))}));
  const auto loaded = Scraper::ArtProvenance::loadRecordedHashes(tmp.path());
  QCOMPARE(loaded.size(), 1);
  QCOMPARE(loaded.value(QStringLiteral("wheel/platform_4.png")), md5Of("WHEEL"));
}

void TestScrapeArtProvenance::load_toleratesMissingAndCorruptRecords() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(Scraper::ArtProvenance::loadRecordedHashes(QString()).isEmpty());
  QVERIFY(writeFile(Scraper::ArtProvenance::provenanceFilePath(tmp.path()), "{not json"));
  QVERIFY(Scraper::ArtProvenance::loadRecordedHashes(tmp.path()).isEmpty());
  // A corrupt record is replaced on the next write rather than poisoning it.
  QVERIFY(Scraper::ArtProvenance::recordHashes(
      tmp.path(), {{QStringLiteral("wheel/platform_4.png"), QStringLiteral("aabb")}}));
  QCOMPARE(Scraper::ArtProvenance::loadRecordedHashes(tmp.path()).size(), 1);
}

void TestScrapeArtProvenance::plan_fetchesWhenNothingOnDiskOrNoCatalogHash() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QByteArray bytes("WHEEL");
  QVERIFY(
      writeFile(QDir(tmp.path()).filePath(QStringLiteral("_shared/icon/platform_4.png")), bytes));
  const QList<Scraper::MediaAsset> assets = {
      platformAsset(QStringLiteral("wheel"), md5Of(bytes)), // no file yet
      platformAsset(QStringLiteral("icon"), QString()),     // file, but nothing to compare
  };
  const auto plan = Scraper::ArtProvenance::planFetches(assets, tmp.path());
  QCOMPARE(plan.fetch.size(), 2);
  QVERIFY(plan.upToDatePaths.isEmpty());
  QVERIFY(plan.keptLocalPaths.isEmpty());
  QVERIFY(plan.confirmed.isEmpty());
  // No artwork dir at all: everything is fetched, nothing is touched.
  QCOMPARE(Scraper::ArtProvenance::planFetches(assets, QString()).fetch.size(), 2);
}

void TestScrapeArtProvenance::plan_skipsFileMatchingCatalogAndConfirmsIt() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QByteArray bytes("WHEEL");
  const QString path = QDir(tmp.path()).filePath(QStringLiteral("_shared/wheel/platform_4.png"));
  QVERIFY(writeFile(path, bytes));
  // Catalogue hashes arrive in whatever case SS uses; comparison is case-blind.
  const auto plan = Scraper::ArtProvenance::planFetches(
      {platformAsset(QStringLiteral("wheel"), md5Of(bytes).toUpper())}, tmp.path());
  QVERIFY(plan.fetch.isEmpty());
  QCOMPARE(plan.upToDatePaths, QStringList{path});
  QVERIFY(plan.keptLocalPaths.isEmpty());
  QCOMPARE(plan.confirmed.value(QStringLiteral("wheel/platform_4.png")), md5Of(bytes));
  // Planning never writes the record itself — that is the caller's call.
  QVERIFY(!QFile::exists(Scraper::ArtProvenance::provenanceFilePath(tmp.path())));
}

void TestScrapeArtProvenance::plan_refetchesOurFileWhenCatalogMoved() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QByteArray ours("OLD_WHEEL");
  const QString path = QDir(tmp.path()).filePath(QStringLiteral("_shared/wheel/platform_4.png"));
  QVERIFY(writeFile(path, ours));
  QVERIFY(Scraper::ArtProvenance::recordHashes(
      tmp.path(), {{QStringLiteral("wheel/platform_4.png"), md5Of(ours)}}));
  const auto plan = Scraper::ArtProvenance::planFetches(
      {platformAsset(QStringLiteral("wheel"), md5Of("NEW_WHEEL"))}, tmp.path());
  QCOMPARE(plan.fetch.size(), 1);
  QVERIFY(plan.upToDatePaths.isEmpty());
  QVERIFY(plan.keptLocalPaths.isEmpty());
  QCOMPARE(readFile(path), ours); // planning is read-only
}

void TestScrapeArtProvenance::plan_keepsFileMatchingNeither() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QByteArray theirs("HAND_MADE_WHEEL");
  const QString path = QDir(tmp.path()).filePath(QStringLiteral("_shared/wheel/platform_4.png"));
  QVERIFY(writeFile(path, theirs));
  // No record at all (pre-dates the feature, or hand-placed): the file is
  // treated as the user's and left alone.
  auto plan = Scraper::ArtProvenance::planFetches(
      {platformAsset(QStringLiteral("wheel"), md5Of("NEW_WHEEL"))}, tmp.path());
  QVERIFY(plan.fetch.isEmpty());
  QCOMPARE(plan.keptLocalPaths, QStringList{path});
  // A record that names a DIFFERENT hash than what is on disk means the same
  // thing: someone replaced our file after we wrote it.
  QVERIFY(Scraper::ArtProvenance::recordHashes(
      tmp.path(), {{QStringLiteral("wheel/platform_4.png"), md5Of("WHAT_WE_WROTE")}}));
  plan = Scraper::ArtProvenance::planFetches(
      {platformAsset(QStringLiteral("wheel"), md5Of("NEW_WHEEL"))}, tmp.path());
  QVERIFY(plan.fetch.isEmpty());
  QCOMPARE(plan.keptLocalPaths, QStringList{path});
  QCOMPARE(readFile(path), theirs);
}

void TestScrapeArtProvenance::plan_findsSvgOnDisk() {
  // ScreenScraper serves vector logos as .svg and the writer keeps the
  // suffix; the probe has to find those too or every re-scrape refetches them.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QByteArray bytes("<svg/>");
  const QString path = QDir(tmp.path()).filePath(QStringLiteral("_shared/logo-svg/platform_4.svg"));
  QVERIFY(writeFile(path, bytes));
  const auto plan = Scraper::ArtProvenance::planFetches(
      {platformAsset(QStringLiteral("logo-svg"), md5Of(bytes))}, tmp.path());
  QVERIFY(plan.fetch.isEmpty());
  QCOMPARE(plan.upToDatePaths, QStringList{path});
}

QTEST_GUILESS_MAIN(TestScrapeArtProvenance)
#include "test_scrapeartprovenance.moc"
