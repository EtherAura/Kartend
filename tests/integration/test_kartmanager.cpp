#include "test_kartmanager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include "applicationcontext.h"
#include "applicationmanager.h"
#include "collection/collectionconfig.h"
#include "collection/launcherpreset.h"
#include "iplaylistmanager.h"
#include "kartcompression.h"
#include "kartmanager.h"
#include "kartmanifest.h"
#include "kartmerge.h"
#include "kartreader.h"
#include "kartwriter.h"

namespace {

// Wipe the QStandardPaths-routed qttest sandbox between tests so each
// case starts from a known-empty state. test_main.cpp enables test mode
// process-wide; the directories live under ~/.qttest on Linux and are
// recreated lazily when ApplicationManager::initialize() runs.
void wipeSandbox() {
  const auto wipe = [](QStandardPaths::StandardLocation loc) {
    const QString p = QStandardPaths::writableLocation(loc);
    if (!p.isEmpty() && p.contains(QStringLiteral("qttest"))) {
      QDir(p).removeRecursively();
    }
  };
  wipe(QStandardPaths::ConfigLocation);
  wipe(QStandardPaths::AppConfigLocation);
  wipe(QStandardPaths::AppDataLocation);
  wipe(QStandardPaths::AppLocalDataLocation);
  wipe(QStandardPaths::CacheLocation);
  wipe(QStandardPaths::GenericCacheLocation);
}

// Writes a small file at `dir/<name>` with the supplied bytes. Used to
// stand in for real ROM / artwork files in the synthesized collection
// the kart bundle is built from.
void writeFile(const QDir &dir, const QString &name, const QByteArray &bytes) {
  QFile f(dir.filePath(name));
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(bytes);
  f.close();
}

// Build a self-contained collection on disk and return a CollectionConfig
// pointing at it. The collection has one ROM with one matching artwork
// asset so KartWriter::prepareFromCollection picks up both.
struct SyntheticCollection {
  QTemporaryDir root;
  CollectionConfig cfg;
};

std::unique_ptr<SyntheticCollection> buildSyntheticCollection(const QString &name,
                                                              const QString &romFileName,
                                                              const QByteArray &romBytes) {
  auto out = std::make_unique<SyntheticCollection>();
  Q_ASSERT(out->root.isValid());
  QDir rootDir(out->root.path());
  rootDir.mkpath(QStringLiteral("media"));
  rootDir.mkpath(QStringLiteral("artwork"));
  const QDir mDir(rootDir.filePath(QStringLiteral("media")));
  const QDir aDir(rootDir.filePath(QStringLiteral("artwork")));
  writeFile(mDir, romFileName, romBytes);
  const QString artworkName = QFileInfo(romFileName).completeBaseName() + QStringLiteral(".png");
  writeFile(aDir, artworkName, QByteArray(64, 'p'));

  out->cfg.name = name;
  out->cfg.mediaDirectory = mDir.absolutePath();
  out->cfg.artworkDirectory = aDir.absolutePath();
  out->cfg.extensions = QStringList{QFileInfo(romFileName).suffix()};
  return out;
}

} // namespace

void TestKartManager::init() {
  QStandardPaths::setTestModeEnabled(true);
  wipeSandbox();
}

void TestKartManager::cleanup() {
  // Same defensive wipe as init() so the next test isn't affected by
  // a half-applied state that crashed before reaching its own init().
  wipeSandbox();
}

