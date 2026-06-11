#ifndef KARTEND_TESTS_SUPPORT_TESTSANDBOX_H
#define KARTEND_TESTS_SUPPORT_TESTSANDBOX_H

// Kartend-9o2a9: the shared initTestCase() sandbox preamble.
//
// Suites that exercise production code resolving paths via QStandardPaths
// (media.db under AppDataLocation, config INIs, scrape logs, ...) all open
// with the same three lines: enable QStandardPaths test mode so every
// writableLocation lands under the disposable qttest sandbox, then give the
// process a per-suite application name so parallel ctest binaries get
// disjoint sandbox subdirectories. This helper replaces the verbatim copies.
//
// Suites with extra preamble needs (sandbox wipes, qFatal escape guards,
// QTemporaryDir homes) intentionally keep their own variants — see e.g.
// tests/integration/mainwindowfixture.cpp.

#include <QCoreApplication>
#include <QStandardPaths>
#include <QString>

namespace KartendTest {

/// Call from initTestCase(). @p appName should be unique per suite
/// (convention: "kartend-test-<suite>").
inline void initSandboxedTestCase(const QString &appName) {
  QStandardPaths::setTestModeEnabled(true);
  QCoreApplication::setOrganizationName(QStringLiteral("Kartend"));
  QCoreApplication::setApplicationName(appName);
}

} // namespace KartendTest

#endif // KARTEND_TESTS_SUPPORT_TESTSANDBOX_H
