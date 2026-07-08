// Tests for ScrapeAssetDedup — the pure dedup / CRC-short-circuit helpers
// extracted from ScrapeResultDialog (Kartend-dpehr). These exercise the
// scraper download dedup logic with no QDialog, no network, no UI.

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QUrlQuery>

#include "scrapeassetdedup.h"
#include "scrapertypes.h"

using ScrapeAssetDedup::findExistingPerGameAsset;
using ScrapeAssetDedup::findExistingSharedAsset;
using ScrapeAssetDedup::hashLocalFile;
using ScrapeAssetDedup::isHashShortCircuit;
using ScrapeAssetDedup::LocalHashes;
using ScrapeAssetDedup::sharedAssetProbePaths;
using ScrapeAssetDedup::withHashHints;

namespace {
auto makeAsset(const QString &type, Scraper::MediaScope scope, const QString &scopeKey = QString())
    -> Scraper::MediaAsset {
  Scraper::MediaAsset a;
  a.type = type;
  a.scope = scope;
  a.scopeKey = scopeKey;
  a.url = QUrl(QStringLiteral("https://example.test/media?type=") + type);
  return a;
}

auto writeFile(const QString &path, const QByteArray &data) -> bool {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly)) return false;
  f.write(data);
  return true;
}
} // namespace

class TestScrapeAssetDedup : public QObject {
  Q_OBJECT
private slots:
  void sharedProbe_gameScopeYieldsNothing();
  void sharedProbe_emptyInputsYieldNothing();
  void sharedProbe_groupAndCompanyPrefixes();
  void findExistingShared_returnsFirstHit();
  void findExistingShared_emptyWhenAbsent();
  void findExistingPerGame_returnsHit();
  void findExistingPerGame_skipsNonGameAndVideoManual();
  void hashLocalFile_knownDigests();
  void hashLocalFile_unreadableIsEmpty();
  void withHashHints_emptyReturnsOriginal();
  void withHashHints_appendsAndDoesNotDuplicate();
  void isShortCircuit_recognizesSentinels();
  void isShortCircuit_rejectsBodies();
};

void TestScrapeAssetDedup::sharedProbe_gameScopeYieldsNothing() {
  const auto a = makeAsset("box", Scraper::MediaScope::Game, "123");
  QVERIFY(sharedAssetProbePaths(a, "/art").isEmpty());
}

void TestScrapeAssetDedup::sharedProbe_emptyInputsYieldNothing() {
  QVERIFY(
      sharedAssetProbePaths(makeAsset("box", Scraper::MediaScope::Group, ""), "/art").isEmpty());
  QVERIFY(sharedAssetProbePaths(makeAsset("box", Scraper::MediaScope::Group, "g1"), QString())
              .isEmpty());
}

void TestScrapeAssetDedup::sharedProbe_groupAndCompanyPrefixes() {
  const QStringList group =
      sharedAssetProbePaths(makeAsset("box", Scraper::MediaScope::Group, "g1"), "/art");
  QCOMPARE(group.size(), 4); // png/jpg/jpeg/webp
  QVERIFY(group.first().contains("/art/_shared/box/group_g1.png"));

  const QStringList company =
      sharedAssetProbePaths(makeAsset("box", Scraper::MediaScope::Company, "c9"), "/art");
  QVERIFY(company.first().contains("/art/_shared/box/company_c9.png"));
}

void TestScrapeAssetDedup::findExistingShared_returnsFirstHit() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString hit = tmp.filePath("_shared/box/group_g1.jpg");
  QVERIFY(writeFile(hit, "x"));

  const auto a = makeAsset("box", Scraper::MediaScope::Group, "g1");
  const QString found = findExistingSharedAsset(a, {tmp.path()});
  QCOMPARE(QFileInfo(found).canonicalFilePath(), QFileInfo(hit).canonicalFilePath());
}

void TestScrapeAssetDedup::findExistingShared_emptyWhenAbsent() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const auto a = makeAsset("box", Scraper::MediaScope::Group, "missing");
  QVERIFY(findExistingSharedAsset(a, {tmp.path()}).isEmpty());
}

void TestScrapeAssetDedup::findExistingPerGame_returnsHit() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString hit = tmp.filePath("box/MyGame.png");
  QVERIFY(writeFile(hit, "x"));

  const auto a = makeAsset("box", Scraper::MediaScope::Game);
  const QString found = findExistingPerGameAsset(a, tmp.path(), "MyGame");
  QCOMPARE(QFileInfo(found).canonicalFilePath(), QFileInfo(hit).canonicalFilePath());
}

