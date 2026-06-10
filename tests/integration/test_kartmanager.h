#ifndef KARTEND_TESTS_TEST_KARTMANAGER_H
#define KARTEND_TESTS_TEST_KARTMANAGER_H

#include <QObject>

class TestKartManager : public QObject {
  Q_OBJECT
private slots:
  void init();
  void cleanup();

  // Kartend-r9u0 — high-impact paths in KartManager that weren't
  // exercised by any integration test until this file landed:
  //
  // testImportRoundtripRegistersCollection: build a .kart bundle from a
  // synthesized CollectionConfig + ROM file, hand it to
  // importKartHeadless, and assert that the manager's collectionImported
  // signal fires AND the resulting on-disk layout matches what
  // KartWriter produced.
  void testImportRoundtripRegistersCollection();
  // testExportProducesReadableBundle: configure a collection with a real
  // media file on disk, call exportCollection, then re-open the produced
  // .kart with KartReader to confirm the manifest carries the
  // collection name + media item the caller asked for.
  void testExportProducesReadableBundle();
  // testFullRoundtripExportThenImport: build a real on-disk collection,
  // export it through KartManager::exportCollection, then re-import the
  // resulting .kart into a fresh destination via importKartHeadless.
  // Asserts the imported on-disk tree mirrors the source's media file.
  void testFullRoundtripExportThenImport();
  // testImportWithUnknownLauncherPathStillSucceeds: build a .kart bundle
  // whose launcher path points at an absolute location that doesn't
  // exist on this host (the typical "imported from another machine"
  // case). importKartHeadless must still succeed — the manager
  // preserves the path verbatim so the user can fix it after import.
  void testImportWithUnknownLauncherPathStillSucceeds();

  // Kartend-jset: the post-import probe must invoke the wired
  // MissingLauncherPathsReporter callback when an imported collection's
  // launcher path doesn't resolve on this host. Asserts the callback
  // fires once with the collection name + a non-empty issue list whose
  // field column references launcherPath.
  void testImportFiresMissingLauncherPathsReporter();

  // Kartend-jset: the inverse — when every imported launcher path
  // resolves on the host (or is empty), the reporter is NOT called.
  // Otherwise users would see a spurious dialog after every benign
  // import.
  void testImportSkipsReporterWhenAllLauncherPathsResolve();

  // Kartend-u8wf0: a headless import whose launcher path resolves inside
  // the extracted kart tree (a self-bundled executable) is refused by
  // default, with an error that names the opt-in flag.
  void testHeadlessImportRefusesInTreeLauncherByDefault();
  // Kartend-u8wf0: the same import succeeds when the caller opts in via
  // allowUntrustedLauncher=true (the CLI's --allow-untrusted-launcher).
  void testHeadlessImportAcceptsInTreeLauncherWhenOptedIn();
};

#endif