void TestKartManager::testImportRoundtripRegistersCollection() {
  // 1. Synthesize a source collection on disk and build a .kart bundle
  // from it via KartWriter — same pipeline production export uses.
  auto src = buildSyntheticCollection(QStringLiteral("Test Audio"), QStringLiteral("clip.bin"),
                                      QByteArray("audio-bytes"));
  auto prep = KartWriter::prepareFromCollection(src->cfg, QStringLiteral("test-uuid-1"), {});
  QVERIFY2(prep.isOk(), qPrintable(prep.isError() ? prep.error().message : QString()));
  // prepareFromCollection defaults to Compression_Zstd; pick Zlib when CI
  // builds without zstd so the writer doesn't error out on the codec gate.
  prep.value().preferredCompression = KartCompression::zstdAvailable()
                                          ? KartFormat::Compression_Zstd
                                          : KartFormat::Compression_Zlib;

  QTemporaryDir kartHome;
  QVERIFY(kartHome.isValid());
  const QString kartPath = QDir(kartHome.path()).filePath(QStringLiteral("test.kart"));
  KartWriter::Writer writer;
  auto writeRes = writer.writeKart(kartPath, prep.value());
  QVERIFY2(writeRes.isOk(), qPrintable(writeRes.isError() ? writeRes.error().message : QString()));
  QVERIFY(QFile::exists(kartPath));

  // 2. Spin up a full ApplicationManager and grab the KartManager. The
  // initialize() call wires settings, session, database, and the kart
  // sub-manager against the qttest sandbox, so register paths land
  // somewhere safe to inspect after the import.
  // appCtx declared BEFORE the manager: ~ApplicationManager nulls the
  // ctx->managers.* slots, so the context must outlive it (Kartend-w06qp).
  ApplicationContext appCtx;
  ApplicationManager appManager;
  appManager.initialize(&appCtx);
  kart::KartManager *kartMgr = appManager.getKartManager();
  QVERIFY2(kartMgr != nullptr, "ApplicationManager must hand out a KartManager");

  // 3. Import to a separate destination directory so the test verifies
  // the manager unpacked files there (not silently into the source).
  QTemporaryDir destRoot;
  QVERIFY(destRoot.isValid());
  QSignalSpy importedSpy(kartMgr, &kart::KartManager::collectionImported);
  auto importRes = kartMgr->importKartHeadless(kartPath, destRoot.path(),
                                               /*registerCollection=*/false,
                                               /*headlessChoice=*/kart::MergeChoice::Skip);
  QVERIFY2(importRes.isOk(),
           qPrintable(importRes.isError() ? importRes.error().message : QString()));
  // The headless import returns the on-disk path of the imported
  // collection bundle (under destRoot). Confirm the path exists and
  // contains a media directory with our test ROM.
  const QString importedRoot = importRes.value();
  QVERIFY2(!importedRoot.isEmpty(), "importKartHeadless returned an empty path");
  QVERIFY(QFile::exists(QDir(importedRoot).filePath(QStringLiteral("media/clip.bin"))));
}

void TestKartManager::testExportProducesReadableBundle() {
  // ApplicationManager constructs KartManager but doesn't call
  // setupReferences on it — that's MainWindow's job. For an integration
  // test we'd otherwise need the full MainWindowFixture; instead, wire a
  // minimal KartManagerSetup by hand. The export path reads
  // setup.getCollections() to resolve the collection index, plus ctx for
  // sibling-manager access (settings, sessions). ApplicationManager
  // wires the ctx via initialize(), so the manual setup here just
  // populates the getter closures the export flow consults.
  // appCtx declared BEFORE the manager: ~ApplicationManager nulls the
  // ctx->managers.* slots, so the context must outlive it (Kartend-w06qp).
  ApplicationContext appCtx;
  ApplicationManager appManager;
  appManager.initialize(&appCtx);

  auto src = buildSyntheticCollection(QStringLiteral("Test Video"), QStringLiteral("clip.mp4"),
                                      QByteArray("video-bytes"));
  QList<CollectionConfig> collections{src->cfg};

  kart::KartManager *kartMgr = appManager.getKartManager();
  QVERIFY(kartMgr != nullptr);
  kart::KartManagerSetup setup;
  setup.ctx = &appCtx;
  setup.getCollections = [&collections]() -> QList<CollectionConfig> * { return &collections; };
  setup.getLauncherPresets = []() -> QList<LauncherPreset> { return {}; };
  setup.getParentWindow = []() -> QWidget * { return nullptr; };
  setup.getPlaylistManager = []() -> IPlaylistManager * { return nullptr; };
  kartMgr->setupReferences(setup);

  QTemporaryDir outRoot;
  QVERIFY(outRoot.isValid());
  const QString outPath = QDir(outRoot.path()).filePath(QStringLiteral("export.kart"));
  auto exportRes = kartMgr->exportCollection(/*collectionIndex=*/0, outPath);
  QVERIFY2(exportRes.isOk(),
           qPrintable(exportRes.isError() ? exportRes.error().message : QString()));
  QVERIFY(QFile::exists(outPath));

  // Re-open the bundle with KartReader and confirm the manifest
  // round-trips the collection name + the single media item.
  auto manifestRes = KartReader::peekManifest(outPath);
  QVERIFY2(manifestRes.isOk(),
           qPrintable(manifestRes.isError() ? manifestRes.error().message : QString()));
  const auto &manifest = manifestRes.value();
  QCOMPARE(manifest.collectionConfig.name, QStringLiteral("Test Video"));
  QCOMPARE(manifest.items.size(), 1);
  QCOMPARE(manifest.items.first().mediaPath, QStringLiteral("media/clip.mp4"));
}

