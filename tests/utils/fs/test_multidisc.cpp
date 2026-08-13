/**
 * @file test_multidisc.cpp
 * @brief Unit tests for the multi-disc release detector (Kartend-21t50).
 * Pure string/grouping transforms — no filesystem is touched, so every case
 * here is a paper exercise over names and paths.
 */

#include "multidisc.h"

#include <QString>
#include <QStringList>
#include <QTest>

class TestMultiDisc : public QObject {
  Q_OBJECT

private slots:
  void parse_detectsMarkers_data();
  void parse_detectsMarkers();
  void parse_rejectsUnorderableAndUnrelatedTags_data();
  void parse_rejectsUnorderableAndUnrelatedTags();
  void parse_keepsOtherTagsInTheBase();

  void group_collectsDiscsOfOneRelease();
  void group_ordersNumericallyNotLexicographically();
  void group_ignoresLoneDiscFile();
  void group_separatesIdenticalNamesInDifferentDirectories();
  void group_foldsDiskAndDiscSpellings();
  void group_ignoresFilesWithoutMarkers();

  void m3u_writesRelativePathsForSiblingsAndAbsoluteOtherwise();
  void m3uFileName_sanitisesIllegalCharacters();
};

void TestMultiDisc::parse_detectsMarkers_data() {
  QTest::addColumn<QString>("fileName");
  QTest::addColumn<QString>("expectedBase");
  QTest::addColumn<QString>("expectedQualifier");

  QTest::newRow("disc") << "Recital (Disc 2).flac" << "Recital" << "disc 2";
  QTest::newRow("disk-folds-to-disc") << "Recital (Disk 2).flac" << "Recital" << "disc 2";
  QTest::newRow("cd") << "Recital (CD 3).flac" << "Recital" << "cd 3";
  QTest::newRow("cd-no-space") << "Recital (CD3).flac" << "Recital" << "cd 3";
  QTest::newRow("side-letter") << "Recital (Side A).flac" << "Recital" << "side a";
  QTest::newRow("brackets") << "Recital [Disc 1].flac" << "Recital" << "disc 1";
  QTest::newRow("case-insensitive") << "Recital (DISC 1).flac" << "Recital" << "disc 1";
  // Multi-dot names must keep everything before the real extension.
  QTest::newRow("dotted-name") << "Recital.Live.1994 (Disc 1).mkv" << "Recital.Live.1994"
                               << "disc 1";
}

void TestMultiDisc::parse_detectsMarkers() {
  QFETCH(QString, fileName);
  QFETCH(QString, expectedBase);
  QFETCH(QString, expectedQualifier);

  const auto marker = MultiDisc::parse(fileName);
  QVERIFY(marker.isValid());
  QCOMPARE(marker.base, expectedBase);
  QCOMPARE(marker.qualifier, expectedQualifier);
}

void TestMultiDisc::parse_rejectsUnorderableAndUnrelatedTags_data() {
  QTest::addColumn<QString>("fileName");

  QTest::newRow("no-tag") << "Recital.flac";
  QTest::newRow("unrelated-tag") << "Recital (Remastered).flac";
  QTest::newRow("region-tag") << "Recital (USA).flac";
  // A marker we cannot ORDER is worse than none: it would place discs
  // arbitrarily in the generated playlist, so spelled-out and open-ended
  // values are deliberately not markers.
  QTest::newRow("spelled-out-number") << "Recital (Disc Two).flac";
  QTest::newRow("multi-letter-value") << "Recital (Disc AB).flac";
  QTest::newRow("edition-not-disc") << "Recital (Special Edition).flac";
}

void TestMultiDisc::parse_rejectsUnorderableAndUnrelatedTags() {
  QFETCH(QString, fileName);
  QVERIFY(!MultiDisc::parse(fileName).isValid());
}

void TestMultiDisc::parse_keepsOtherTagsInTheBase() {
  // Only the disc tag is consumed. The region tag still distinguishes this
  // release from its other-region siblings, so dropping it would merge two
  // different releases into one playlist.
  const auto marker = MultiDisc::parse(QStringLiteral("Recital (USA) (Disc 2).bin"));
  QVERIFY(marker.isValid());
  QCOMPARE(marker.base, QStringLiteral("Recital (USA)"));
  QCOMPARE(marker.qualifier, QStringLiteral("disc 2"));
}

void TestMultiDisc::group_collectsDiscsOfOneRelease() {
  const QStringList paths{QStringLiteral("/m/Recital (Disc 1).flac"),
                          QStringLiteral("/m/Recital (Disc 2).flac")};
  const auto groups = MultiDisc::group(paths);
  QCOMPARE(groups.size(), 1);
  QCOMPARE(groups.first().base, QStringLiteral("Recital"));
  QCOMPARE(groups.first().files, paths);
}

