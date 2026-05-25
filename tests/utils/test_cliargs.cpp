#include <QCoreApplication>
#include <QFileInfo>
#include <QStringList>
#include <QTest>

#include "cliargs.h"

class TestCliArgs : public QObject {
  Q_OBJECT
private slots:
  void noOptions_returnsEmptyOverride();
  void longForm_setsCollectionOverride();
  void shortForm_setsCollectionOverride();
  void equalsForm_setsCollectionOverride();
  void whitespace_isTrimmed();
  void unknownOption_doesNotAbort();
  void emptyValue_returnsEmptyOverride();
  void importKart_capturesPathAndDest();
  void importKart_expandsTilde();
  void importKart_rejectsShellMetachars();
  void importKart_rejectsNullByte();
  void to_rejectsShellMetachars();
  void exportKart_capturesNameAndOut();
  void onConflict_overwriteRecognized();
  void onConflict_mergeRecognized();
  void onConflict_unknownDefaultsToSkip();
};

void TestCliArgs::noOptions_returnsEmptyOverride() {
  const auto opts = CliArgs::parseStartupArguments({QStringLiteral("kartend")});
  QVERIFY(opts.collectionOverride.isEmpty());
}

void TestCliArgs::longForm_setsCollectionOverride() {
  const auto opts = CliArgs::parseStartupArguments(
      {QStringLiteral("kartend"), QStringLiteral("--collection"), QStringLiteral("Genesis")});
  QCOMPARE(opts.collectionOverride, QStringLiteral("Genesis"));
}

void TestCliArgs::shortForm_setsCollectionOverride() {
  const auto opts = CliArgs::parseStartupArguments(
      {QStringLiteral("kartend"), QStringLiteral("-c"), QStringLiteral("SNES")});
  QCOMPARE(opts.collectionOverride, QStringLiteral("SNES"));
}

void TestCliArgs::equalsForm_setsCollectionOverride() {
  const auto opts = CliArgs::parseStartupArguments(
      {QStringLiteral("kartend"), QStringLiteral("--collection=PS1")});
  QCOMPARE(opts.collectionOverride, QStringLiteral("PS1"));
}

void TestCliArgs::whitespace_isTrimmed() {
  const auto opts = CliArgs::parseStartupArguments(
      {QStringLiteral("kartend"), QStringLiteral("-c"), QStringLiteral("  N64  ")});
  QCOMPARE(opts.collectionOverride, QStringLiteral("N64"));
}

void TestCliArgs::unknownOption_doesNotAbort() {
  // parse() (vs process()) is non-fatal on unknown options. Verify the helper
  // tolerates them without crashing — main.cpp uses process() to surface the
  // standard error to users on the real CLI.
  const auto opts =
      CliArgs::parseStartupArguments({QStringLiteral("kartend"), QStringLiteral("--bogus"),
                                      QStringLiteral("--collection"), QStringLiteral("DOS")});
  QCOMPARE(opts.collectionOverride, QStringLiteral("DOS"));
}

void TestCliArgs::emptyValue_returnsEmptyOverride() {
  // --collection= with empty value should not override the persisted setting.
  const auto opts =
      CliArgs::parseStartupArguments({QStringLiteral("kartend"), QStringLiteral("--collection=")});
  QVERIFY(opts.collectionOverride.isEmpty());
}

void TestCliArgs::importKart_capturesPathAndDest() {
  const auto opts = CliArgs::parseStartupArguments(
      {QStringLiteral("kartend"), QStringLiteral("--import-kart"), QStringLiteral("/tmp/x.kart"),
       QStringLiteral("--to"), QStringLiteral("/home/user/karts")});
  QCOMPARE(opts.importKartPath, QStringLiteral("/tmp/x.kart"));
  QCOMPARE(opts.importDestDir, QStringLiteral("/home/user/karts"));
  QVERIFY(!opts.pathValidationError.isError());
}