void TestKartManager::testFullRoundtripExportThenImport() {
  // 1. Synthesize a source collection.
  auto src = buildSyntheticCollection(QStringLiteral("Roundtrip"), QStringLiteral("song.flac"),
                                      QByteArray("audio-payload"));
  QList<CollectionConfig> collections{src->cfg};

  // 2. Spin up KartManager wired to the synthetic collection.
  // appCtx declared BEFORE the manager: ~ApplicationManager nulls the
  // ctx->managers.* slots, so the context must outlive it (Kartend-w06qp).
  ApplicationContext appCtx;
  ApplicationManager appManager;
  appManager.initialize(&appCtx);
  kart::KartManager *kartMgr = appManager.getKartManager();
  QVERIFY(kartMgr != nullptr);
  kart::KartManagerSetup setup;
  setup.ctx = &appCtx;
  setup.getCollections = [&collections]() -> QList<CollectionConfig> * { return &collections; };
  setup.getLauncherPresets = []() -> QList<LauncherPreset> { return {}; };
  setup.getParentWindow = []() -> QWidget * { return nullptr; };
  setup.getPlaylistManager = []() -> IPlaylistManager * { return nullptr; };
  kartMgr->setupReferences(setup);

  // 3. Export → .kart.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString kartPath = QDir(tmp.path()).filePath(QStringLiteral("roundtrip.kart"));
  auto exportRes = kartMgr->exportCollection(/*collectionIndex=*/0, kartPath);
  QVERIFY2(exportRes.isOk(),
           qPrintable(exportRes.isError() ? exportRes.error().message : QString()));
  QVERIFY(QFile::exists(kartPath));

  // 4. Re-import to a fresh destination. The same KartManager handles
  // both — production allows that (export-then-import in one session is
  // a supported user flow).
  QTemporaryDir destRoot;
  QVERIFY(destRoot.isValid());
  auto importRes = kartMgr->importKartHeadless(kartPath, destRoot.path(),
                                               /*registerCollection=*/false,
                                               /*headlessChoice=*/kart::MergeChoice::Skip);
  QVERIFY2(importRes.isOk(),
           qPrintable(importRes.isError() ? importRes.error().message : QString()));
  // Reimport must yield the same on-disk media file under the imported
  // root. Byte-identity confirms the writer + reader stayed in sync end
  // to end.
  const QString importedRoot = importRes.value();
  QVERIFY(!importedRoot.isEmpty());
  const QString importedMedia = QDir(importedRoot).filePath(QStringLiteral("media/song.flac"));
  QVERIFY2(QFile::exists(importedMedia), qPrintable(importedMedia));
  QFile f(importedMedia);
  QVERIFY(f.open(QIODevice::ReadOnly));
  QCOMPARE(f.readAll(), QByteArray("audio-payload"));
}

void TestKartManager::testInteractiveFlowsUseStubbedDialogRunners() {
  // Kartend-sqoq0: the interactive entry points used to construct
  // QFileDialog / QMessageBox directly, which made them untestable
  // headlessly. With DialogRunners stubs wired, the whole flow runs
  // without a modal and the warnings land in the stub.
  ApplicationContext appCtx;
  ApplicationManager appManager;
  appManager.initialize(&appCtx);
  kart::KartManager *kartMgr = appManager.getKartManager();
  QVERIFY(kartMgr != nullptr);

  QList<CollectionConfig> collections; // deliberately empty
  QStringList warnings;                // "title|text" per warn call
  QString nextOpenFile;                // canned picker answer

  kart::KartManagerSetup setup;
  setup.ctx = &appCtx;
  setup.getCollections = [&collections]() -> QList<CollectionConfig> * { return &collections; };
  setup.getLauncherPresets = []() -> QList<LauncherPreset> { return {}; };
  setup.getParentWindow = []() -> QWidget * { return nullptr; };
  setup.getPlaylistManager = []() -> IPlaylistManager * { return nullptr; };
  setup.dialogs.warn = [&warnings](const QString &title, const QString &text) {
    warnings.append(title + QStringLiteral("|") + text);
  };
  setup.dialogs.getOpenFileName = [&nextOpenFile](const QString &, const QString &,
                                                  const QString &) { return nextOpenFile; };
  kartMgr->setupReferences(setup);

  // 1. Export with an out-of-range index → the warn runner receives the
  // "No collection selected" failure; nothing modal, nothing exported.
  kartMgr->exportCollectionInteractive(/*collectionIndex=*/5);
  QCOMPARE(warnings.size(), 1);
  QVERIFY(warnings.first().startsWith(QStringLiteral("Export Kart|")));
  warnings.clear();

  // 2. Import with the picker stubbed to "cancel" (empty path) → clean
  // no-op: no warning, no importFailed.
  QSignalSpy failedSpy(kartMgr, &kart::KartManager::importFailed);
  nextOpenFile.clear();
  kartMgr->importInteractive();
  QCOMPARE(warnings.size(), 0);
  QCOMPARE(failedSpy.count(), 0);

  // 3. Import with the picker stubbed to a nonexistent bundle → the
  // manifest peek fails, importFailed fires, and the failure text reaches
  // the warn runner instead of a QMessageBox.
  nextOpenFile = QStringLiteral("/nonexistent/bundle.kart");
  kartMgr->importInteractive();
  QCOMPARE(failedSpy.count(), 1);
  QCOMPARE(warnings.size(), 1);
  QVERIFY(warnings.first().startsWith(QStringLiteral("Import Kart|")));
}

