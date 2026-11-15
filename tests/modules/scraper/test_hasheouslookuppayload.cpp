// Unit tests for HasheousLookupPayload::buildBody (Kartend-fgd5k) — the pure
// serializer for the outgoing Lookup/ByHash JSON body. No network and no
// server responses: these lock only what Kartend sends.
#include <QtTest/QtTest>

#include <QJsonDocument>
#include <QJsonObject>

#include "errorutils.h"
#include "hasheouslookuppayload.h"
#include "romhasher.h"

using ErrorUtils::ErrorCode;

namespace {
const QString kMd5 = QStringLiteral("d41d8cd98f00b204e9800998ecf8427e");
const QString kSha1 = QStringLiteral("da39a3ee5e6b4b0d3255bfef95601890afd80709");
const QString kCrc = QStringLiteral("00000000");

RomHasher::Result hashes(const QString &md5, const QString &sha1, const QString &crc,
                         qint64 size = -1) {
  RomHasher::Result r;
  r.md5 = md5;
  r.sha1 = sha1;
  r.crc = crc;
  r.size = size;
  return r;
}

// Parse the body, failing the test when it is not a JSON object.
QJsonObject parse(const QByteArray &body) {
  QJsonParseError err{};
  const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
  [&] {
    QCOMPARE(err.error, QJsonParseError::NoError);
    QVERIFY(doc.isObject());
  }();
  return doc.object();
}
} // namespace

class TestHasheousLookupPayload : public QObject {
  Q_OBJECT
private slots:
  void allHashesTogether();
  void eachHashAlone_data();
  void eachHashAlone();
  void valuesArePreservedVerbatim();
  void bodyIsCompactJson();
  void sizeNeverLeaks();
  void emptyInputIsRejected();
  void sizeOnlyInputIsRejected();
};

void TestHasheousLookupPayload::allHashesTogether() {
  const auto result = HasheousLookupPayload::buildBody(hashes(kMd5, kSha1, kCrc, 1024));
  QVERIFY(result.isOk());
  const QJsonObject obj = parse(result.value());
  QCOMPARE(obj.size(), 3);
  QCOMPARE(obj.value(QStringLiteral("MD5")).toString(), kMd5);
  QCOMPARE(obj.value(QStringLiteral("SHA1")).toString(), kSha1);
  QCOMPARE(obj.value(QStringLiteral("CRC")).toString(), kCrc);
}

void TestHasheousLookupPayload::eachHashAlone_data() {
  QTest::addColumn<QString>("md5");
  QTest::addColumn<QString>("sha1");
  QTest::addColumn<QString>("crc");
  QTest::addColumn<QString>("key");
  QTest::addColumn<QString>("value");
  QTest::newRow("md5") << kMd5 << QString() << QString() << "MD5" << kMd5;
  QTest::newRow("sha1") << QString() << kSha1 << QString() << "SHA1" << kSha1;
  QTest::newRow("crc") << QString() << QString() << kCrc << "CRC" << kCrc;
}

void TestHasheousLookupPayload::eachHashAlone() {
  QFETCH(QString, md5);
  QFETCH(QString, sha1);
  QFETCH(QString, crc);
  QFETCH(QString, key);
  QFETCH(QString, value);

  const auto result = HasheousLookupPayload::buildBody(hashes(md5, sha1, crc, 4096));
  QVERIFY(result.isOk());
  const QJsonObject obj = parse(result.value());
  // Absent hashes are omitted, not written as "" or null.
  QCOMPARE(obj.keys(), QStringList{key});
  QCOMPARE(obj.value(key).toString(), value);
  // Property names are case-sensitive: no lowercase or mixed-case variant.
  for (const QString &wrong :
       {QStringLiteral("md5"), QStringLiteral("sha1"), QStringLiteral("crc"), QStringLiteral("Md5"),
        QStringLiteral("Sha1"), QStringLiteral("Crc")})
    QVERIFY2(!obj.contains(wrong), qPrintable(wrong));
}

void TestHasheousLookupPayload::valuesArePreservedVerbatim() {
  // Uppercase, mixed case and surrounding whitespace pass through untouched:
  // the serializer neither normalizes nor validates the digests.
  const QString md5 = QStringLiteral("D41D8CD98F00B204E9800998ECF8427E");
  const QString sha1 = QStringLiteral("Da39A3ee5E6b4B0d3255BFEF95601890afd80709");
  const QString crc = QStringLiteral(" 1a2B3c4D ");
  const auto result = HasheousLookupPayload::buildBody(hashes(md5, sha1, crc));
  QVERIFY(result.isOk());
  const QJsonObject obj = parse(result.value());
  QCOMPARE(obj.value(QStringLiteral("MD5")).toString(), md5);
  QCOMPARE(obj.value(QStringLiteral("SHA1")).toString(), sha1);
  QCOMPARE(obj.value(QStringLiteral("CRC")).toString(), crc);
}

void TestHasheousLookupPayload::bodyIsCompactJson() {
  const auto result = HasheousLookupPayload::buildBody(hashes(kMd5, QString(), kCrc));
  QVERIFY(result.isOk());
  const QByteArray body = result.value();
  QCOMPARE(body, QByteArray(R"({"CRC":"00000000","MD5":"d41d8cd98f00b204e9800998ecf8427e"})"));
  QVERIFY(!body.contains('\n'));
  QVERIFY(!body.contains(' '));
}

void TestHasheousLookupPayload::sizeNeverLeaks() {
  const auto result = HasheousLookupPayload::buildBody(hashes(kMd5, kSha1, kCrc, 987654321));
  QVERIFY(result.isOk());
  const QByteArray body = result.value();
  QVERIFY(!body.contains("987654321"));
  QVERIFY(!body.toLower().contains("size"));
}

void TestHasheousLookupPayload::emptyInputIsRejected() {
  const auto result = HasheousLookupPayload::buildBody(RomHasher::Result{});
  QVERIFY(result.isError());
  QVERIFY(result.hasErrorCode(ErrorCode::InvalidArgument));
}

void TestHasheousLookupPayload::sizeOnlyInputIsRejected() {
  const auto result =
      HasheousLookupPayload::buildBody(hashes(QString(), QString(), QString(), 1048576));
  QVERIFY(result.isError());
  QVERIFY(result.hasErrorCode(ErrorCode::InvalidArgument));
}

QTEST_MAIN(TestHasheousLookupPayload)
#include "test_hasheouslookuppayload.moc"
