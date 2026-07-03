/**
 * @file test_datauditcontroller.h
 * @brief Integration tests for DatAuditController (src/core/datauditcontroller.cpp).
 *
 * The controller runs standalone against a closure context (it mirrors
 * ScraperController's shape). Covered: the lazily constructed, reused,
 * hide-not-destroy DAT Audit dialog (openDialog / openForCollection), the
 * QPointer cache self-healing after an external dialog destruction, and
 * the no-library-configured guard on the startup scan. The modal library
 * review / import prompts (nested exec loops) are exercised by their own
 * dialog-level suites, not here.
 */

#ifndef KARTEND_TESTS_TEST_DATAUDITCONTROLLER_H
#define KARTEND_TESTS_TEST_DATAUDITCONTROLLER_H

#include <QObject>

class TestDatAuditController : public QObject {
  Q_OBJECT

private slots:
  void openDialog_createsThenReusesOneDialog();
  void openDialog_recreatesAfterExternalDestruction();
  void openForCollection_showsTheSameCachedDialog();
  void startupLibraryScan_withoutLibraryPathIsInert();
};

#endif // KARTEND_TESTS_TEST_DATAUDITCONTROLLER_H