void TestKartManager::testImportWithUnknownLauncherPathStillSucceeds() {
  // 1. Synthesize a collection whose launcher.launcherPath points at an
  // absolute location that doesn't exist on this test host — the
  // canonical "imported from another machine" case the bd's
  // missing-launcher acceptance criterion covers.
  auto src = buildSyntheticCollection(QStringLiteral("Cross Host"), QStringLiteral("clip.bin"),
                                      QByteArray("bytes"));
  src->cfg.launcher.launcherPath = QStringLiteral("/nonexistent/launchers/never-installed-runner");

  // 2. Build the .kart bundle with that config.
  auto prep = KartWriter::prepareFromCollection(src->cfg, QStringLiteral("cross-host-uuid"), {});
  QVERIFY2(prep.isOk(), qPrintable(prep.isError() ? prep.error().message : QString()));
  prep.value().preferredCompression = KartCompression::zstdAvailable()
                                          ? KartFormat::Compression_Zstd
                                          : KartFormat::Compression_Zlib;
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString kartPath = QDir(tmp.path()).filePath(QStringLiteral("crosshost.kart"));
  KartWriter::Writer writer;
  auto writeRes = writer.writeKart(kartPath, prep.value());
  QVERIFY2(writeRes.isOk(), qPrintable(writeRes.isError() ? writeRes.error().message : QString()));

  // 3. Import; expectation is success (the manager doesn't refuse the
  // import just because the launcher isn't on this host — it preserves
  // the configured path so the user can fix it after install or on
  // re-import to the target machine).
  // appCtx declared BEFORE the manager: ~ApplicationManager nulls the
  // ctx->managers.* slots, so the context must outlive it (Kartend-w06qp).
  ApplicationContext appCtx;
  ApplicationManager appManager;
  appManager.initialize(&appCtx);
  kart::KartManager *kartMgr = appManager.getKartManager();
  QVERIFY(kartMgr != nullptr);
  QTemporaryDir destRoot;
  QVERIFY(destRoot.isValid());
  auto importRes = kartMgr->importKartHeadless(kartPath, destRoot.path(),
                                               /*registerCollection=*/false,
                                               /*headlessChoice=*/kart::MergeChoice::Skip);
  QVERIFY2(importRes.isOk(),
           qPrintable(importRes.isError() ? importRes.error().message : QString()));

  // 4. Confirm the imported manifest still carries the original
  // (broken) launcher path — preserved verbatim is the contract the
  // bd's missing-launcher case is testing.
  auto manifestRes = KartReader::peekManifest(kartPath);
  QVERIFY(manifestRes.isOk());
  QCOMPARE(manifestRes.value().collectionConfig.launcher.launcherPath,
           QStringLiteral("/nonexistent/launchers/never-installed-runner"));
}

