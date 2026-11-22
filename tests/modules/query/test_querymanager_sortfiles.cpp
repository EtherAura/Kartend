// Regression tests for Kartend-m9r1s: sortFiles used to stat every path
// (QFileInfo exists() + lastModified()/size()) for the Date/Size sort modes
// even though the load pipeline already had the values in hand (scan-collected
// mtimes, items.last_modified / items.file_size). sortFiles now accepts
// optional metadata maps and only stats paths missing from them.
//
// The ordering CONTRACT must stay identical: numeric key first (mtime ms or
// byte size, ascending/descending), then characterSortPriority, then
// case-insensitive name compare — only the SOURCE of the numeric key changed.
//
//   dateSortUsesProvidedMetadata / sizeSortUsesProvidedMetadata
//     With maps covering every path, the order follows the map values — even
//     where they deliberately contradict the on-disk values, proving no
//     per-item stat happens when the data is in hand.
//
//   dateSortWithMapsMatchesStatPath / sizeSortWithMapsMatchesStatPath
//     Maps built from the real on-disk values produce EXACTLY the order the
//     historical stat-everything path (nullptr maps) produces.
//
//   missingKeysFallBackToStat
//     A path absent from the maps gets its real on-disk value; a nonexistent
//     path absent from the maps keeps the historical "sorts as 0" behavior.
//
//   nameSortIgnoresMetadata
//     Name modes never consult the maps.
//
//   buildSortMetadataKeysLikeSortFiles
//     Relative-keyed scan/DB metadata is re-keyed to the absolute (and, when
//     a canonical-path cache is supplied, canonicalized) form sortFiles sees.
//
// Name-order rules (Kartend-ek49m). Paths need not exist: name sort never
// stats, and the date tie-break case covers every path with the map. Every
// asserted list uses distinct normalized keys so no order between equal keys
// is imposed.
//
//   nameAscendingOrdersByPriorityBucket
//     Bracket/paren, then other punctuation, then digit, then letter — chosen
//     so a plain string compare of the keys would give a different order.
//
//   nameSortStripsLeadingApostrophe
//     "'98 Game" sorts as "98 Game" and "'Bout" as "Bout"; an apostrophe not
//     followed by a digit or letter stays punctuation.
//
//   nameDescendingIsExactReverse / nameSortIsCaseInsensitive
//   dateSortBreaksTiesByName
//     Equal mtimes fall back to the same name order (reversed descending).
//
//   sizeDescendingBreaksTiesByName (Kartend-cfdac)
//     Equal sizes, largest first, fall back to descending name order.

#include <algorithm>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include "querymanagerhelpers.h"

using QueryManagerInternal::buildSortMetadata;
using QueryManagerInternal::sortFiles;

class TestQueryManagerSortFiles : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void dateSortUsesProvidedMetadata();
  void sizeSortUsesProvidedMetadata();
  void dateSortWithMapsMatchesStatPath();
  void sizeSortWithMapsMatchesStatPath();
  void missingKeysFallBackToStat();
  void nameSortIgnoresMetadata();
  void buildSortMetadataKeysLikeSortFiles();
  void nameAscendingOrdersByPriorityBucket();
  void nameSortStripsLeadingApostrophe();
  void nameDescendingIsExactReverse();
  void nameSortIsCaseInsensitive();
  void dateSortBreaksTiesByName();
  void sizeDescendingBreaksTiesByName();

private:
  // Absolute paths for @p names under a subdirectory of m_dir that is never
  // created, so any accidental stat would see a missing file.
  QStringList namePaths(const QStringList &names) const;
  // Returns @p list in reverse order.
  static QStringList reversed(const QStringList &list);

  // Creates <name> under m_dir with @p size bytes and mtime @p mtime.
  QString makeFile(const QString &name, qint64 size, const QDateTime &mtime);

  QTemporaryDir m_dir;
  QString m_pathA; // size 1, oldest
  QString m_pathB; // size 3, newest
  QString m_pathC; // size 2, middle
};

