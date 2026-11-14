/**
 * @file test_chdheader.cpp
 * @brief Unit tests for the CHD fixed-header reader (Kartend-xu36n).
 *
 * Every fixture is a synthetic header written to a QTemporaryDir — no real
 * CHD, no chdman. Covers the v5 identity (combined SHA-1, not raw/parent),
 * delta headers, complete v3/v4 headers (conversion required, no identity),
 * malformed and truncated input of every supported version, and bounded
 * reading (the device position never passes the fixed header).
 */

#include "chdheader.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtEndian>
#include <QTest>

using DatAudit::ChdHeader;
using DatAudit::ChdHeaderStatus;

namespace {

constexpr int kPrefix = 16;

const QString kRawHex = QStringLiteral("101112131415161718191a1b1c1d1e1f20212223");
const QString kCombinedHex = QStringLiteral("404142434445464748494a4b4c4d4e4f50515253");
const QString kParentHex = QStringLiteral("808182838485868788898a8b8c8d8e8f90919293");

void putBe32(QByteArray &b, int at, quint32 v) {
  qToBigEndian<quint32>(v, b.data() + at);
}
void putBe64(QByteArray &b, int at, quint64 v) {
  qToBigEndian<quint64>(v, b.data() + at);
}

void putSha1(QByteArray &b, int at, int first) {
  for (int i = 0; i < 20; ++i) b[at + i] = char(first + i);
}

// Tag + declared length + version, then `size` bytes in total of a non-zero
// filler so legacy fixtures carry hash-looking bytes the reader must ignore.
QByteArray prefixed(quint32 version, quint32 declaredLength, int size) {
  QByteArray b(qMax(size, kPrefix), char(0xA5));
  b.replace(0, 8, QByteArrayLiteral("MComprHD"));
  putBe32(b, 8, declaredLength);
  putBe32(b, 12, version);
  return b.left(size);
}

QByteArray v5Header(bool withParent) {
  QByteArray b = prefixed(5, DatAudit::kChdV5HeaderLength, DatAudit::kChdV5HeaderLength);
  putBe64(b, 32, Q_UINT64_C(0x123456789));
  // Map / metadata offsets point far past the fixture: the reader must not
  // follow them.
  putBe64(b, 40, Q_UINT64_C(0x7000000000));
  putBe64(b, 48, Q_UINT64_C(0x7100000000));
  putBe32(b, 56, 19584);
  putBe32(b, 60, 2448);
  putSha1(b, 64, 0x10);
  putSha1(b, 84, 0x40);
  if (withParent)
    putSha1(b, 104, 0x80);
  else
    b.replace(104, 20, QByteArray(20, '\0'));
  return b;
}

// The classification of the rejected first attempt: decide from the 16-byte
// prefix alone. Kept here only to show the legacy malformed fixtures really
// exercise the difference between prefix-only and complete-header checks.
ChdHeaderStatus rejectedPrefixOnlyStatus(const QByteArray &b) {
  if (b.size() < kPrefix || !b.startsWith("MComprHD")) return ChdHeaderStatus::Malformed;
  const quint32 version = qFromBigEndian<quint32>(b.constData() + 12);
  return (version == 3 || version == 4) ? ChdHeaderStatus::NeedsConversion
                                        : ChdHeaderStatus::Malformed;
}

quint32 legacyLength(quint32 version) {
  return version == 3 ? DatAudit::kChdV3HeaderLength : DatAudit::kChdV4HeaderLength;
}

void expectNoIdentity(const ChdHeader &h) {
  QVERIFY(h.combinedSha1.isEmpty());
  QVERIFY(h.rawSha1.isEmpty());
  QVERIFY(h.parentSha1.isEmpty());
  QVERIFY(!h.hasParent());
}

} // namespace