void TestKartManager::testImportFiresMissingLauncherPathsReporter() {
  // Same cross-host fixture as testImportWithUnknownLauncherPathStillSucceeds
  // — bundle with a launcherPath that won't resolve on this host.
  auto src = buildSyntheticCollection(QStringLiteral("Reporter Fixture"),
                                      QStringLiteral("clip.bin"), QByteArray("bytes"));
  src->cfg.launcher.launcherPath = QStringLiteral("/nonexistent/launchers/never-installed-runner");
  auto prep = KartWriter::prepareFromCollection(src->cfg, QStringLiteral("reporter-uuid"), {});
  QVERIFY2(prep.isOk(), qPrintable(prep.isError() ? prep.error().message : QString()));
  prep.value().preferredCompression = KartCompression::zstdAvailable()
                                          ? KartFormat::Compression_Zstd
                                          : KartFormat::Compression_Zlib;
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString kartPath = QDir(tmp.path()).filePath(QStringLiteral("reporter.kart"));
  KartWriter::Writer writer;
  auto writeRes = writer.writeKart(kartPath, prep.value());
  QVERIFY2(writeRes.isOk(), qPrintable(writeRes.isError() ? writeRes.error().message : QString()));

  // appCtx declared BEFORE the manager: ~ApplicationManager nulls the
  // ctx->managers.* slots, so the context must outlive it (Kartend-w06qp).
  ApplicationContext appCtx;
  ApplicationManager appManager;
  appManager.initialize(&appCtx);
  kart::KartManager *kartMgr = appManager.getKartManager();
  QVERIFY(kartMgr != nullptr);

  // Wire the reporter and capture the args it receives. KartManagerSetup
  // is normally populated by MainWindow; the rest of the fields stay
  // default-empty because the headless import path doesn't consult them.
  int reporterCalls = 0;
  QString reportedCollectionName;
  QList<kart::MissingLauncherPathIssue> reportedIssues;
  kart::KartManagerSetup setup;
  setup.ctx = &appCtx;
  setup.missingLauncherPathsReporter = [&](const QString &name,
                                           const QList<kart::MissingLauncherPathIssue> &issues) {
    ++reporterCalls;
    reportedCollectionName = name;
    reportedIssues = issues;
  };
  kartMgr->setupReferences(setup);

  QTemporaryDir destRoot;
  QVERIFY(destRoot.isValid());
  auto importRes = kartMgr->importKartHeadless(kartPath, destRoot.path(),
                                               /*registerCollection=*/false,
                                               /*headlessChoice=*/kart::MergeChoice::Skip);
  QVERIFY2(importRes.isOk(),
           qPrintable(importRes.isError() ? importRes.error().message : QString()));

  QCOMPARE(reporterCalls, 1);
  QCOMPARE(reportedCollectionName, QStringLiteral("Reporter Fixture"));
  QCOMPARE(reportedIssues.size(), 1);
  QCOMPARE(reportedIssues.first().first, QStringLiteral("launcherPath"));
  // Description is "'<path>' <reason>" — assert the path is in the
  // description so a user reading the dialog can identify which entry
  // to fix.
  QVERIFY(reportedIssues.first().second.contains(
      QStringLiteral("/nonexistent/launchers/never-installed-runner")));
}

void TestKartManager::testImportSkipsReporterWhenAllLauncherPathsResolve() {
  // Build a collection whose launcher path is empty — the canonical
  // "no launcher configured yet" state. Empty maps to PathStatus::Empty
  // which the probe explicitly treats as OK (not a missing path).
  auto src = buildSyntheticCollection(QStringLiteral("Empty Launcher"), QStringLiteral("clip.bin"),
                                      QByteArray("bytes"));
  src->cfg.launcher.launcherPath = QString();
  auto prep =
      KartWriter::prepareFromCollection(src->cfg, QStringLiteral("empty-launcher-uuid"), {});
  QVERIFY(prep.isOk());
  prep.value().preferredCompression = KartCompression::zstdAvailable()
                                          ? KartFormat::Compression_Zstd
                                          : KartFormat::Compression_Zlib;
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString kartPath = QDir(tmp.path()).filePath(QStringLiteral("empty.kart"));
  KartWriter::Writer writer;
  QVERIFY(writer.writeKart(kartPath, prep.value()).isOk());

  // appCtx declared BEFORE the manager: ~ApplicationManager nulls the
  // ctx->managers.* slots, so the context must outlive it (Kartend-w06qp).
  ApplicationContext appCtx;
  ApplicationManager appManager;
  appManager.initialize(&appCtx);
  kart::KartManager *kartMgr = appManager.getKartManager();
  QVERIFY(kartMgr != nullptr);
  int reporterCalls = 0;
  kart::KartManagerSetup setup;
  setup.ctx = &appCtx;
  setup.missingLauncherPathsReporter =
      [&](const QString &, const QList<kart::MissingLauncherPathIssue> &) { ++reporterCalls; };
  kartMgr->setupReferences(setup);

  QTemporaryDir destRoot;
  QVERIFY(destRoot.isValid());
  auto importRes = kartMgr->importKartHeadless(kartPath, destRoot.path(),
                                               /*registerCollection=*/false,
                                               /*headlessChoice=*/kart::MergeChoice::Skip);
  QVERIFY(importRes.isOk());
  QCOMPARE(reporterCalls, 0);
}

