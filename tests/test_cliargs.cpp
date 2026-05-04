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

QTEST_MAIN(TestCliArgs)
#include "test_cliargs.moc"