class TestChdHeader : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void v5HeaderOnlyFileReadsCombinedIdentity();
  void v5DeltaKeepsCombinedIdentity();
  void v5ReadingIsBoundedToHeader();
  void legacyCompleteHeaderNeedsConversion_data();
  void legacyCompleteHeaderNeedsConversion();
  void legacyPrefixOnlyIsMalformed_data();
  void legacyPrefixOnlyIsMalformed();
  void legacyTruncatedHeaderIsMalformed_data();
  void legacyTruncatedHeaderIsMalformed();
  void legacyBadLengthIsMalformed_data();
  void legacyBadLengthIsMalformed();
  void wrongMagicIsMalformed();
  void v5BadLengthIsMalformed_data();
  void v5BadLengthIsMalformed();
  void v5TruncatedIsMalformed_data();
  void v5TruncatedIsMalformed();
  void unsupportedVersionIsMalformed_data();
  void unsupportedVersionIsMalformed();
  void unreadableInput();

private:
  QString writeFixture(const QByteArray &bytes);
  QTemporaryDir m_dir;
  int m_caseId = 0;
};

void TestChdHeader::initTestCase() {
  QVERIFY(m_dir.isValid());
}

QString TestChdHeader::writeFixture(const QByteArray &bytes) {
  const QString path = m_dir.filePath(QStringLiteral("disk%1.chd").arg(++m_caseId));
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size()) return {};
  return path;
}

void TestChdHeader::v5HeaderOnlyFileReadsCombinedIdentity() {
  const QString path = writeFixture(v5Header(false));
  QVERIFY(!path.isEmpty());
  QCOMPARE(QFileInfo(path).size(), qint64(124));

  const ChdHeader h = DatAudit::readChdHeader(path);
  QCOMPARE(h.status, ChdHeaderStatus::Ok);
  QCOMPARE(h.version, 5u);
  QCOMPARE(h.headerLength, 124u);
  QCOMPARE(h.combinedSha1, kCombinedHex);
  QCOMPARE(h.rawSha1, kRawHex);
  QVERIFY(h.combinedSha1 != h.rawSha1);
  QVERIFY(h.parentSha1.isEmpty());
  QVERIFY(!h.hasParent());
  QCOMPARE(h.logicalBytes, Q_UINT64_C(0x123456789));
  QCOMPARE(h.hunkBytes, 19584u);
  QCOMPARE(h.unitBytes, 2448u);
}

void TestChdHeader::v5DeltaKeepsCombinedIdentity() {
  const QString path = writeFixture(v5Header(true));
  QVERIFY(!path.isEmpty());

  const ChdHeader h = DatAudit::readChdHeader(path);
  QCOMPARE(h.status, ChdHeaderStatus::Ok);
  QVERIFY(h.hasParent());
  QCOMPARE(h.parentSha1, kParentHex);
  // A non-zero parent does not change the identity.
  QCOMPARE(h.combinedSha1, kCombinedHex);
  QVERIFY(h.combinedSha1 != h.parentSha1);
  QVERIFY(h.combinedSha1 != h.rawSha1);
}

void TestChdHeader::v5ReadingIsBoundedToHeader() {
  const QByteArray payload(256 * 1024, char(0x5A));
  const QString path = writeFixture(v5Header(false) + payload);
  QVERIFY(!path.isEmpty());

  QFile f(path);
  QVERIFY(f.open(QIODevice::ReadOnly));
  const ChdHeader h = DatAudit::readChdHeader(f);
  QCOMPARE(h.status, ChdHeaderStatus::Ok);
  QCOMPARE(h.combinedSha1, kCombinedHex);
  QCOMPARE(f.pos(), qint64(124));
  // The appended payload is still unread.
  QCOMPARE(f.read(4), QByteArray(4, char(0x5A)));

  QByteArray bytes = v5Header(true) + payload;
  QBuffer buf(&bytes);
  QVERIFY(buf.open(QIODevice::ReadOnly));
  QCOMPARE(DatAudit::readChdHeader(buf).status, ChdHeaderStatus::Ok);
  QCOMPARE(buf.pos(), qint64(124));
}

void TestChdHeader::legacyCompleteHeaderNeedsConversion_data() {
  QTest::addColumn<quint32>("version");
  QTest::addColumn<int>("payload");
  QTest::newRow("v3 header only") << 3u << 0;
  QTest::newRow("v3 with payload") << 3u << 4096;
  QTest::newRow("v4 header only") << 4u << 0;
  QTest::newRow("v4 with payload") << 4u << 4096;
}

