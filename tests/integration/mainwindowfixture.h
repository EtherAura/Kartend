/**
 * @file mainwindowfixture.h
 * @brief Test fixture that builds a MainWindow without touching real user data.
 *
 * Uses QStandardPaths::setTestModeEnabled(true) so settings, session, cache,
 * and database files all live inside the per-user qttest sandbox
 * (~/.qttest/... on Linux) and are wiped on construction. Construct one
 * fixture per test method.
 */

#ifndef KARTEND_TESTS_MAINWINDOWFIXTURE_H
#define KARTEND_TESTS_MAINWINDOWFIXTURE_H

#include <memory>
#include <QHash>
#include <QString>

class MainWindow;

namespace KartendTest {

/// Snapshot of the real-user kartend config / data directories taken
/// outside test mode. The fixture captures one at construction and
/// re-captures + diffs at destruction so any write that escaped the
/// QStandardPaths sandbox (e.g. an explicit absolute path bypassing
/// setTestModeEnabled) aborts the run instead of silently corrupting
/// the developer's real config (Kartend-jcj7).
struct RealUserPathSnapshot {
  QString configDir;
  QString appConfigDir;
  QString appDataDir;
  /// path -> (size, mtime_msecs); empty when the directory didn't exist.
  QHash<QString, QPair<qint64, qint64>> entries;
};

/**
 * RAII fixture that constructs a fully-wired MainWindow against an isolated
 * QStandardPaths sandbox.
 *
 * Requirements:
 * - A QApplication must already exist (created once in test_main.cpp).
 *
 * The fixture does NOT call show() — tests that need rendering can call
 * window()->show() themselves.
 */
class MainWindowFixture {
public:
  MainWindowFixture();
  ~MainWindowFixture();

  MainWindowFixture(const MainWindowFixture &) = delete;
  MainWindowFixture &operator=(const MainWindowFixture &) = delete;

  [[nodiscard]] MainWindow *window() const { return m_window.get(); }

  /**
   * Returns the qttest config root that the live MainWindow is reading and
   * writing. Useful for assertions that data is actually sandboxed.
   */
  [[nodiscard]] static QString sandboxConfigPath();

private:
  std::unique_ptr<MainWindow> m_window;
  RealUserPathSnapshot m_realPathsBefore;
};

} // namespace KartendTest

#endif // KARTEND_TESTS_MAINWINDOWFIXTURE_H
