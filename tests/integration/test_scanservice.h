#ifndef KARTEND_TESTS_TEST_SCANSERVICE_H
#define KARTEND_TESTS_TEST_SCANSERVICE_H

#include <QObject>

class TestScanService : public QObject {
  Q_OBJECT
private slots:
  void init();

  // Kartend-r9u0 — ScanService had no integration-test coverage before
  // this file. These two cover the directory-walk happy paths:
  //
  // testScanMediaDirectoryEmpty: scanMediaDirectory on an empty media
  // directory returns no items and a stable directory signature. Anchors
  // the contract that "no files" is not an error.
  void testScanMediaDirectoryEmpty();
  // testScanMediaDirectoryFindsExtensionMatches: populate the media dir
  // with a mix of extensions and confirm only the extensions listed in
  // CollectionConfig.extensions get reported back.
  void testScanMediaDirectoryFindsExtensionMatches();
  // testRequestCancelScanSetsFlag: requestCancelScan() must flip
  // isScanCancelled() to true — the scan loop polls this flag between
  // batches to bail out without committing. Anchors the contract the
  // cancel-mid-scan UI handler relies on.
  void testRequestCancelScanSetsFlag();
  // testResetScanCancellationClearsFlag: after a cancelled scan
  // settles, the next scan call expects the flag back at false.
  // resetScanCancellation() is the explicit reset entry point. Without
  // this contract a single cancelled scan would poison every subsequent
  // scan in the same session.
  void testResetScanCancellationClearsFlag();
};

#endif
