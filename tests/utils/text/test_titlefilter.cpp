// Unit tests for TitleFilter.
//
// TitleFilter is the per-collection title-cleanup engine consumed by the
// DatabaseManager interception path. These tests exercise the public surface:
// rebuildFromCollections() / apply() / clearForTests().
#include <atomic>

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QThread>

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

  // Concurrency regression: apply() copies the compiled patterns under the
  // read lock then DROPS it before the regex loop, so a concurrent
  // rebuildFromCollections() (write lock) is never blocked for a whole
  // per-item match. Hammer both paths from separate threads and assert no
  // deadlock + only-legitimate outputs. Guards the lock-scope narrowing that
  // a refactor re-widening the read lock wouldn't otherwise trip.
  void concurrentApplyAndRebuildIsRaceFree();

  void cleanup() { TitleFilter::clearForTests(); }
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

namespace {

// Worker that hammers apply() for collection index 0 in a tight loop until the
// shared deadline flag flips. The single input below has exactly two legitimate
// results: "Mario" when the (USA) pattern is registered + enabled, or the
// untouched input when the registry is momentarily empty / disabled between the
// writer's rebuilds. Anything else means apply() returned over a half-updated
// registry — the regression this test guards. All cross-thread bookkeeping is
// std::atomic so TSan only flags real SUT races, not the harness.
class ApplyHammer : public QThread {
public:
  ApplyHammer(const std::atomic<bool> &stop, std::atomic<bool> &sawBadOutput,
              std::atomic<quint64> &iterations)
      : m_stop(stop), m_sawBadOutput(sawBadOutput), m_iterations(iterations) {}

protected:
  void run() override {
    const QString input = QStringLiteral("Mario (USA)");
    const QString stripped = QStringLiteral("Mario");
    quint64 local = 0;
    while (!m_stop.load(std::memory_order_acquire)) {
      const QString out = TitleFilter::apply(0, input);
      // Both outcomes are valid; either the patterns applied (stripped) or the
      // registry was momentarily empty/disabled (input survives intact). No
      // other value can legitimately come back.
      if (out != stripped && out != input) {
        m_sawBadOutput.store(true, std::memory_order_release);
      }
      ++local;
    }
    m_iterations.fetch_add(local, std::memory_order_relaxed);
  }

private:
  const std::atomic<bool> &m_stop;
  std::atomic<bool> &m_sawBadOutput;
  std::atomic<quint64> &m_iterations;
};

// Worker that drives the write-lock path: it alternates the registry between a
// populated state (index 0 strips "(USA)") and an empty rebuild, so the readers
// race against both the QWriteLocker swap and the registryNonEmpty() fast-path
// flag flipping in both directions.
class RebuildHammer : public QThread {
public:
  RebuildHammer(const std::atomic<bool> &stop, std::atomic<quint64> &iterations)
      : m_stop(stop), m_iterations(iterations) {}

protected:
  void run() override {
    const QList<CollectionConfig> populated{makeConfig("NES", {QStringLiteral("\\s*\\(USA\\)$")})};
    const QList<CollectionConfig> empty;
    quint64 local = 0;
    while (!m_stop.load(std::memory_order_acquire)) {
      TitleFilter::rebuildFromCollections((local & 1u) ? empty : populated);
      ++local;
    }
    m_iterations.fetch_add(local, std::memory_order_relaxed);
  }

private:
  const std::atomic<bool> &m_stop;
  std::atomic<quint64> &m_iterations;
};

} // namespace