void TestChdHeader::legacyCompleteHeaderNeedsConversion() {
  QFETCH(quint32, version);
  QFETCH(int, payload);
  const quint32 length = legacyLength(version);
  const QByteArray bytes = prefixed(version, length, int(length)) + QByteArray(payload, 'P');
  const QString path = writeFixture(bytes);
  QVERIFY(!path.isEmpty());

  QFile f(path);
  QVERIFY(f.open(QIODevice::ReadOnly));
  const ChdHeader h = DatAudit::readChdHeader(f);
  QCOMPARE(h.status, ChdHeaderStatus::NeedsConversion);
  QCOMPARE(h.version, version);
  QCOMPARE(h.headerLength, length);
  expectNoIdentity(h);
  // The whole fixed header was read, and nothing after it.
  QCOMPARE(f.pos(), qint64(length));
}

void TestChdHeader::legacyPrefixOnlyIsMalformed_data() {
  QTest::addColumn<quint32>("version");
  QTest::addColumn<quint32>("declaredLength");
  QTest::newRow("v3 correct length") << 3u << DatAudit::kChdV3HeaderLength;
  QTest::newRow("v3 length 0") << 3u << 0u;
  QTest::newRow("v4 correct length") << 4u << DatAudit::kChdV4HeaderLength;
  QTest::newRow("v4 length 0") << 4u << 0u;
}

void TestChdHeader::legacyPrefixOnlyIsMalformed() {
  QFETCH(quint32, version);
  QFETCH(quint32, declaredLength);
  const QByteArray bytes = prefixed(version, declaredLength, kPrefix);
  QCOMPARE(bytes.size(), kPrefix);
  // The rejected prefix-only rule would have called this NeedsConversion.
  QCOMPARE(rejectedPrefixOnlyStatus(bytes), ChdHeaderStatus::NeedsConversion);

  const QString path = writeFixture(bytes);
  QVERIFY(!path.isEmpty());
  QFile f(path);
  QVERIFY(f.open(QIODevice::ReadOnly));
  const ChdHeader h = DatAudit::readChdHeader(f);
  QCOMPARE(h.status, ChdHeaderStatus::Malformed);
  QCOMPARE(h.version, version);
  expectNoIdentity(h);
  QCOMPARE(f.pos(), qint64(kPrefix));
}

void TestChdHeader::legacyTruncatedHeaderIsMalformed_data() {
  QTest::addColumn<quint32>("version");
  QTest::addColumn<int>("size");
  for (const quint32 v : {3u, 4u}) {
    const int length = int(legacyLength(v));
    for (const int size : {kPrefix + 1, length / 2, length - 1})
      QTest::addRow("v%u size %d of %d", v, size, length) << v << size;
  }
}

void TestChdHeader::legacyTruncatedHeaderIsMalformed() {
  QFETCH(quint32, version);
  QFETCH(int, size);
  const QByteArray bytes = prefixed(version, legacyLength(version), size);
  QCOMPARE(bytes.size(), size);
  QCOMPARE(rejectedPrefixOnlyStatus(bytes), ChdHeaderStatus::NeedsConversion);

  const QString path = writeFixture(bytes);
  QVERIFY(!path.isEmpty());
  QFile f(path);
  QVERIFY(f.open(QIODevice::ReadOnly));
  const ChdHeader h = DatAudit::readChdHeader(f);
  QCOMPARE(h.status, ChdHeaderStatus::Malformed);
  expectNoIdentity(h);
  QCOMPARE(f.pos(), qint64(size));
}

void TestChdHeader::legacyBadLengthIsMalformed_data() {
  QTest::addColumn<quint32>("version");
  QTest::addColumn<quint32>("declaredLength");
  for (const quint32 v : {3u, 4u}) {
    const quint32 length = legacyLength(v);
    // The other legacy version's length and the v5 length are also wrong.
    const quint32 other = legacyLength(v == 3 ? 4u : 3u);
    for (const quint32 bad : {0u, 16u, length - 1, length + 1, other, 124u, 0xFFFFFFFFu})
      QTest::addRow("v%u length %u", v, bad) << v << bad;
  }
}

