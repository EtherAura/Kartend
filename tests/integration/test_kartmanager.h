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

  // Kartend-sqoq0: the interactive entry points must route their modals
  // through the owner-supplied DialogRunners when wired — no stock
  // QFileDialog / QMessageBox opens. Stubs the file picker to return a
  // canned path / cancel and asserts the warn runner receives the
  // "No collection selected" failure instead of a modal popping.
  void testInteractiveFlowsUseStubbedDialogRunners();

  // Kartend-h7xnr.1: importKartAsync (the drop-drain entry) runs the same
  // QtConcurrent worker path as the menu import — kartProgressStarted /
  // kartProgressFinished bracket each run, operationInFlight() is true
  // while the future is live, and two back-to-back imports of the same
  // bundle (sequenced off collectionImported, exactly how MainWindow's
  // drop chain does it) register both collections with the name-dedup
  // suffix on the second.
  void testImportKartAsyncSequentialImportsRegisterBoth();

  // Kartend-h7xnr.1: the async failure path — a nonexistent bundle — must
  // end in exactly the terminal signals the drop chain sequences on
  // (importFailed + kartProgressFailed), route the message through the
  // warn runner, and leave no operation in flight.
  void testImportKartAsyncFailureEmitsTerminalSignals();

  // Kartend audit jpit3 rework: destroying a KartManager while an async
  // import is in flight must cancel cooperatively and join within the
  // bounded teardown budget (2s + slack) instead of hanging — and must not
  // crash (the worker task owns its state by value/shared_ptr, so even an
  // abandoned task touches nothing of the dead manager).
  void testDestructorBoundsInFlightImportJoin();

  // Kartend-u8wf0: a headless import whose launcher path resolves inside
  // the extracted kart tree (a self-bundled executable) is refused by
  // default, with an error that names the opt-in flag.
  void testHeadlessImportRefusesInTreeLauncherByDefault();
  // Kartend-u8wf0: the same import succeeds when the caller opts in via
  // allowUntrustedLauncher=true (the CLI's --allow-untrusted-launcher).
  void testHeadlessImportAcceptsInTreeLauncherWhenOptedIn();

  // Kartend-kxqqf: the drag-and-drop import path never opens the preflight
  // dialog, so the confirmation hook is the whole gate there. A bundle whose
  // launcher lives in an allowlisted directory but whose arguments carry the
  // payload must still prompt — with the values shown verbatim — and
  // declining must register nothing.
  void testAsyncImportConfirmsBundleLauncherConfiguration();
  // Kartend-kxqqf: the inverse — a bundle that asks to run nothing imports
  // without a prompt, so the warning keeps its meaning.
  void testAsyncImportSkipsConfirmerWithoutLauncherConfiguration();

  // Kartend-qbfk1: every import extracts into a FRESH subdirectory of the
  // caller-chosen parent, named for the bundle — never loose into the parent
  // itself, where a hostile bundle could land dotfile payloads
  // ('.config/autostart/…') among the user's existing files.
  void testImportLandsInFreshBundleNamedSubdir();
  // Kartend-qbfk1: a second import of the same bundle to the same parent
  // finds the subdirectory occupied and is refused — nothing overwritten,
  // the first import left intact.
  void testRepeatImportToSameParentIsRefused();
  // Kartend-qbfk1: the subdirectory derives from the manifest's
  // user-controlled name — separators, Windows segment quirks and reserved
  // device names must come out inert.
  void testImportSubdirNameSanitizesHostileNames();
};

#endif
