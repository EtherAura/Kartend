/**
 * @file test_datlibraryscan.cpp
 * @brief Unit tests for the DAT-library folder scan (Kartend-m6qsb.5).
 *
 * Synthesizes a library folder of Logiqx fixtures and verifies: proposals
 * for matching catalogues, exclusion of already-attached and dismissed
 * (path, mtime) revisions, re-proposal after a catalogue update, silent
 * skipping of non-DAT XML, and the defensive empty paths.
 */

#include "datlibraryscan.h"
#include "datlibrarystate.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using DatCollectionMatch::CollectionInfo;
using DatLibraryScan::ScanResult;

namespace {

void writeDat(const QString &path, const QString &headerName) {
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(QStringLiteral(R"xml(<?xml version="1.0"?>
<datafile>
  <header><name>%1</name><version>1.0</version></header>
  <game name="Entry"><rom name="Entry.mkv" size="1" crc="deadbeef"/></game>
</datafile>
)xml")
              .arg(headerName)
              .toUtf8());
}

CollectionInfo makeCollection(const QString &uuid, const QString &name) {
  CollectionInfo c;
  c.uuid = uuid;
  c.name = name;
  c.extensions = {QStringLiteral("mkv")};
  return c;
}

} // namespace

class TestDatLibraryScan : public QObject {
  Q_OBJECT

private slots:
  void proposesMatchingCatalogues();
  void excludesAttachedAndDismissed();
  void updatedCatalogueProposesAgain();
  void skipsNonCatalogueXmlAndDefensivePaths();
  void unmatchedCataloguesGoToUnmatchedList();

private:
  QTemporaryDir m_dir;
  int m_caseId = 0;

  QString freshRoot() {
    const QString root = m_dir.filePath(QStringLiteral("lib-%1").arg(m_caseId++));
    QDir().mkpath(root);
    return root;
  }
};

void TestDatLibraryScan::proposesMatchingCatalogues() {
  const QString root = freshRoot();
  writeDat(root + QStringLiteral("/concerts.dat"), QStringLiteral("Concert Recordings"));
  writeDat(root + QStringLiteral("/unrelated.dat"), QStringLiteral("Totally Different Topic"));

  const QList<CollectionInfo> collections{
      makeCollection(QStringLiteral("uuid-concerts"), QStringLiteral("Concert Recordings"))};
  const ScanResult result = DatLibraryScan::scan(root, collections, {});

  QCOMPARE(result.scannedDats, 2);
  QCOMPARE(result.proposals.size(), 1);
  QCOMPARE(result.proposals.first().headerName, QStringLiteral("Concert Recordings"));
  QCOMPARE(result.proposals.first().headerVersion, QStringLiteral("1.0"));
  QVERIFY(!result.proposals.first().candidates.isEmpty());
  QCOMPARE(result.proposals.first().candidates.first().collectionUuid,
           QStringLiteral("uuid-concerts"));
  QVERIFY(result.proposals.first().mtimeMs > 0);
}

void TestDatLibraryScan::excludesAttachedAndDismissed() {
  const QString root = freshRoot();
  const QString attachedPath = root + QStringLiteral("/attached.dat");
  const QString dismissedPath = root + QStringLiteral("/dismissed.dat");
  writeDat(attachedPath, QStringLiteral("Concert Recordings"));
  writeDat(dismissedPath, QStringLiteral("Concert Recordings"));

  CollectionInfo c =
      makeCollection(QStringLiteral("uuid-concerts"), QStringLiteral("Concert Recordings"));
  c.attachedDatPaths = {attachedPath}; // a fact, not a proposal

  const QFileInfo fi(dismissedPath);
  const QSet<QString> dismissed{
      DatLibraryState::dismissalKey(fi.canonicalFilePath(), fi.lastModified().toMSecsSinceEpoch())};

  const ScanResult result = DatLibraryScan::scan(root, {c}, dismissed);
  QVERIFY(result.proposals.isEmpty());
}

void TestDatLibraryScan::updatedCatalogueProposesAgain() {
  const QString root = freshRoot();
  const QString path = root + QStringLiteral("/concerts.dat");
  writeDat(path, QStringLiteral("Concert Recordings"));
  const QFileInfo before(path);

  // Dismissed at an OLDER revision (different mtime) — the current file is a
  // new catalogue revision and must propose again.
  const QSet<QString> dismissedOld{DatLibraryState::dismissalKey(
      before.canonicalFilePath(), before.lastModified().toMSecsSinceEpoch() - 1000)};

  const QList<CollectionInfo> collections{
      makeCollection(QStringLiteral("uuid-concerts"), QStringLiteral("Concert Recordings"))};
  const ScanResult result = DatLibraryScan::scan(root, collections, dismissedOld);
  QCOMPARE(result.proposals.size(), 1);
}

void TestDatLibraryScan::skipsNonCatalogueXmlAndDefensivePaths() {
  const QString root = freshRoot();
  {
    QFile f(root + QStringLiteral("/stray.xml"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("<html><body>not a catalogue</body></html>");
  }
  const QList<CollectionInfo> collections{
      makeCollection(QStringLiteral("uuid-a"), QStringLiteral("Anything"))};

  const ScanResult result = DatLibraryScan::scan(root, collections, {});
  QCOMPARE(result.scannedDats, 0);
  QVERIFY(result.proposals.isEmpty());

  // Defensive paths: missing root, empty root, no collections.
  QVERIFY(DatLibraryScan::scan(QString(), collections, {}).proposals.isEmpty());
  QVERIFY(DatLibraryScan::scan(m_dir.filePath(QStringLiteral("absent")), collections, {})
              .proposals.isEmpty());
  QVERIFY(DatLibraryScan::scan(root, {}, {}).proposals.isEmpty());
}

void TestDatLibraryScan::unmatchedCataloguesGoToUnmatchedList() {
  // A valid catalogue that matches no collection isn't a proposal, but it IS
  // surfaced separately so the user can attach it by hand (Kartend-m6qsb.24).
  const QString root = freshRoot();
  writeDat(root + QStringLiteral("/concerts.dat"), QStringLiteral("Concert Recordings"));
  writeDat(root + QStringLiteral("/orphan.dat"), QStringLiteral("Totally Unrelated Topic"));

  const QList<CollectionInfo> collections{
      makeCollection(QStringLiteral("uuid-concerts"), QStringLiteral("Concert Recordings"))};
  const ScanResult result = DatLibraryScan::scan(root, collections, {});

  QCOMPARE(result.proposals.size(), 1); // the matching one
  QCOMPARE(result.proposals.first().headerName, QStringLiteral("Concert Recordings"));
  QCOMPARE(result.unmatched.size(), 1); // the orphan
  QCOMPARE(result.unmatched.first().headerName, QStringLiteral("Totally Unrelated Topic"));
  QVERIFY(result.unmatched.first().candidates.isEmpty());
}

QTEST_MAIN(TestDatLibraryScan)
#include "test_datlibraryscan.moc"
