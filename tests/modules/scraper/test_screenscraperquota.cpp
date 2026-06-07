// Tests for ScreenScraperQuotaManager (Kartend-vyz9u).
//
// SS embeds the account's `ssuser` quota block in every jeuInfos.php reply;
// QuotaManager parses it into a QuotaStatus snapshot. The behaviour that
// matters for rate-limit correctness: a valid block updates the snapshot, an
// anonymous-tier reply (no `ssuser`) is a no-op that preserves the prior
// snapshot rather than clobbering it. No HTTP machinery involved.
#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QTest>

#include "scrapertypes.h"
#include "screenscraperquotamanager.h"

namespace {

// A jeuInfos.php-shaped reply carrying an `ssuser` quota block. SS sends the
// counters as JSON numbers; the parser also accepts strings.
QByteArray quotaReply(int used, int max, int koUsed, int koMax) {
  return QStringLiteral("{\"response\":{\"ssuser\":{"
                        "\"id\":\"acct\",\"niveau\":\"1\","
                        "\"requeststoday\":%1,\"maxrequestsperday\":%2,"
                        "\"requestskotoday\":%3,\"maxrequestskoperday\":%4}}}")
      .arg(used)
      .arg(max)
      .arg(koUsed)
      .arg(koMax)
      .toUtf8();
}

} // namespace

class TestScreenScraperQuota : public QObject {
  Q_OBJECT
private slots:
  void validBlock_populatesSnapshot();
  void noSsuserBlock_isNoOpAndStaysInvalid();
  void noSsuserBlock_preservesPriorSnapshot();
  void stringEncodedCounters_areParsed();
};

void TestScreenScraperQuota::validBlock_populatesSnapshot() {
  ScreenScraperQuotaManager quota;
  QVERIFY(!quota.status().valid); // nothing parsed yet

  QVERIFY(quota.updateFromResponse(quotaReply(40, 20000, 1, 50000)));

  const Scraper::QuotaStatus s = quota.status();
  QVERIFY(s.valid);
  QCOMPARE(s.dailyUsed, 40);
  QCOMPARE(s.dailyMax, 20000);
  QCOMPARE(s.koUsed, 1);
  QCOMPARE(s.koMax, 50000);
  // Reset rolls at the next 00:00 UTC: a valid future instant, exactly midnight
  // UTC, within 24h of now — uniquely the NEXT midnight, without an exact date
  // compare that would flake if the test crossed midnight mid-run.
  const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
  QVERIFY(s.resetAtUtc.isValid());
  QCOMPARE(s.resetAtUtc.timeSpec(), Qt::UTC);
  QCOMPARE(s.resetAtUtc.time(), QTime(0, 0));
  QVERIFY(s.resetAtUtc > nowUtc);
  QVERIFY(s.resetAtUtc <= nowUtc.addDays(1));
}

void TestScreenScraperQuota::noSsuserBlock_isNoOpAndStaysInvalid() {
  ScreenScraperQuotaManager quota;

  // Anonymous-tier reply: response object with no ssuser block.
  QVERIFY(!quota.updateFromResponse(QByteArrayLiteral("{\"response\":{}}")));
  QVERIFY(!quota.status().valid);

  // A non-JSON error blob is also a no-op, not a crash.
  QVERIFY(!quota.updateFromResponse(QByteArrayLiteral("Erreur : API closed")));
  QVERIFY(!quota.status().valid);
}

void TestScreenScraperQuota::noSsuserBlock_preservesPriorSnapshot() {
  ScreenScraperQuotaManager quota;
  QVERIFY(quota.updateFromResponse(quotaReply(123, 20000, 2, 50000)));
  QVERIFY(quota.status().valid);

  // A subsequent anonymous reply must NOT wipe the last good snapshot —
  // otherwise the dialog flickers between "quota known" and "unknown".
  QVERIFY(!quota.updateFromResponse(QByteArrayLiteral("{\"response\":{}}")));

  const Scraper::QuotaStatus s = quota.status();
  QVERIFY(s.valid);
  QCOMPARE(s.dailyUsed, 123);
  QCOMPARE(s.dailyMax, 20000);
}

void TestScreenScraperQuota::stringEncodedCounters_areParsed() {
  // Some SS endpoint variants quote the numbers; the parser's asInt handles it.
  ScreenScraperQuotaManager quota;
  const QByteArray reply = QByteArrayLiteral(
      "{\"response\":{\"ssuser\":{\"requeststoday\":\"77\",\"maxrequestsperday\":\"20000\"}}}");
  QVERIFY(quota.updateFromResponse(reply));
  QCOMPARE(quota.status().dailyUsed, 77);
  QCOMPARE(quota.status().dailyMax, 20000);
}

QTEST_MAIN(TestScreenScraperQuota)
#include "test_screenscraperquota.moc"
