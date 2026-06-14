/**
 * @file test_datcollectionmatch.cpp
 * @brief Unit tests for the pure DAT-to-collection match scorer.
 *
 * Verifies the ranking heuristics (exact name match, qualifier
 * stripping, token containment, header-description / filename
 * fallbacks), the extension-overlap boost and zero-overlap penalty,
 * the proposal threshold, deterministic near-tie ordering, the
 * canonical-path already-attached exclusion, and that empty inputs
 * are safe. No build of records or cache is involved — the scorer
 * consumes plain value structs.
 */

#include "datcollectionmatch.h"

#include <QDir>
#include <QFile>
#include <QList>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

namespace {

DatCollectionMatch::CollectionInfo makeCollection(const QString &uuid, const QString &name,
                                                  const QStringList &extensions = {},
                                                  const QStringList &attachedDatPaths = {}) {
  DatCollectionMatch::CollectionInfo c;
  c.uuid = uuid;
  c.name = name;
  c.extensions = extensions;
  c.attachedDatPaths = attachedDatPaths;
  return c;
}

DatCollectionMatch::DatInfo makeDat(const QString &headerName,
                                    const QStringList &romExtensions = {},
                                    const QString &path = {}) {
  DatCollectionMatch::DatInfo d;
  d.path = path;
  d.headerName = headerName;
  d.romExtensions = romExtensions;
  return d;
}

QString writeFile(const QString &path, const char *bytes) {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly)) return QString();
  f.write(bytes);
  f.close();
  return path;
}

} // namespace

class TestDatCollectionMatch : public QObject {
  Q_OBJECT

private slots:
  void exactNameMatchWinsOverUnrelated();
  void qualifierSuffixesAreIgnored();
  void tokenContainmentMatchesShorterCollectionName();
  void headerDescriptionAndFilenameFallbacks();
  void extensionOverlapBoostsScore();
  void zeroExtensionOverlapPenalizesScore();
  void belowThresholdYieldsNoCandidates();
  void nearTieReturnsBothDeterministically();
  void alreadyAttachedCollectionIsExcluded();
  void alreadyAttachedMatchesNonCanonicalPathVariant();
  void missingFileFallsBackToStringComparison();
  void emptyInputsAreSafe();

private:
  QTemporaryDir m_dir;
};

void TestDatCollectionMatch::exactNameMatchWinsOverUnrelated() {
  const auto dat = makeDat(QStringLiteral("Concert Recordings"));
  const QList<DatCollectionMatch::CollectionInfo> collections = {
      makeCollection(QStringLiteral("uuid-lectures"), QStringLiteral("Lecture Archive")),
      makeCollection(QStringLiteral("uuid-concerts"), QStringLiteral("Concert Recordings")),
  };

  const auto result = DatCollectionMatch::rankCollections(dat, collections);
  // The unrelated collection shares no name tokens — it must not be
  // proposed at all, not merely ranked lower.
  QCOMPARE(result.candidates.size(), 1);
  QCOMPARE(result.candidates.first().collectionUuid, QStringLiteral("uuid-concerts"));
  QVERIFY(result.candidates.first().score >= DatCollectionMatch::kScoreThreshold);
  QVERIFY(!result.candidates.first().reason.isEmpty());
  QVERIFY(result.alreadyAttachedTo.isEmpty());
}

void TestDatCollectionMatch::qualifierSuffixesAreIgnored() {
  // Catalogue names carry revision/source qualifiers that say nothing
  // about the destination collection. Stripping them must make the
  // decorated name score identically to the plain one.
  const auto plain = makeDat(QStringLiteral("Concert Recordings"));
  const auto decorated = makeDat(QStringLiteral("Concert Recordings (2026 Refresh) [curated]"));
  const QList<DatCollectionMatch::CollectionInfo> collections = {
      makeCollection(QStringLiteral("uuid-concerts"), QStringLiteral("Concert Recordings")),
  };

  const auto plainResult = DatCollectionMatch::rankCollections(plain, collections);
  const auto decoratedResult = DatCollectionMatch::rankCollections(decorated, collections);
  QCOMPARE(plainResult.candidates.size(), 1);
  QCOMPARE(decoratedResult.candidates.size(), 1);
  QCOMPARE(decoratedResult.candidates.first().score, plainResult.candidates.first().score);
}