namespace {
// Build a .kart whose primary launcherPath resolves inside `extractRoot` (the
// directory the import will extract into), simulating a malicious kart that
// bundles its own executable and points the launcher at it. The bundled file
// need not exist — the gate fires pre-launch. Returns the on-disk .kart path
// (under `kartHome`).
QString buildKartWithInTreeLauncher(const QString &extractRoot, QTemporaryDir &kartHome) {
  auto src = buildSyntheticCollection(QStringLiteral("Bundled Launcher"),
                                      QStringLiteral("clip.bin"), QByteArray("bytes"));
  // Derive from the canonical root so the in-tree prefix compare is exact even
  // when the temp dir lives under a symlinked path.
  const QString canonRoot = QFileInfo(extractRoot).canonicalFilePath();
  src->cfg.launcher.launcherPath = canonRoot + QStringLiteral("/payload/bundled-runner");
  auto prep = KartWriter::prepareFromCollection(src->cfg, QStringLiteral("bundled-uuid"), {});
  if (prep.isError()) return QString();
  prep.value().preferredCompression = KartCompression::zstdAvailable()
                                          ? KartFormat::Compression_Zstd
                                          : KartFormat::Compression_Zlib;
  const QString kartPath = QDir(kartHome.path()).filePath(QStringLiteral("bundled.kart"));
  KartWriter::Writer writer;
  if (writer.writeKart(kartPath, prep.value()).isError()) return QString();
  return kartPath;
}
} // namespace

void TestKartManager::testHeadlessImportRefusesInTreeLauncherByDefault() {
  QTemporaryDir destRoot;
  QVERIFY(destRoot.isValid());
  QTemporaryDir kartHome;
  QVERIFY(kartHome.isValid());
  const QString kartPath = buildKartWithInTreeLauncher(destRoot.path(), kartHome);
  QVERIFY(!kartPath.isEmpty());

  // appCtx declared BEFORE the manager: ~ApplicationManager nulls the
  // ctx->managers.* slots, so the context must outlive it (Kartend-w06qp).
  ApplicationContext appCtx;
  ApplicationManager appManager;
  appManager.initialize(&appCtx);
  kart::KartManager *kartMgr = appManager.getKartManager();
  QVERIFY(kartMgr != nullptr);

  // Default (allowUntrustedLauncher omitted == false) → refused.
  auto res = kartMgr->importKartHeadless(kartPath, destRoot.path(),
                                         /*registerCollection=*/false,
                                         /*headlessChoice=*/kart::MergeChoice::Skip);
  QVERIFY2(res.isError(), "in-tree launcher import must be refused without opt-in");
  // The error must point the operator at the opt-in flag.
  QVERIFY(res.error().details.contains(QStringLiteral("--allow-untrusted-launcher")));
}

void TestKartManager::testHeadlessImportAcceptsInTreeLauncherWhenOptedIn() {
  QTemporaryDir destRoot;
  QVERIFY(destRoot.isValid());
  QTemporaryDir kartHome;
  QVERIFY(kartHome.isValid());
  const QString kartPath = buildKartWithInTreeLauncher(destRoot.path(), kartHome);
  QVERIFY(!kartPath.isEmpty());

  // appCtx declared BEFORE the manager: ~ApplicationManager nulls the
  // ctx->managers.* slots, so the context must outlive it (Kartend-w06qp).
  ApplicationContext appCtx;
  ApplicationManager appManager;
  appManager.initialize(&appCtx);
  kart::KartManager *kartMgr = appManager.getKartManager();
  QVERIFY(kartMgr != nullptr);

  // Explicit opt-in → import proceeds (the warning is logged, not blocking).
  auto res = kartMgr->importKartHeadless(kartPath, destRoot.path(),
                                         /*registerCollection=*/false,
                                         /*headlessChoice=*/kart::MergeChoice::Skip,
                                         /*allowUntrustedLauncher=*/true);
  QVERIFY2(res.isOk(), qPrintable(res.isError() ? res.error().message : QString()));
}
