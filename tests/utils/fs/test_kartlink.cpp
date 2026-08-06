// KartLink stub read/write round-trip. Pure file <-> struct, no DB or event
// loop; every case stages its files in a QTemporaryDir.
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

QTEST_MAIN(TestKartLink)
#include "test_kartlink.moc"