void TestDatCollectionMatch::tokenContainmentMatchesShorterCollectionName() {
  // "Set - Subset" catalogue names are longer than the collection
  // label they belong to. Full containment of the collection tokens
  // must propose — but rank below an exact token-set match.
  const auto dat = makeDat(QStringLiteral("Audio - Lossless Masters"));
  const QList<DatCollectionMatch::CollectionInfo> collections = {
      makeCollection(QStringLiteral("uuid-contained"), QStringLiteral("Lossless Masters")),
      makeCollection(QStringLiteral("uuid-exact"), QStringLiteral("Audio Lossless Masters")),
  };

  const auto result = DatCollectionMatch::rankCollections(dat, collections);
  QCOMPARE(result.candidates.size(), 2);
  QCOMPARE(result.candidates.at(0).collectionUuid, QStringLiteral("uuid-exact"));
  QCOMPARE(result.candidates.at(1).collectionUuid, QStringLiteral("uuid-contained"));
  QVERIFY(result.candidates.at(0).score > result.candidates.at(1).score);
  QVERIFY(result.candidates.at(1).score >= DatCollectionMatch::kScoreThreshold);
}

void TestDatCollectionMatch::headerDescriptionAndFilenameFallbacks() {
  const QList<DatCollectionMatch::CollectionInfo> collections = {
      makeCollection(QStringLiteral("uuid-concerts"), QStringLiteral("Concert Recordings")),
  };

  // No header name → the header description carries the signal.
  DatCollectionMatch::DatInfo byDescription;
  byDescription.headerDescription = QStringLiteral("Concert Recordings - master set");
  const auto descResult = DatCollectionMatch::rankCollections(byDescription, collections);
  QCOMPARE(descResult.candidates.size(), 1);
  QCOMPARE(descResult.candidates.first().collectionUuid, QStringLiteral("uuid-concerts"));

  // No header at all (legal in Logiqx) → the base filename is the
  // last resort. The path doesn't need to exist for scoring.
  DatCollectionMatch::DatInfo byFilename;
  byFilename.path = QStringLiteral("/library/catalogues/Concert Recordings.dat");
  const auto fileResult = DatCollectionMatch::rankCollections(byFilename, collections);
  QCOMPARE(fileResult.candidates.size(), 1);
  QCOMPARE(fileResult.candidates.first().collectionUuid, QStringLiteral("uuid-concerts"));
}

void TestDatCollectionMatch::extensionOverlapBoostsScore() {
  const QList<DatCollectionMatch::CollectionInfo> collections = {
      makeCollection(QStringLiteral("uuid-video"), QStringLiteral("Reference Video Set"),
                     {QStringLiteral("mkv"), QStringLiteral("mp4")}),
  };

  // Baseline: name-only signal (the DAT declares no extensions).
  const auto baseline = DatCollectionMatch::rankCollections(
      makeDat(QStringLiteral("Reference Video Set")), collections);
  QCOMPARE(baseline.candidates.size(), 1);

  // Same name, but the DAT's record extensions are accepted by the
  // collection's allow-list — the score must strictly improve.
  const auto boosted = DatCollectionMatch::rankCollections(
      makeDat(QStringLiteral("Reference Video Set"), {QStringLiteral("mkv")}), collections);
  QCOMPARE(boosted.candidates.size(), 1);
  QVERIFY(boosted.candidates.first().score > baseline.candidates.first().score);
  QVERIFY(boosted.candidates.first().reason.contains(QStringLiteral("+ extension overlap")));
}

