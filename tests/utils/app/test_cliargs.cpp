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
  void importKart_rejectsTraversal();
  void to_rejectsShellMetachars();
  void exportKart_capturesNameAndOut();
  void exportOut_rejectsShellMetachars();
  void exportOut_expandsTilde();
  void onConflict_overwriteRecognized();
  void onConflict_mergeRecognized();
  void onConflict_unknownIsRejected();
  void allowUntrustedLauncher_defaultsOff();
  void allowUntrustedLauncher_setByFlag();
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

void TestCliArgs::importKart_rejectsTraversal() {
  // The CLI seam relies solely on validatePathSecurity, which now rejects a
  // `..` traversal segment (Kartend-w13c) — `--import-kart ../../etc/foo` used
  // to slip through the command-injection-only checks.
  const auto opts =
      CliArgs::parseStartupArguments({QStringLiteral("kartend"), QStringLiteral("--import-kart"),
                                      QStringLiteral("../../etc/foo")});
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

void TestCliArgs::exportOut_rejectsShellMetachars() {
  // Kartend-928mu: --export-out now goes through the same sanitizer as the
  // import paths, so a shell-metachar payload is rejected at the CLI seam
  // instead of reaching QSaveFile raw.
  const auto opts = CliArgs::parseStartupArguments(
      {QStringLiteral("kartend"), QStringLiteral("--export-kart"), QStringLiteral("Genesis"),
       QStringLiteral("--export-out"), QStringLiteral("/tmp/out.kart; rm -rf /")});
  QVERIFY(opts.pathValidationError.isError());
  QCOMPARE(opts.pathValidationError.code, ErrorUtils::ErrorCode::InvalidFilePath);
}

void TestCliArgs::exportOut_expandsTilde() {
  // Kartend-928mu: a ~-prefixed export path now expands to an absolute path at
  // the seam (it used to be stored verbatim, creating a literal '~' directory).
  const auto opts = CliArgs::parseStartupArguments(
      {QStringLiteral("kartend"), QStringLiteral("--export-kart"), QStringLiteral("Genesis"),
       QStringLiteral("--export-out"), QStringLiteral("~/exports/genesis.kart")});
  QVERIFY(!opts.pathValidationError.isError());
  QVERIFY(!opts.exportOutPath.startsWith('~'));
  QVERIFY(QFileInfo(opts.exportOutPath).isAbsolute());
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

void TestCliArgs::onConflict_unknownIsRejected() {
  // Kartend audit E-04: an unrecognized --on-conflict value must surface a
  // CLI-validation error (not silently fall back to Skip).
  const auto opts = CliArgs::parseStartupArguments(
      {QStringLiteral("kartend"), QStringLiteral("--on-conflict=garbage")});
  QVERIFY(opts.pathValidationError.isError());
  QCOMPARE(opts.pathValidationError.code, ErrorUtils::ErrorCode::InvalidArgument);
  // onConflict is left at its safe default; the error is what fails the run.
  QCOMPARE(static_cast<int>(opts.onConflict), static_cast<int>(CliArgs::KartConflictPolicy::Skip));
}

void TestCliArgs::allowUntrustedLauncher_defaultsOff() {
  // Kartend-l8vt8 / Kartend-u8wf0. This flag gates whether a headless
  // --import-kart may register a launcher path pointing INSIDE the extracted
  // kart tree, i.e. run an executable the archive shipped. Default-off is the
  // security property, so assert it on a command line that carries an import
  // but not the flag — not on an empty one, where a default-constructed
  // struct would pass without the parser doing anything.
  const auto opts = CliArgs::parseStartupArguments(
      {QStringLiteral("kartend"), QStringLiteral("--import-kart=/tmp/x.kart")});
  QVERIFY(!opts.allowUntrustedLauncher);
}

void TestCliArgs::allowUntrustedLauncher_setByFlag() {
  // The option exists only in main.cpp until Kartend-l8vt8 mirrored it here,
  // so this is the first coverage it has had. If the shim ever stops
  // registering the option, parse() treats it as unknown and isSet() returns
  // false — this fails rather than silently losing the flag.
  const auto opts = CliArgs::parseStartupArguments({QStringLiteral("kartend"),
                                                    QStringLiteral("--import-kart=/tmp/x.kart"),
                                                    QStringLiteral("--allow-untrusted-launcher")});
  QVERIFY(opts.allowUntrustedLauncher);
  // The flag must not disturb the rest of the parse.
  QCOMPARE(opts.importKartPath, QStringLiteral("/tmp/x.kart"));
  QVERIFY(!opts.pathValidationError.isError());
}

QTEST_MAIN(TestCliArgs)
#include "test_cliargs.moc"