QString TestQueryManagerSortFiles::makeFile(const QString &name, qint64 size,
                                            const QDateTime &mtime) {
  const QString path = QDir(m_dir.path()).absoluteFilePath(name);
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly)) {
    return {};
  }
  f.write(QByteArray(static_cast<int>(size), 'x'));
  // Flush BEFORE setFileTime: close() flushing buffered bytes afterwards
  // would stamp the mtime back to "now" and silently undo the override.
  if (!f.flush() || !f.setFileTime(mtime, QFileDevice::FileModificationTime)) {
    return {};
  }
  f.close();
  return path;
}

void TestQueryManagerSortFiles::initTestCase() {
  QVERIFY(m_dir.isValid());
  // Distinct whole-second mtimes (ISO-roundtrip safe) in an order that
  // deliberately disagrees with both the name order and the size order, so a
  // wrong sort key source cannot accidentally produce the right order.
  const QDateTime base = QDateTime::fromSecsSinceEpoch(1700000000);
  m_pathA = makeFile(QStringLiteral("aaa.bin"), 1, base);
  m_pathB = makeFile(QStringLiteral("bbb.bin"), 3, base.addSecs(20));
  m_pathC = makeFile(QStringLiteral("ccc.bin"), 2, base.addSecs(10));
  QVERIFY(!m_pathA.isEmpty());
  QVERIFY(!m_pathB.isEmpty());
  QVERIFY(!m_pathC.isEmpty());
}

void TestQueryManagerSortFiles::dateSortUsesProvidedMetadata() {
  // Map values CONTRADICT the on-disk mtimes (reverse order): if sortFiles
  // statted instead of using the map, the assertion below would fail.
  QHash<QString, qint64> mtimes;
  mtimes.insert(m_pathA, 3000);
  mtimes.insert(m_pathB, 1000);
  mtimes.insert(m_pathC, 2000);

  QStringList files{m_pathA, m_pathB, m_pathC};
  sortFiles(files, SortMode::DateAscending, &mtimes, nullptr);
  QCOMPARE(files, (QStringList{m_pathB, m_pathC, m_pathA}));

  sortFiles(files, SortMode::DateDescending, &mtimes, nullptr);
  QCOMPARE(files, (QStringList{m_pathA, m_pathC, m_pathB}));
}

void TestQueryManagerSortFiles::sizeSortUsesProvidedMetadata() {
  QHash<QString, qint64> sizes;
  sizes.insert(m_pathA, 300);
  sizes.insert(m_pathB, 100);
  sizes.insert(m_pathC, 200);

  QStringList files{m_pathA, m_pathB, m_pathC};
  sortFiles(files, SortMode::SizeAscending, nullptr, &sizes);
  QCOMPARE(files, (QStringList{m_pathB, m_pathC, m_pathA}));

  sortFiles(files, SortMode::SizeDescending, nullptr, &sizes);
  QCOMPARE(files, (QStringList{m_pathA, m_pathC, m_pathB}));
}

void TestQueryManagerSortFiles::dateSortWithMapsMatchesStatPath() {
  QHash<QString, qint64> mtimes;
  for (const QString &p : {m_pathA, m_pathB, m_pathC}) {
    mtimes.insert(p, QFileInfo(p).lastModified().toMSecsSinceEpoch());
  }

  for (SortMode mode : {SortMode::DateAscending, SortMode::DateDescending}) {
    QStringList statSorted{m_pathA, m_pathB, m_pathC};
    sortFiles(statSorted, mode); // historical stat-everything path
    QStringList mapSorted{m_pathA, m_pathB, m_pathC};
    sortFiles(mapSorted, mode, &mtimes, nullptr);
    QCOMPARE(mapSorted, statSorted);
  }

  // Sanity: the on-disk order really is B newest, C middle, A oldest.
  QStringList files{m_pathA, m_pathB, m_pathC};
  sortFiles(files, SortMode::DateAscending, &mtimes, nullptr);
  QCOMPARE(files, (QStringList{m_pathA, m_pathC, m_pathB}));
}