void TestCliArgs::importKart_expandsTilde() {
  // Tilde expansion runs at the CLI seam (PathUtils::expandPathWithoutExistenceCheck)
  // so KartReader receives an absolute path — its own validation rejects
  // non-absolute paths and would fail without this seam doing the work.
  const auto opts = CliArgs::parseStartupArguments(
      {QStringLiteral("kartend"), QStringLiteral("--import-kart"),
       QStringLiteral("~/incoming/x.kart"), QStringLiteral("--to"), QStringLiteral("~/karts")});
  QVERIFY(!opts.pathValidationError.isError());
  QVERIFY(!opts.importKartPath.startsWith('~'));
  QVERIFY(!opts.importDestDir.startsWith('~'));
  // QFileInfo::isAbsolute is platform-aware: '/' on POSIX, drive-letter
  // ('C:/...') on Windows. The earlier '/' prefix check only worked on
  // POSIX because tilde expanded to /home/user/...; on Windows it
  // expands to C:\Users\user\... .
  QVERIFY(QFileInfo(opts.importKartPath).isAbsolute());
  QVERIFY(QFileInfo(opts.importDestDir).isAbsolute());
}

void TestCliArgs::importKart_rejectsShellMetachars() {
  // ; | ` $ < > are rejected by validatePathSecurity — these are the
  // primary command-injection vectors. The raw value is preserved in the
  // struct so the caller can include it in the error message.
  const auto opts =
      CliArgs::parseStartupArguments({QStringLiteral("kartend"), QStringLiteral("--import-kart"),
                                      QStringLiteral("/tmp/x.kart; rm -rf /")});
  QVERIFY(opts.pathValidationError.isError());
  QCOMPARE(opts.pathValidationError.code, ErrorUtils::ErrorCode::InvalidFilePath);
}

void TestCliArgs::importKart_rejectsNullByte() {
  // Null bytes truncate strings at the C-API boundary and can route the
  // path past validation checks — must be rejected at the seam.
  QString badPath = QStringLiteral("/tmp/x.kart");
  badPath.append(QChar('\0'));
  badPath.append(QStringLiteral("/etc/passwd"));
  const auto opts = CliArgs::parseStartupArguments(
      {QStringLiteral("kartend"), QStringLiteral("--import-kart"), badPath});
  QVERIFY(opts.pathValidationError.isError());
  QCOMPARE(opts.pathValidationError.code, ErrorUtils::ErrorCode::InvalidFilePath);
}

void TestCliArgs::to_rejectsShellMetachars() {
  // Same shell-metachar gate applies to the destination dir.
  const auto opts = CliArgs::parseStartupArguments(
      {QStringLiteral("kartend"), QStringLiteral("--import-kart"), QStringLiteral("/tmp/x.kart"),
       QStringLiteral("--to"), QStringLiteral("/tmp/dest`whoami`")});
  QVERIFY(opts.pathValidationError.isError());
  QCOMPARE(opts.pathValidationError.code, ErrorUtils::ErrorCode::InvalidFilePath);
}

void TestCliArgs::exportKart_capturesNameAndOut() {
  const auto opts = CliArgs::parseStartupArguments(
      {QStringLiteral("kartend"), QStringLiteral("--export-kart"), QStringLiteral("Genesis"),
       QStringLiteral("--export-out"), QStringLiteral("/tmp/genesis.kart")});
  QCOMPARE(opts.exportCollectionName, QStringLiteral("Genesis"));
  QCOMPARE(opts.exportOutPath, QStringLiteral("/tmp/genesis.kart"));
}

void TestCliArgs::onConflict_overwriteRecognized() {
  const auto opts = CliArgs::parseStartupArguments(
      {QStringLiteral("kartend"), QStringLiteral("--on-conflict"), QStringLiteral("overwrite")});
  QCOMPARE(static_cast<int>(opts.onConflict),
           static_cast<int>(CliArgs::KartConflictPolicy::Overwrite));
}

void TestCliArgs::onConflict_mergeRecognized() {
  const auto opts = CliArgs::parseStartupArguments(
      {QStringLiteral("kartend"), QStringLiteral("--on-conflict=merge")});
  QCOMPARE(static_cast<int>(opts.onConflict), static_cast<int>(CliArgs::KartConflictPolicy::Merge));
}

void TestCliArgs::onConflict_unknownDefaultsToSkip() {
  const auto opts = CliArgs::parseStartupArguments(
      {QStringLiteral("kartend"), QStringLiteral("--on-conflict=garbage")});
  QCOMPARE(static_cast<int>(opts.onConflict), static_cast<int>(CliArgs::KartConflictPolicy::Skip));
}

QTEST_MAIN(TestCliArgs)
#include "test_cliargs.moc"