void TestDatCollectionMatch::zeroExtensionOverlapPenalizesScore() {
  const QList<DatCollectionMatch::CollectionInfo> collections = {
      makeCollection(QStringLiteral("uuid-video"), QStringLiteral("Reference Video Set"),
                     {QStringLiteral("mkv"), QStringLiteral("mp4")}),
  };

  const auto baseline = DatCollectionMatch::rankCollections(
      makeDat(QStringLiteral("Reference Video Set")), collections);
  QCOMPARE(baseline.candidates.size(), 1);

  // Both sides declare extensions but share none: a strong name match
  // is demoted (not vetoed) — it stays proposed at a lower score.
  const auto penalized = DatCollectionMatch::rankCollections(
      makeDat(QStringLiteral("Reference Video Set"), {QStringLiteral("flac")}), collections);
  QCOMPARE(penalized.candidates.size(), 1);
  QVERIFY(penalized.candidates.first().score < baseline.candidates.first().score);
  QVERIFY(penalized.candidates.first().reason.contains(QStringLiteral("no extension overlap")));
}

void TestDatCollectionMatch::belowThresholdYieldsNoCandidates() {
  // No shared name tokens — extension overlap alone (even a full one)
  // must never be enough to propose a candidate.
  const auto dat = makeDat(QStringLiteral("Quarterly Tax Scans"), {QStringLiteral("pdf")});
  const QList<DatCollectionMatch::CollectionInfo> collections = {
      makeCollection(QStringLiteral("uuid-concerts"), QStringLiteral("Concert Recordings"),
                     {QStringLiteral("pdf")}),
  };

  const auto result = DatCollectionMatch::rankCollections(dat, collections);
  QVERIFY(result.candidates.isEmpty());
  QVERIFY(result.alreadyAttachedTo.isEmpty());
}

void TestDatCollectionMatch::nearTieReturnsBothDeterministically() {
  // Two collections normalize to the same token set → identical
  // scores. Both must be returned (the review flow never auto-picks),
  // ordered by name ascending regardless of input order.
  const auto dat = makeDat(QStringLiteral("Concert Recordings"));
  const QList<DatCollectionMatch::CollectionInfo> collections = {
      makeCollection(QStringLiteral("uuid-dashed"), QStringLiteral("Concert-Recordings")),
      makeCollection(QStringLiteral("uuid-spaced"), QStringLiteral("Concert Recordings")),
  };

  const auto result = DatCollectionMatch::rankCollections(dat, collections);
  QCOMPARE(result.candidates.size(), 2);
  QCOMPARE(result.candidates.at(0).score, result.candidates.at(1).score);
  // ' ' sorts before '-', so the spaced name wins the tiebreak.
  QCOMPARE(result.candidates.at(0).collectionUuid, QStringLiteral("uuid-spaced"));
  QCOMPARE(result.candidates.at(1).collectionUuid, QStringLiteral("uuid-dashed"));
}

void TestDatCollectionMatch::alreadyAttachedCollectionIsExcluded() {
  const QString datPath = writeFile(m_dir.filePath(QStringLiteral("concerts.dat")), "<datafile/>");
  QVERIFY(!datPath.isEmpty());

  const auto dat = makeDat(QStringLiteral("Concert Recordings"), {}, datPath);
  const QList<DatCollectionMatch::CollectionInfo> collections = {
      makeCollection(QStringLiteral("uuid-attached"), QStringLiteral("Concert Recordings"), {},
                     {datPath}),
      makeCollection(QStringLiteral("uuid-other"), QStringLiteral("Concert Recordings")),
  };

  const auto result = DatCollectionMatch::rankCollections(dat, collections);
  // The attached collection is a fact, not a proposal — only the
  // other identically-named collection may be proposed.
  QCOMPARE(result.alreadyAttachedTo, QStringLiteral("uuid-attached"));
  QCOMPARE(result.candidates.size(), 1);
  QCOMPARE(result.candidates.first().collectionUuid, QStringLiteral("uuid-other"));
}