void TestQueryManagerSortFiles::sizeSortWithMapsMatchesStatPath() {
  QHash<QString, qint64> sizes;
  for (const QString &p : {m_pathA, m_pathB, m_pathC}) {
    sizes.insert(p, QFileInfo(p).size());
  }

  for (SortMode mode : {SortMode::SizeAscending, SortMode::SizeDescending}) {
    QStringList statSorted{m_pathA, m_pathB, m_pathC};
    sortFiles(statSorted, mode);
    QStringList mapSorted{m_pathA, m_pathB, m_pathC};
    sortFiles(mapSorted, mode, nullptr, &sizes);
    QCOMPARE(mapSorted, statSorted);
  }

  QStringList files{m_pathA, m_pathB, m_pathC};
  sortFiles(files, SortMode::SizeAscending, nullptr, &sizes);
  QCOMPARE(files, (QStringList{m_pathA, m_pathC, m_pathB}));
}

void TestQueryManagerSortFiles::missingKeysFallBackToStat() {
  // Only B is covered by the map (with a value making it the OLDEST); A and C
  // must fall back to their on-disk mtimes. A nonexistent path keeps the
  // historical "missing file sorts as 0" key, placing it first ascending.
  const QString ghost = QDir(m_dir.path()).absoluteFilePath(QStringLiteral("zzz-ghost.bin"));
  QHash<QString, qint64> mtimes;
  mtimes.insert(m_pathB, 1);

  QStringList files{m_pathA, m_pathB, m_pathC, ghost};
  sortFiles(files, SortMode::DateAscending, &mtimes, nullptr);
  QCOMPARE(files, (QStringList{ghost, m_pathB, m_pathA, m_pathC}));
}

void TestQueryManagerSortFiles::nameSortIgnoresMetadata() {
  // Maps that would reverse the order if consulted.
  QHash<QString, qint64> reversed;
  reversed.insert(m_pathA, 3);
  reversed.insert(m_pathB, 2);
  reversed.insert(m_pathC, 1);

  QStringList files{m_pathC, m_pathA, m_pathB};
  sortFiles(files, SortMode::NameAscending, &reversed, &reversed);
  QCOMPARE(files, (QStringList{m_pathA, m_pathB, m_pathC}));
}

void TestQueryManagerSortFiles::buildSortMetadataKeysLikeSortFiles() {
  const QDateTime mtime = QDateTime::fromSecsSinceEpoch(1700000123);
  QHash<QString, QDateTime> relTimestamps;
  relTimestamps.insert(QStringLiteral("sub/file.bin"), mtime);
  QHash<QString, qint64> relSizes;
  relSizes.insert(QStringLiteral("sub/file.bin"), 42);

  const QString abs = QDir(m_dir.path()).absoluteFilePath(QStringLiteral("sub/file.bin"));

  // Without a canonical-path cache: keyed by the plain absolute path.
  {
    QHash<QString, qint64> mtimeMsByPath;
    QHash<QString, qint64> sizeByPath;
    buildSortMetadata(m_dir.path(), relTimestamps, relSizes, nullptr, mtimeMsByPath, sizeByPath);
    QCOMPARE(mtimeMsByPath.value(abs, -1), mtime.toMSecsSinceEpoch());
    QCOMPARE(sizeByPath.value(abs, -1), qint64(42));
  }

  // With a cache (the dedup load path): keyed by the canonical form the cache
  // resolved, exactly like appendFileMapsAndListCanonical keyed the list.
  {
    const QString canon = QStringLiteral("/canonical/elsewhere/file.bin");
    QHash<QString, QString> cache;
    cache.insert(abs, canon);
    QHash<QString, qint64> mtimeMsByPath;
    QHash<QString, qint64> sizeByPath;
    buildSortMetadata(m_dir.path(), relTimestamps, relSizes, &cache, mtimeMsByPath, sizeByPath);
    QCOMPARE(mtimeMsByPath.value(canon, -1), mtime.toMSecsSinceEpoch());
    QCOMPARE(sizeByPath.value(canon, -1), qint64(42));
    QVERIFY(!mtimeMsByPath.contains(abs));
  }

  // First insertion wins across collections (mirrors the dedup list's
  // first-occurrence-wins semantics).
  {
    QHash<QString, qint64> mtimeMsByPath;
    QHash<QString, qint64> sizeByPath;
    buildSortMetadata(m_dir.path(), relTimestamps, relSizes, nullptr, mtimeMsByPath, sizeByPath);
    QHash<QString, QDateTime> laterTimestamps;
    laterTimestamps.insert(QStringLiteral("sub/file.bin"), mtime.addSecs(999));
    QHash<QString, qint64> laterSizes;
    laterSizes.insert(QStringLiteral("sub/file.bin"), 9999);
    buildSortMetadata(m_dir.path(), laterTimestamps, laterSizes, nullptr, mtimeMsByPath,
                      sizeByPath);
    QCOMPARE(mtimeMsByPath.value(abs, -1), mtime.toMSecsSinceEpoch());
    QCOMPARE(sizeByPath.value(abs, -1), qint64(42));
  }
}

