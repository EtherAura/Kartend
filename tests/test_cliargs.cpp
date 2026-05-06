#include <QCoreApplication>
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
  const auto opts = CliArgs::parseStartupArguments(
      {QStringLiteral("kartend"), QStringLiteral("--bogus"), QStringLiteral("--collection"),
       QStringLiteral("DOS")});
  QCOMPARE(opts.collectionOverride, QStringLiteral("DOS"));
}

void TestCliArgs::emptyValue_returnsEmptyOverride() {
  // --collection= with empty value should not override the persisted setting.
  const auto opts = CliArgs::parseStartupArguments(
      {QStringLiteral("kartend"), QStringLiteral("--collection=")});
  QVERIFY(opts.collectionOverride.isEmpty());
}

void TestCliArgs::importKart_capturesPathAndDest() {
  const auto opts = CliArgs::parseStartupArguments(
      {QStringLiteral("kartend"), QStringLiteral("--import-kart"), QStringLiteral("/tmp/x.kart"),
       QStringLiteral("--to"), QStringLiteral("/home/user/karts")});
  QCOMPARE(opts.importKartPath, QStringLiteral("/tmp/x.kart"));
  QCOMPARE(opts.importDestDir, QStringLiteral("/home/user/karts"));
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
  QCOMPARE(static_cast<int>(opts.onConflict),
           static_cast<int>(CliArgs::KartConflictPolicy::Merge));
}

void TestCliArgs::onConflict_unknownDefaultsToSkip() {
  const auto opts = CliArgs::parseStartupArguments(
      {QStringLiteral("kartend"), QStringLiteral("--on-conflict=garbage")});
  QCOMPARE(static_cast<int>(opts.onConflict), static_cast<int>(CliArgs::KartConflictPolicy::Skip));
}

QTEST_MAIN(TestCliArgs)
#include "test_cliargs.moc"
