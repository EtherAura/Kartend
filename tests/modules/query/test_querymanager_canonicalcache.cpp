// Regression tests for Kartend-4fmu1: loadItemsWithSubcollections dedups via
// canonicalKeyPath (QFileInfo::canonicalFilePath — a full realpath walk per
// item), and its per-load cache only collapsed duplicates WITHIN one load, so
// every flattened collection switch re-resolved all 10k+ paths. The fix keeps
// a persistent (absolute path -> (mtime ms, canonical path)) map on
// QueryManager, moved in and out of the per-load cache by
// seedCanonicalPathCache / harvestCanonicalPathCache with the mtime the load
// already has in hand as the validity stamp.
//
//   seedSkipsRealpathForValidEntries
//     A seeded entry is returned by canonicalKeyPath verbatim — proven by
//     seeding a deliberately WRONG canonical value for a real file and
//     observing it win over what a realpath would produce.
//
//   seedRejectsStaleMtime
//     A persisted entry whose mtime stamp differs from the in-hand mtime is
//     NOT seeded; canonicalKeyPath re-resolves from the filesystem.
//
//   harvestRoundTrips
//     What one load resolves, the next load's seed reproduces — the
//     cross-load survival this issue is about — including symlink resolution
//     (when the platform lets the test create one).

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QPair>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

#include "querymanagerhelpers.h"

using QueryManagerInternal::canonicalKeyPath;
using QueryManagerInternal::harvestCanonicalPathCache;
using QueryManagerInternal::seedCanonicalPathCache;

class TestQueryManagerCanonicalCache : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void seedSkipsRealpathForValidEntries();
  void seedRejectsStaleMtime();
  void harvestRoundTrips();

private:
  QTemporaryDir m_dir;
  QString m_relName;
  QString m_absPath;
  QDateTime m_mtime;
};

void TestQueryManagerCanonicalCache::initTestCase() {
  QVERIFY(m_dir.isValid());
  m_relName = QStringLiteral("file.bin");
  m_absPath = QDir(m_dir.path()).absoluteFilePath(m_relName);
  QFile f(m_absPath);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("x");
  f.close();
  m_mtime = QFileInfo(m_absPath).lastModified();
  QVERIFY(m_mtime.isValid());
}

void TestQueryManagerCanonicalCache::seedSkipsRealpathForValidEntries() {
  // Persist a deliberately WRONG canonical value with the CORRECT mtime
  // stamp. If the seed works and canonicalKeyPath trusts the per-load cache,
  // the wrong value comes back — proof that no realpath walk happened.
  QHash<QString, QPair<qint64, QString>> persistent;
  persistent.insert(m_absPath,
                    qMakePair(m_mtime.toMSecsSinceEpoch(), QStringLiteral("/sentinel/canon.bin")));

  QHash<QString, QDateTime> relTimestamps;
  relTimestamps.insert(m_relName, m_mtime);

  QHash<QString, QString> perLoad;
  seedCanonicalPathCache(m_dir.path(), relTimestamps, persistent, perLoad);
  QCOMPARE(perLoad.value(m_absPath), QStringLiteral("/sentinel/canon.bin"));

  QCOMPARE(canonicalKeyPath(m_absPath, true, &perLoad), QStringLiteral("/sentinel/canon.bin"));
}

void TestQueryManagerCanonicalCache::seedRejectsStaleMtime() {
  QHash<QString, QPair<qint64, QString>> persistent;
  persistent.insert(
      m_absPath, qMakePair(m_mtime.toMSecsSinceEpoch() - 1, QStringLiteral("/sentinel/stale.bin")));

  QHash<QString, QDateTime> relTimestamps;
  relTimestamps.insert(m_relName, m_mtime);

  QHash<QString, QString> perLoad;
  seedCanonicalPathCache(m_dir.path(), relTimestamps, persistent, perLoad);
  QVERIFY(!perLoad.contains(m_absPath));

  // canonicalKeyPath therefore resolves from the filesystem.
  const QString resolved = canonicalKeyPath(m_absPath, true, &perLoad);
  QCOMPARE(resolved, QFileInfo(m_absPath).canonicalFilePath());
}

void TestQueryManagerCanonicalCache::harvestRoundTrips() {
  // A symlinked entry makes the canonical form differ from the cleaned
  // absolute path, exercising the case the dedup cache exists for.
  const QString linkPath = QDir(m_dir.path()).absoluteFilePath(QStringLiteral("link.bin"));
  if (!QFile::link(m_absPath, linkPath)) {
    QSKIP("Platform/filesystem does not support symlinks here");
  }
  const QDateTime linkMtime = QFileInfo(linkPath).lastModified(); // target's mtime

  QHash<QString, QDateTime> relTimestamps;
  relTimestamps.insert(QStringLiteral("link.bin"), linkMtime);

  // Load 1: cold — resolve via the filesystem, then harvest.
  QHash<QString, QPair<qint64, QString>> persistent;
  QHash<QString, QString> perLoad1;
  seedCanonicalPathCache(m_dir.path(), relTimestamps, persistent, perLoad1);
  QVERIFY(perLoad1.isEmpty());
  const QString resolved = canonicalKeyPath(linkPath, true, &perLoad1);
  QCOMPARE(resolved, QFileInfo(m_absPath).canonicalFilePath());
  harvestCanonicalPathCache(m_dir.path(), relTimestamps, perLoad1, persistent);
  QCOMPARE(persistent.value(linkPath).second, resolved);
  QCOMPARE(persistent.value(linkPath).first, linkMtime.toMSecsSinceEpoch());

  // Load 2: warm — the seed reproduces load 1's resolution without touching
  // the filesystem (same observable result either way; the cache hit is what
  // Kartend-4fmu1 is about, asserted via the perLoad pre-population).
  QHash<QString, QString> perLoad2;
  seedCanonicalPathCache(m_dir.path(), relTimestamps, persistent, perLoad2);
  QCOMPARE(perLoad2.value(linkPath), resolved);
  QCOMPARE(canonicalKeyPath(linkPath, true, &perLoad2), resolved);
}

QTEST_GUILESS_MAIN(TestQueryManagerCanonicalCache)
#include "test_querymanager_canonicalcache.moc"