void TestChdHeader::legacyBadLengthIsMalformed() {
  QFETCH(quint32, version);
  QFETCH(quint32, declaredLength);
  // Plenty of bytes follow, so only the declared length is at fault.
  const QByteArray bytes = prefixed(version, declaredLength, 124) + QByteArray(4096, 'P');
  QCOMPARE(rejectedPrefixOnlyStatus(bytes), ChdHeaderStatus::NeedsConversion);

  const QString path = writeFixture(bytes);
  QVERIFY(!path.isEmpty());
  QFile f(path);
  QVERIFY(f.open(QIODevice::ReadOnly));
  const ChdHeader h = DatAudit::readChdHeader(f);
  QCOMPARE(h.status, ChdHeaderStatus::Malformed);
  QCOMPARE(h.headerLength, declaredLength);
  expectNoIdentity(h);
  // A bad length is never trusted: nothing beyond the prefix is read.
  QCOMPARE(f.pos(), qint64(kPrefix));
}

void TestChdHeader::wrongMagicIsMalformed() {
  QByteArray bytes = v5Header(false);
  bytes.replace(0, 8, QByteArrayLiteral("MComprHX"));
  const QString path = writeFixture(bytes);
  QVERIFY(!path.isEmpty());
  const ChdHeader h = DatAudit::readChdHeader(path);
  QCOMPARE(h.status, ChdHeaderStatus::Malformed);
  expectNoIdentity(h);
}

void TestChdHeader::v5BadLengthIsMalformed_data() {
  QTest::addColumn<quint32>("declaredLength");
  for (const quint32 bad : {0u, 16u, 108u, 120u, 123u, 125u, 0xFFFFFFFFu})
    QTest::addRow("length %u", bad) << bad;
}

void TestChdHeader::v5BadLengthIsMalformed() {
  QFETCH(quint32, declaredLength);
  QByteArray bytes = v5Header(false) + QByteArray(4096, 'P');
  putBe32(bytes, 8, declaredLength);
  const QString path = writeFixture(bytes);
  QVERIFY(!path.isEmpty());
  QFile f(path);
  QVERIFY(f.open(QIODevice::ReadOnly));
  const ChdHeader h = DatAudit::readChdHeader(f);
  QCOMPARE(h.status, ChdHeaderStatus::Malformed);
  expectNoIdentity(h);
  QCOMPARE(f.pos(), qint64(kPrefix));
}

void TestChdHeader::v5TruncatedIsMalformed_data() {
  QTest::addColumn<int>("size");
  for (const int size : {0, 5, 8, 12, 15, 16, 70, 100, 123}) QTest::addRow("size %d", size) << size;
}

void TestChdHeader::v5TruncatedIsMalformed() {
  QFETCH(int, size);
  const QString path = writeFixture(v5Header(false).left(size));
  QVERIFY(!path.isEmpty());
  const ChdHeader h = DatAudit::readChdHeader(path);
  QCOMPARE(h.status, ChdHeaderStatus::Malformed);
  expectNoIdentity(h);
}

void TestChdHeader::unsupportedVersionIsMalformed_data() {
  QTest::addColumn<quint32>("version");
  for (const quint32 v : {0u, 1u, 2u, 6u}) QTest::addRow("v%u", v) << v;
}

void TestChdHeader::unsupportedVersionIsMalformed() {
  QFETCH(quint32, version);
  QByteArray bytes = v5Header(false);
  putBe32(bytes, 12, version);
  const QString path = writeFixture(bytes);
  QVERIFY(!path.isEmpty());
  const ChdHeader h = DatAudit::readChdHeader(path);
  QCOMPARE(h.status, ChdHeaderStatus::Malformed);
  QCOMPARE(h.version, version);
  expectNoIdentity(h);
}

void TestChdHeader::unreadableInput() {
  const ChdHeader missing =
      DatAudit::readChdHeader(m_dir.filePath(QStringLiteral("does-not-exist.chd")));
  QCOMPARE(missing.status, ChdHeaderStatus::Unreadable);
  expectNoIdentity(missing);

  QByteArray bytes = v5Header(false);
  QBuffer closed(&bytes);
  const ChdHeader h = DatAudit::readChdHeader(closed);
  QCOMPARE(h.status, ChdHeaderStatus::Unreadable);
  expectNoIdentity(h);
}

QTEST_MAIN(TestChdHeader)
#include "test_chdheader.moc"
