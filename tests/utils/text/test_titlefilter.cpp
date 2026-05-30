// Unit tests for TitleFilter.
//
// TitleFilter is the per-collection title-cleanup engine consumed by the
// DatabaseManager interception path. These tests exercise the public surface:
// rebuildFromCollections() / apply() / clearForTests().
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>

#include "collection/collectionconfig.h"
#include "titlefilter.h"

class TestTitleFilter : public QObject {
  Q_OBJECT
private slots:
  void init() { TitleFilter::clearForTests(); }

  void emptyRegistryReturnsInputUnchanged();
  void disabledCollectionSkipsPatterns();
  void singlePatternStripsRegionTag();
  void multiplePatternsAppliedInOrder();
  void invalidPatternIsSkipped();
  void emptyPatternListIgnored();
  void unknownCollectionIndexFallsThrough();
  void leadingTrailingWhitespaceCollapsed();
  void rebuildReplacesPriorPatterns();

  // Direct-API coverage for the extracted compilePatterns helper. The
  // registry-side tests above exercise it indirectly via rebuildFromCollections,
  // but the failure modes (invalid regex, whitespace-only entry) are
  // easier to isolate when the helper is called directly.
  void compilePatterns_emptyInputReturnsEmpty();
  void compilePatterns_validRegexCompiles();
  void compilePatterns_invalidRegexSkippedNotCounted();
  void compilePatterns_whitespaceOnlyEntrySkipped();
  void compilePatterns_validAndInvalidMixedKeepsOnlyValid();
};

namespace {

CollectionConfig makeConfig(const QString &name, const QStringList &patterns, bool enabled = true) {
  CollectionConfig c;
  c.name = name;
  c.filter.titleExclusionPatterns = patterns;
  c.filter.titleExclusionEnabled = enabled;
  return c;
}

} // namespace

void TestTitleFilter::emptyRegistryReturnsInputUnchanged() {
  // Sanity: no rebuild called → registry empty → no-op.
  QCOMPARE(TitleFilter::apply(0, QStringLiteral("Mario (USA)")), QStringLiteral("Mario (USA)"));
}

void TestTitleFilter::disabledCollectionSkipsPatterns() {
  const QList<CollectionConfig> collections{
      makeConfig("NES", {QStringLiteral("\\s*\\(USA\\)$")}, /*enabled=*/false)};
  TitleFilter::rebuildFromCollections(collections);
  QCOMPARE(TitleFilter::apply(0, QStringLiteral("Mario (USA)")), QStringLiteral("Mario (USA)"));
}

void TestTitleFilter::singlePatternStripsRegionTag() {
  const QList<CollectionConfig> collections{makeConfig("NES", {QStringLiteral("\\s*\\(USA\\)$")})};
  TitleFilter::rebuildFromCollections(collections);
  QCOMPARE(TitleFilter::apply(0, QStringLiteral("Super Mario Bros (USA)")),
           QStringLiteral("Super Mario Bros"));
  // Pattern doesn't match — input survives intact.
  QCOMPARE(TitleFilter::apply(0, QStringLiteral("Tetris")), QStringLiteral("Tetris"));
}

void TestTitleFilter::multiplePatternsAppliedInOrder() {
  const QList<CollectionConfig> collections{
      makeConfig("NES", {QStringLiteral("\\s*\\([A-Z]+\\)"), QStringLiteral("\\s*\\[!\\]"),
                         QStringLiteral("\\s*\\(Rev \\d+\\)")})};
  TitleFilter::rebuildFromCollections(collections);
  QCOMPARE(TitleFilter::apply(0, QStringLiteral("Mario (USA) [!] (Rev 1)")),
           QStringLiteral("Mario"));
}

void TestTitleFilter::invalidPatternIsSkipped() {
  // The unmatched paren below is invalid regex. The valid pattern that
  // follows must still apply — a single typo can't disable the whole list.
  const QList<CollectionConfig> collections{
      makeConfig("NES", {QStringLiteral("("), QStringLiteral("\\s*\\(USA\\)$")})};
  TitleFilter::rebuildFromCollections(collections);
  QCOMPARE(TitleFilter::apply(0, QStringLiteral("Mario (USA)")), QStringLiteral("Mario"));
}