void TestScrapeAssetDedup::findExistingPerGame_skipsNonGameAndVideoManual() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath("video/MyGame.png"), "x"));
  QVERIFY(writeFile(tmp.filePath("box/MyGame.png"), "x"));

  // Non-Game scope is rejected outright.
  QVERIFY(findExistingPerGameAsset(makeAsset("box", Scraper::MediaScope::Group, "g1"), tmp.path(),
                                   "MyGame")
              .isEmpty());
  // Video/manual types skip the CRC short-circuit even when a file exists.
  QVERIFY(
      findExistingPerGameAsset(makeAsset("video", Scraper::MediaScope::Game), tmp.path(), "MyGame")
          .isEmpty());
  QVERIFY(
      findExistingPerGameAsset(makeAsset("manual", Scraper::MediaScope::Game), tmp.path(), "MyGame")
          .isEmpty());
  // Kind-based skip (Kartend-jjyst.1): "video-*" variants are videos too.
  QVERIFY(findExistingPerGameAsset(makeAsset("video-normalized", Scraper::MediaScope::Game),
                                   tmp.path(), "MyGame")
              .isEmpty());
}

void TestScrapeAssetDedup::hashLocalFile_knownDigests() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString p = tmp.filePath("h.bin");
  QVERIFY(writeFile(p, "hello"));

  const LocalHashes h = hashLocalFile(p);
  QCOMPARE(h.md5Hex, QStringLiteral("5d41402abc4b2a76b9719d911017c592"));
  QCOMPARE(h.sha1Hex, QStringLiteral("aaf4c61ddcc5e8a2dabede0f3b482cd9aea9434d"));
  QVERIFY(h.crc32Hex.isEmpty()); // Qt ships no CRC32; left empty by design
}

void TestScrapeAssetDedup::hashLocalFile_unreadableIsEmpty() {
  const LocalHashes h = hashLocalFile("/no/such/file/at/all.bin");
  QVERIFY(h.md5Hex.isEmpty());
  QVERIFY(h.sha1Hex.isEmpty());
}

void TestScrapeAssetDedup::withHashHints_emptyReturnsOriginal() {
  const QUrl original(QStringLiteral("https://example.test/media?type=box"));
  QCOMPARE(withHashHints(original, LocalHashes{}), original);
}

void TestScrapeAssetDedup::withHashHints_appendsAndDoesNotDuplicate() {
  LocalHashes h;
  h.md5Hex = "abc";
  h.sha1Hex = "def";
  const QUrl out = withHashHints(QUrl(QStringLiteral("https://example.test/m?type=box")), h);
  const QUrlQuery q(out);
  QCOMPARE(q.queryItemValue("md5"), QStringLiteral("abc"));
  QCOMPARE(q.queryItemValue("sha1"), QStringLiteral("def"));

  // An already-present md5 is not duplicated/overwritten.
  LocalHashes h2;
  h2.md5Hex = "new";
  const QUrl out2 = withHashHints(QUrl(QStringLiteral("https://example.test/m?md5=orig")), h2);
  const QUrlQuery q2(out2);
  QCOMPARE(q2.allQueryItemValues("md5").size(), 1);
  QCOMPARE(q2.queryItemValue("md5"), QStringLiteral("orig"));
}

void TestScrapeAssetDedup::isShortCircuit_recognizesSentinels() {
  QVERIFY(isHashShortCircuit("MD5OK"));
  QVERIFY(isHashShortCircuit("SHA1OK"));
  QVERIFY(isHashShortCircuit("CRCOK"));
  QVERIFY(isHashShortCircuit("  MD5OK\n")); // trailing whitespace tolerated
}

void TestScrapeAssetDedup::isShortCircuit_rejectsBodies() {
  QVERIFY(!isHashShortCircuit("OK"));
  QVERIFY(!isHashShortCircuit(QByteArray("MD5OK plus a much longer body that exceeds 32 bytes!!")));
  QVERIFY(!isHashShortCircuit(QByteArray())); // empty body is not a sentinel
}

QTEST_GUILESS_MAIN(TestScrapeAssetDedup)
#include "test_scrapeassetdedup.moc"