void TestMultiDisc::group_ordersNumericallyNotLexicographically() {
  // Input deliberately shuffled and spanning double digits. Sorted as strings
  // "disc 10" would land next to "disc 1", silently reordering the playlist of
  // any release with ten or more parts.
  const QStringList paths{
      QStringLiteral("/m/Set (Disc 10).flac"), QStringLiteral("/m/Set (Disc 2).flac"),
      QStringLiteral("/m/Set (Disc 1).flac"), QStringLiteral("/m/Set (Disc 9).flac")};
  const auto groups = MultiDisc::group(paths);
  QCOMPARE(groups.size(), 1);
  const QStringList expected{
      QStringLiteral("/m/Set (Disc 1).flac"), QStringLiteral("/m/Set (Disc 2).flac"),
      QStringLiteral("/m/Set (Disc 9).flac"), QStringLiteral("/m/Set (Disc 10).flac")};
  QCOMPARE(groups.first().files, expected);
}

void TestMultiDisc::group_ignoresLoneDiscFile() {
  // One labelled file is not a release split across discs. Collapsing it would
  // strip the label from its title and leave a one-line playlist behind.
  const auto groups = MultiDisc::group(QStringList{QStringLiteral("/m/Recital (Disc 1).flac")});
  QVERIFY(groups.isEmpty());
}

void TestMultiDisc::group_separatesIdenticalNamesInDifferentDirectories() {
  // Same title, two folders: two releases. Merging them would build a playlist
  // that jumps between directories.
  const QStringList paths{
      QStringLiteral("/m/live/Recital (Disc 1).flac"),
      QStringLiteral("/m/live/Recital (Disc 2).flac"),
      QStringLiteral("/m/studio/Recital (Disc 1).flac"),
      QStringLiteral("/m/studio/Recital (Disc 2).flac")};
  const auto groups = MultiDisc::group(paths);
  QCOMPARE(groups.size(), 2);
  QCOMPARE(groups.at(0).files.size(), 2);
  QCOMPARE(groups.at(1).files.size(), 2);
  QVERIFY(groups.at(0).files.first().contains(QStringLiteral("/live/")));
  QVERIFY(groups.at(1).files.first().contains(QStringLiteral("/studio/")));
}

void TestMultiDisc::group_foldsDiskAndDiscSpellings() {
  // One release whose files disagree on spelling still groups, because "disk"
  // folds to "disc" before the key is built.
  const QStringList paths{QStringLiteral("/m/Recital (Disc 1).flac"),
                          QStringLiteral("/m/Recital (Disk 2).flac")};
  const auto groups = MultiDisc::group(paths);
  QCOMPARE(groups.size(), 1);
  QCOMPARE(groups.first().files.size(), 2);
}

void TestMultiDisc::group_ignoresFilesWithoutMarkers() {
  const QStringList paths{QStringLiteral("/m/Recital (Disc 1).flac"),
                          QStringLiteral("/m/Recital (Disc 2).flac"),
                          QStringLiteral("/m/Encore.flac")};
  const auto groups = MultiDisc::group(paths);
  QCOMPARE(groups.size(), 1);
  QCOMPARE(groups.first().files.size(), 2);
}

void TestMultiDisc::m3u_writesRelativePathsForSiblingsAndAbsoluteOtherwise() {
  const QStringList paths{QStringLiteral("/m/Recital (Disc 1).flac"),
                          QStringLiteral("/other/Recital (Disc 2).flac")};
  const QString body = MultiDisc::buildM3uContents(paths, QStringLiteral("/m"));
  // Sibling relative so moving the folder keeps the playlist working; the
  // outsider stays absolute rather than growing a fragile "../" chain.
  QCOMPARE(body, QStringLiteral("Recital (Disc 1).flac\n/other/Recital (Disc 2).flac\n"));

  // Empty dir forces absolute paths throughout.
  const QString absolute = MultiDisc::buildM3uContents(paths, QString());
  QVERIFY(absolute.startsWith(QStringLiteral("/m/Recital (Disc 1).flac\n")));
}

void TestMultiDisc::m3uFileName_sanitisesIllegalCharacters() {
  QCOMPARE(MultiDisc::m3uFileNameFor(QStringLiteral("Recital")), QStringLiteral("Recital.m3u"));
  // A separator in a title must never let the generated name escape its
  // directory.
  QCOMPARE(MultiDisc::m3uFileNameFor(QStringLiteral("AC/DC: Live?")),
           QStringLiteral("AC_DC_ Live_.m3u"));
  QCOMPARE(MultiDisc::m3uFileNameFor(QString()), QStringLiteral("playlist.m3u"));
}

QTEST_APPLESS_MAIN(TestMultiDisc)
#include "test_multidisc.moc"