void TestTitleFilter::emptyPatternListIgnored() {
  // Empty list means the collection isn't registered → fall-through.
  const QList<CollectionConfig> collections{makeConfig("NES", {})};
  TitleFilter::rebuildFromCollections(collections);
  QCOMPARE(TitleFilter::apply(0, QStringLiteral("Mario (USA)")), QStringLiteral("Mario (USA)"));
}

void TestTitleFilter::unknownCollectionIndexFallsThrough() {
  const QList<CollectionConfig> collections{makeConfig("NES", {QStringLiteral("\\s*\\(USA\\)$")})};
  TitleFilter::rebuildFromCollections(collections);
  // Index 1 isn't registered.
  QCOMPARE(TitleFilter::apply(1, QStringLiteral("Mario (USA)")), QStringLiteral("Mario (USA)"));
  // Negative index is the documented "no collection" sentinel.
  QCOMPARE(TitleFilter::apply(-1, QStringLiteral("Mario (USA)")), QStringLiteral("Mario (USA)"));
}

void TestTitleFilter::leadingTrailingWhitespaceCollapsed() {
  // The patterns leave whitespace artefacts; simplified() in apply() must
  // tidy them so the visible title doesn't end mid-space.
  const QList<CollectionConfig> collections{
      makeConfig("NES", {QStringLiteral("\\(USA\\)"), QStringLiteral("\\[!\\]")})};
  TitleFilter::rebuildFromCollections(collections);
  QCOMPARE(TitleFilter::apply(0, QStringLiteral("  Mario   (USA)  [!]  ")),
           QStringLiteral("Mario"));
}

void TestTitleFilter::rebuildReplacesPriorPatterns() {
  // First rebuild: NES at index 0 strips (USA).
  TitleFilter::rebuildFromCollections({makeConfig("NES", {QStringLiteral("\\s*\\(USA\\)$")})});
  QCOMPARE(TitleFilter::apply(0, QStringLiteral("Mario (USA)")), QStringLiteral("Mario"));

  // Second rebuild for the same index swaps the rules — old pattern must
  // not linger or stack on top of the new one.
  TitleFilter::rebuildFromCollections({makeConfig("NES", {QStringLiteral("\\s*\\(JPN\\)$")})});
  QCOMPARE(TitleFilter::apply(0, QStringLiteral("Mario (USA)")), QStringLiteral("Mario (USA)"));
  QCOMPARE(TitleFilter::apply(0, QStringLiteral("Mario (JPN)")), QStringLiteral("Mario"));
}

void TestTitleFilter::compilePatterns_emptyInputReturnsEmpty() {
  const auto compiled = TitleFilter::compilePatterns({});
  QCOMPARE(compiled.size(), 0);
}

void TestTitleFilter::compilePatterns_validRegexCompiles() {
  const auto compiled = TitleFilter::compilePatterns({QStringLiteral("\\s*\\(USA\\)$")});
  QCOMPARE(compiled.size(), 1);
  QVERIFY(compiled.first().isValid());
}

void TestTitleFilter::compilePatterns_invalidRegexSkippedNotCounted() {
  // Unclosed group is invalid; the helper must drop it (logged via
  // ErrorUtils, suppressed in QTest output) instead of crashing or
  // including an invalid QRegularExpression in the returned list.
  const auto compiled =
      TitleFilter::compilePatterns({QStringLiteral("(unclosed")}, QStringLiteral("test-context"));
  QCOMPARE(compiled.size(), 0);
}

void TestTitleFilter::compilePatterns_whitespaceOnlyEntrySkipped() {
  // Trimmed-empty entries don't count as patterns — they're a common
  // hand-edit artifact in the toolbar popup.
  const auto compiled =
      TitleFilter::compilePatterns({QStringLiteral("   "), QString(), QStringLiteral("\t\n")});
  QCOMPARE(compiled.size(), 0);
}

void TestTitleFilter::compilePatterns_validAndInvalidMixedKeepsOnlyValid() {
  const auto compiled =
      TitleFilter::compilePatterns({QStringLiteral("\\s*\\(USA\\)$"), QStringLiteral("(unclosed"),
                                    QStringLiteral("\\s*\\[!\\]$")},
                                   QStringLiteral("test-context"));
  QCOMPARE(compiled.size(), 2);
  for (const auto &re : compiled) {
    QVERIFY(re.isValid());
  }
}

QTEST_MAIN(TestTitleFilter)
#include "test_titlefilter.moc"