void TestTitleFilter::concurrentApplyAndRebuildIsRaceFree() {
#if defined(__SANITIZE_THREAD__) || (defined(__has_feature) && __has_feature(thread_sanitizer))
  // apply()/rebuildFromCollections() are correctly serialized by a single
  // QReadWriteLock (registryLock()), but QReadWriteLock's lock-free fast path
  // is futex/atomic-based — not a pthread_rwlock TSan intercepts — so TSan
  // cannot see the happens-before edge it establishes. Hammering the lock from
  // five threads here therefore produces a storm of QReadWriteLock false
  // positives (tryLockForRead/unlock on the lock's private, the guarded QHash
  // qt_ptr_swap, the contended pthread_cond_wait, and the QThread-join COW at
  // teardown). The actual offending frames are all stripped libQt6Core in
  // Ubuntu 24.04's qt6-base packages, so no race: pattern in
  // tests/suppressions/tsan.txt can target them without an over-broad intrinsic
  // (race:operator delete / race:memmove / race:pthread_cond_wait) that would
  // mask unrelated real races across the whole suite. The locking is correct by
  // construction and the rest of this file unit-covers apply()/rebuild() under
  // the regular build matrix, so skipping only the concurrency-stress slot under
  // TSan loses no invariant coverage. Same approach as
  // test_batchscraperunner_integration / test_httpclient under TSan.
  QSKIP("TitleFilter's QReadWriteLock fast path is invisible to TSan and the "
        "offending frames are stripped libQt6Core — un-suppressable without an "
        "over-broad intrinsic; the lock is correct and apply()/rebuild() are "
        "covered by the other slots under the non-TSan matrix");
#endif
  // Seed a populated registry so the first apply() calls take the locking path
  // (the registryNonEmpty() fast-path would otherwise short-circuit before the
  // read lock until the writer's first populated rebuild lands).
  TitleFilter::rebuildFromCollections({makeConfig("NES", {QStringLiteral("\\s*\\(USA\\)$")})});

  std::atomic<bool> stop{false};
  std::atomic<bool> sawBadOutput{false};
  std::atomic<quint64> readerIterations{0};
  std::atomic<quint64> writerIterations{0};

  // Several readers + one writer maximises the chance a reader is mid-match
  // while the writer swaps the registry under the write lock.
  constexpr int kReaderCount = 4;
  QList<ApplyHammer *> readers;
  readers.reserve(kReaderCount);
  for (int i = 0; i < kReaderCount; ++i) {
    readers.append(new ApplyHammer(stop, sawBadOutput, readerIterations));
  }
  RebuildHammer writer(stop, writerIterations);

  for (ApplyHammer *r : readers) {
    r->start();
  }
  writer.start();

  // Run for a short, bounded window rather than a fixed iteration count so the
  // test is stable across wildly different hardware (and slow under TSan).
  constexpr qint64 kRunMs = 750;
  QElapsedTimer runTimer;
  runTimer.start();
  while (runTimer.elapsed() < kRunMs) {
    QThread::msleep(10);
  }
  stop.store(true, std::memory_order_release);

  // Watchdog: join every worker with a generous per-thread budget. A real
  // deadlock (e.g. a refactor re-widening the read lock so a writer starves)
  // makes wait() time out → the QVERIFY fails loudly instead of the whole
  // ctest run hanging on an unbounded join.
  constexpr int kJoinTimeoutMs = 15000;
  bool joinedClean = true;
  for (ApplyHammer *r : readers) {
    joinedClean = r->wait(kJoinTimeoutMs) && joinedClean;
  }
  joinedClean = writer.wait(kJoinTimeoutMs) && joinedClean;
  QVERIFY2(joinedClean, "TitleFilter apply/rebuild worker failed to finish — possible deadlock");

  qDeleteAll(readers);

  QVERIFY2(!sawBadOutput.load(std::memory_order_acquire),
           "apply() returned a value that was neither the stripped title nor the "
           "untouched input — registry observed in a torn state");

  // Sanity: both paths actually executed (otherwise a no-op would "pass"
  // vacuously). These are loose lower bounds, not perf assertions.
  QVERIFY2(readerIterations.load(std::memory_order_acquire) > 0,
           "reader threads never ran apply()");
  QVERIFY2(writerIterations.load(std::memory_order_acquire) > 0,
           "writer thread never ran rebuildFromCollections()");

  // cleanup() resets the registry so this test leaks no compiled patterns into
  // the next slot.
}

QTEST_MAIN(TestTitleFilter)
#include "test_titlefilter.moc"
