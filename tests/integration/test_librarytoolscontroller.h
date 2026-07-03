/**
 * @file test_librarytoolscontroller.h
 * @brief Integration tests for LibraryToolsController (src/core/librarytoolscontroller.cpp).
 *
 * The controller runs standalone against a closure context (it mirrors
 * ScraperController / DatAuditController's shape). Covered: the shared
 * withActiveCollectionItems() guard every tool flow funnels through — the
 * "open a collection first" prompt (routed via the ctx DialogRunners so no
 * modal opens headlessly), the guard's ordering (no database resolution
 * before a collection is active), and the silent stop on a null database
 * manager. The tool dialogs themselves (bulk edit, health report, wizards)
 * run nested exec loops and are exercised by their own dialog-level suites.
 */

#ifndef KARTEND_TESTS_TEST_LIBRARYTOOLSCONTROLLER_H
#define KARTEND_TESTS_TEST_LIBRARYTOOLSCONTROLLER_H

#include <QObject>

class TestLibraryToolsController : public QObject {
  Q_OBJECT

private slots:
  void toolFlows_withoutCollections_promptToOpenOne();
  void toolFlows_withOutOfRangeIndex_promptToOpenOne();
  void toolFlows_withoutActiveCollection_skipDatabaseResolution();
  void toolFlows_withNullDatabaseManager_stopSilently();
};

#endif // KARTEND_TESTS_TEST_LIBRARYTOOLSCONTROLLER_H