QStringList TestQueryManagerSortFiles::namePaths(const QStringList &names) const {
  const QDir dir(QDir(m_dir.path()).absoluteFilePath(QStringLiteral("never-created")));
  QStringList paths;
  paths.reserve(names.size());
  for (const QString &name : names) {
    paths.append(dir.absoluteFilePath(name));
  }
  return paths;
}

QStringList TestQueryManagerSortFiles::reversed(const QStringList &list) {
  QStringList out = list;
  std::reverse(out.begin(), out.end());
  return out;
}

void TestQueryManagerSortFiles::nameAscendingOrdersByPriorityBucket() {
  // A plain compare of the lowercased keys would give
  // !bang, (alpha), 1 two, 9 nine, [beta], apple, zebra, ~tilde.
  const QStringList expected = namePaths({
      QStringLiteral("(Alpha).zip"),
      QStringLiteral("[Beta].zip"),
      QStringLiteral("!Bang.zip"),
      QStringLiteral("~Tilde.zip"),
      QStringLiteral("1 Two.zip"),
      QStringLiteral("9 Nine.zip"),
      QStringLiteral("apple.zip"),
      QStringLiteral("Zebra.zip"),
  });

  const QStringList scrambled{expected[6], expected[3], expected[1], expected[7],
                              expected[4], expected[0], expected[5], expected[2]};
  for (const QStringList &input : {scrambled, reversed(expected)}) {
    QStringList files = input;
    sortFiles(files, SortMode::NameAscending);
    QCOMPARE(files, expected);
  }
}

void TestQueryManagerSortFiles::nameSortStripsLeadingApostrophe() {
  // Unstripped, "'98 game" and "'bout time" would sort before "97 game" and
  // "apple" ('\'' < '9' < 'a'). "' Quote" keeps its apostrophe (space follows)
  // and so stays in the punctuation bucket, after "!Bang" ('!' < '\'').
  const QStringList expected = namePaths({
      QStringLiteral("!Bang.zip"),
      QStringLiteral("' Quote.zip"),
      QStringLiteral("97 Game.zip"),
      QStringLiteral("'98 Game.zip"),
      QStringLiteral("99 Game.zip"),
      QStringLiteral("Apple.zip"),
      QStringLiteral("'Bout Time.zip"),
      QStringLiteral("Cat.zip"),
  });

  const QStringList scrambled{expected[3], expected[7], expected[6], expected[0],
                              expected[4], expected[1], expected[2], expected[5]};
  for (const QStringList &input : {scrambled, reversed(expected)}) {
    QStringList files = input;
    sortFiles(files, SortMode::NameAscending);
    QCOMPARE(files, expected);
  }
}