void TestDatCollectionMatch::alreadyAttachedMatchesNonCanonicalPathVariant() {
  // The same file reached through a "sub/.." detour must still count
  // as attached — both sides canonicalise before comparing.
  const QString datPath = writeFile(m_dir.filePath(QStringLiteral("concerts.dat")), "<datafile/>");
  QVERIFY(!datPath.isEmpty());
  QVERIFY(QDir(m_dir.path()).mkpath(QStringLiteral("sub")));
  const QString detourPath = m_dir.filePath(QStringLiteral("sub/../concerts.dat"));

  const auto dat = makeDat(QStringLiteral("Concert Recordings"), {}, detourPath);
  const QList<DatCollectionMatch::CollectionInfo> collections = {
      makeCollection(QStringLiteral("uuid-attached"), QStringLiteral("Concert Recordings"), {},
                     {datPath}),
  };

  const auto result = DatCollectionMatch::rankCollections(dat, collections);
  QCOMPARE(result.alreadyAttachedTo, QStringLiteral("uuid-attached"));
  QVERIFY(result.candidates.isEmpty());
}

void TestDatCollectionMatch::missingFileFallsBackToStringComparison() {
  // Missing files can't be canonicalised; identity degrades to plain
  // string equality (mirrors DatCache::peek). The exact same string
  // still counts as attached…
  const QString missing = QStringLiteral("/nonexistent/library/concerts.dat");
  const auto dat = makeDat(QStringLiteral("Concert Recordings"), {}, missing);
  const auto attached = DatCollectionMatch::rankCollections(
      dat, {makeCollection(QStringLiteral("uuid-attached"), QStringLiteral("Concert Recordings"),
                           {}, {missing})});
  QCOMPARE(attached.alreadyAttachedTo, QStringLiteral("uuid-attached"));

  // …but a differently-spelled variant of a missing path does not —
  // that's the documented limitation of the fallback, so the
  // collection is scored as a normal proposal instead.
  const QString variant = QStringLiteral("/nonexistent/library/./concerts.dat");
  const auto unmatched = DatCollectionMatch::rankCollections(
      dat, {makeCollection(QStringLiteral("uuid-variant"), QStringLiteral("Concert Recordings"), {},
                           {variant})});
  QVERIFY(unmatched.alreadyAttachedTo.isEmpty());
  QCOMPARE(unmatched.candidates.size(), 1);
}

void TestDatCollectionMatch::emptyInputsAreSafe() {
  // No collections at all.
  const auto noCollections =
      DatCollectionMatch::rankCollections(makeDat(QStringLiteral("Concert Recordings")), {});
  QVERIFY(noCollections.candidates.isEmpty());
  QVERIFY(noCollections.alreadyAttachedTo.isEmpty());

  // A fully-empty DatInfo carries no name signal — nothing can clear
  // the threshold, and the empty path must not "match" anything.
  const auto emptyDat = DatCollectionMatch::rankCollections(
      DatCollectionMatch::DatInfo{},
      {makeCollection(QStringLiteral("uuid-concerts"), QStringLiteral("Concert Recordings"))});
  QVERIFY(emptyDat.candidates.isEmpty());
  QVERIFY(emptyDat.alreadyAttachedTo.isEmpty());

  // A collection with an empty name is silently unproposable, not a crash.
  const auto emptyName = DatCollectionMatch::rankCollections(
      makeDat(QStringLiteral("Concert Recordings")),
      {makeCollection(QStringLiteral("uuid-unnamed"), QString())});
  QVERIFY(emptyName.candidates.isEmpty());
}

QTEST_MAIN(TestDatCollectionMatch)
#include "test_datcollectionmatch.moc"
