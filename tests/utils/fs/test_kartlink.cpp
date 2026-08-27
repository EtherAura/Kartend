// KartLink stub read/write round-trip. Pure file <-> struct, no DB or event
// loop; every case stages its files in a QTemporaryDir.
#include <QFile>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include "kartlink.h"
#include "pathutils.h"

class TestKartLink : public QObject {
  Q_OBJECT

private slots:
  void extensionDetection();
  void writeReadRoundTrip();
  void readRejectsMissingFile();
  void readRejectsMalformedJson();
  void readRejectsEmptyTarget();
  void writeCreatesParentDirectory();
  void argsRoundTripAndStayOptional();
  void readRejectsAnImplausiblyLargeStubWithoutReadingIt();

private:
  QTemporaryDir m_dir;

  [[nodiscard]] QString path(const QString &name) const { return m_dir.filePath(name); }

  static void writeRaw(const QString &filePath, const QByteArray &data) {
    QVERIFY(PathUtils::atomicWriteFile(filePath, data));
  }
};

void TestKartLink::extensionDetection() {
  QVERIFY(KartLink::isKartLinkPath(QStringLiteral("/x/Half-Life 2.kartlink")));
  QVERIFY(KartLink::isKartLinkPath(QStringLiteral("/x/UPPER.KARTLINK")));
  QVERIFY(!KartLink::isKartLinkPath(QStringLiteral("/x/movie.mkv")));
  QVERIFY(!KartLink::isKartLinkPath(QStringLiteral("/x/kartlink"))); // no suffix
  QVERIFY(!KartLink::isKartLinkPath(QStringLiteral("/x/a.kartlink.bak")));
}

void TestKartLink::writeReadRoundTrip() {
  KartLink::LinkData data;
  data.source = QStringLiteral("steam");
  data.target = QStringLiteral("steam://rungameid/220");
  data.title = QStringLiteral("Half-Life 2");

  const QString stub = path(QStringLiteral("Half-Life 2.kartlink"));
  QVERIFY(KartLink::write(stub, data));

  const auto loaded = KartLink::read(stub);
  QVERIFY(!loaded.isError());
  QVERIFY(loaded.value() == data);
  QCOMPARE(loaded.value().version, 1);
}

void TestKartLink::readRejectsMissingFile() {
  const auto result = KartLink::read(path(QStringLiteral("nope.kartlink")));
  QVERIFY(result.isError());
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::FileReadError);
}

void TestKartLink::readRejectsMalformedJson() {
  const QString stub = path(QStringLiteral("broken.kartlink"));
  writeRaw(stub, QByteArrayLiteral("{not json"));
  const auto result = KartLink::read(stub);
  QVERIFY(result.isError());
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::InvalidConfigValue);
}

void TestKartLink::readRejectsEmptyTarget() {
  const QString stub = path(QStringLiteral("empty-target.kartlink"));
  writeRaw(stub, QByteArrayLiteral(R"({"version":1,"source":"steam","target":"  "})"));
  const auto result = KartLink::read(stub);
  QVERIFY(result.isError());
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::InvalidConfigValue);
}

void TestKartLink::writeCreatesParentDirectory() {
  KartLink::LinkData data;
  data.source = QStringLiteral("flatpak");
  data.target = QStringLiteral("org.example.Game");

  const QString stub = path(QStringLiteral("nested/dir/Game.kartlink"));
  QVERIFY(KartLink::write(stub, data));
  const auto loaded = KartLink::read(stub);
  QVERIFY(!loaded.isError());
  QCOMPARE(loaded.value().target, QStringLiteral("org.example.Game"));
}

// Kartend-4cff2: `args` is optional, and its absence has to keep meaning
// exactly what it meant before the field existed — every pre-existing stub on
// every user's disk is missing it.
void TestKartLink::argsRoundTripAndStayOptional() {
  KartLink::LinkData data;
  data.source = QStringLiteral("bottles");
  data.target = QStringLiteral("The Game");
  data.title = QStringLiteral("The Game");
  data.args = {QStringLiteral("-b"), QStringLiteral("My Bottle"), QStringLiteral("--")};

  const QString stub = path(QStringLiteral("The Game.kartlink"));
  QVERIFY(KartLink::write(stub, data));
  const auto loaded = KartLink::read(stub);
  QVERIFY(!loaded.isError());
  QVERIFY(loaded.value() == data);
  QCOMPARE(loaded.value().args.size(), 3);

  // An args-less stub must not gain the key — a re-sync compares the parsed
  // struct, so writing `"args":[]` would rewrite every existing stub once.
  KartLink::LinkData plain;
  plain.source = QStringLiteral("steam");
  plain.target = QStringLiteral("steam://rungameid/220");
  plain.title = QStringLiteral("Half-Life 2");
  const QString plainStub = path(QStringLiteral("Half-Life 2 plain.kartlink"));
  QVERIFY(KartLink::write(plainStub, plain));
  QFile written(plainStub);
  QVERIFY(written.open(QIODevice::ReadOnly));
  QVERIFY(!written.readAll().contains("args"));

  // A stub whose args are the wrong type loses the malformed entries rather
  // than stringifying them into the argv.
  const QString odd = path(QStringLiteral("odd.kartlink"));
  writeRaw(odd, QByteArrayLiteral(R"({"version":1,"source":"bottles","target":"X",)"
                                  R"("args":["-b",7,null,"Bottle"]})"));
  const auto oddLoaded = KartLink::read(odd);
  QVERIFY(!oddLoaded.isError());
  QCOMPARE(oddLoaded.value().args, (QStringList{QStringLiteral("-b"), QStringLiteral("Bottle")}));
}

void TestKartLink::readRejectsAnImplausiblyLargeStubWithoutReadingIt() {
  // read() is reached by SCANNING a media directory, so the file is whatever
  // happened to be sitting there wearing a .kartlink suffix — not necessarily
  // anything an importer wrote. Without a size check that was an unbounded
  // readAll() into memory before QJsonDocument saw a byte (Kartend-1o1a1,
  // secondary finding).
  const QString oversized = path(QStringLiteral("huge.kartlink"));
  // Just past the cap, and deliberately VALID JSON: a size rejection must not
  // depend on the body being malformed. The padding rides in an unused member
  // so the document stays well-formed at any length.
  QByteArray body = QByteArray("{\"version\":1,\"source\":\"steam\","
                               "\"target\":\"steam://rungameid/220\",\"pad\":\"");
  body.append(QByteArray(KartLink::kMaxStubBytes, 'x'));
  body.append("\"}");
  QVERIFY(body.size() > KartLink::kMaxStubBytes);
  writeRaw(oversized, body);

  const auto result = KartLink::read(oversized);
  QVERIFY2(result.isError(), "An oversized stub must be refused, not parsed");
  QCOMPARE(result.error().code, ErrorUtils::ErrorCode::InvalidConfigValue);

  // The cap must not catch real stubs: a normal one is a few hundred bytes.
  KartLink::LinkData ordinary;
  ordinary.source = QStringLiteral("steam");
  ordinary.target = QStringLiteral("steam://rungameid/220");
  ordinary.title = QStringLiteral("A Documentary");
  const QString fine = path(QStringLiteral("ordinary.kartlink"));
  QVERIFY(KartLink::write(fine, ordinary));
  const auto ok = KartLink::read(fine);
  QVERIFY(!ok.isError());
}

QTEST_MAIN(TestKartLink)
#include "test_kartlink.moc"