void TestQueryManagerSortFiles::nameDescendingIsExactReverse() {
  // Every bucket plus the apostrophe cases, all with distinct normalized keys,
  // so descending must reverse both the bucket and the key order exactly.
  const QStringList ascending = namePaths({
      QStringLiteral("(Alpha).zip"),
      QStringLiteral("[Beta].zip"),
      QStringLiteral("!Bang.zip"),
      QStringLiteral("' Quote.zip"),
      QStringLiteral("~Tilde.zip"),
      QStringLiteral("1 Two.zip"),
      QStringLiteral("9 Nine.zip"),
      QStringLiteral("97 Game.zip"),
      QStringLiteral("'98 Game.zip"),
      QStringLiteral("99 Game.zip"),
      QStringLiteral("apple.zip"),
      QStringLiteral("'Bout Time.zip"),
      QStringLiteral("Cat.zip"),
      QStringLiteral("Zebra.zip"),
  });

  QStringList files = ascending;
  sortFiles(files, SortMode::NameDescending);
  QCOMPARE(files, reversed(ascending));

  sortFiles(files, SortMode::NameAscending);
  QCOMPARE(files, ascending);
}

void TestQueryManagerSortFiles::nameSortIsCaseInsensitive() {
  // A case-sensitive compare would put CHARLIE and Delta before alpha.
  const QStringList expected = namePaths({
      QStringLiteral("alpha.zip"),
      QStringLiteral("bravo.zip"),
      QStringLiteral("CHARLIE.zip"),
      QStringLiteral("Delta.zip"),
  });

  QStringList files{expected[3], expected[2], expected[0], expected[1]};
  sortFiles(files, SortMode::NameAscending);
  QCOMPARE(files, expected);

  sortFiles(files, SortMode::NameDescending);
  QCOMPARE(files, reversed(expected));
}

void TestQueryManagerSortFiles::dateSortBreaksTiesByName() {
  const QStringList paths = namePaths({
      QStringLiteral("Zebra.zip"),
      QStringLiteral("1 Two.zip"),
      QStringLiteral("apple.zip"),
      QStringLiteral("[Beta].zip"),
      QStringLiteral("zzz Oldest.zip"),
      QStringLiteral("aaa Newest.zip"),
  });
  const QString &zebra = paths[0];
  const QString &oneTwo = paths[1];
  const QString &apple = paths[2];
  const QString &beta = paths[3];
  const QString &oldest = paths[4];
  const QString &newest = paths[5];

  // The four tied names span three buckets; the untied ones have names that
  // would sort opposite to their mtimes, so the numeric key must win first.
  QHash<QString, qint64> mtimes;
  for (const QString &p : {zebra, oneTwo, apple, beta}) {
    mtimes[p] = 5000;
  }
  mtimes[oldest] = 1000;
  mtimes[newest] = 9000;

  QStringList files = paths;
  sortFiles(files, SortMode::DateAscending, &mtimes, nullptr);
  QCOMPARE(files, (QStringList{oldest, beta, oneTwo, apple, zebra, newest}));

  sortFiles(files, SortMode::DateDescending, &mtimes, nullptr);
  QCOMPARE(files, (QStringList{newest, zebra, apple, oneTwo, beta, oldest}));
}

void TestQueryManagerSortFiles::sizeDescendingBreaksTiesByName() {
  // Kartend-cfdac: the three 500-byte items arrive in neither name order, so
  // only a descending case-insensitive name tie-break yields Zebra, mango,
  // apple; the untied sizes must still win first.
  const QStringList paths = namePaths({
      QStringLiteral("apple.zip"),
      QStringLiteral("Zebra.zip"),
      QStringLiteral("mango.zip"),
      QStringLiteral("big.zip"),
      QStringLiteral("tiny.zip"),
  });
  const QString &apple = paths[0];
  const QString &zebra = paths[1];
  const QString &mango = paths[2];
  const QString &big = paths[3];
  const QString &tiny = paths[4];

  QHash<QString, qint64> sizes;
  sizes[apple] = 500;
  sizes[zebra] = 500;
  sizes[mango] = 500;
  sizes[big] = 900;
  sizes[tiny] = 100;

  QStringList files = paths;
  sortFiles(files, SortMode::SizeDescending, nullptr, &sizes);
  QCOMPARE(files, (QStringList{big, zebra, mango, apple, tiny}));
}

QTEST_GUILESS_MAIN(TestQueryManagerSortFiles)
#include "test_querymanager_sortfiles.moc"
