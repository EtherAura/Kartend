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

#include <QString>
#include <memory>

class MainWindow;

namespace KartendTest {

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
};

} // namespace KartendTest

#endif // KARTEND_TESTS_MAINWINDOWFIXTURE_H
/**
 * @file mainwindowfixture.h
 * @brief Test fixture that builds a MainWindow without touching real user data.
 *
 * Uses QStandardPaths::setTestModeEnabled(true) plus an isolated temp directory
 * (set as XDG_CONFIG_HOME / XDG_DATA_HOME / XDG_CACHE_HOME) so settings,
 * session, cache, and database files all live inside the fixture's scope and
 * are deleted on teardown. Construct one fixture per test method.
 */

#ifndef KARTEND_TESTS_MAINWINDOWFIXTURE_H
#define KARTEND_TESTS_MAINWINDOWFIXTURE_H

#include <QString>
#include <QTemporaryDir>
#include <memory>

class MainWindow;

namespace KartendTest {

/**
 * RAII fixture that constructs a fully-wired MainWindow against an isolated
 * filesystem sandbox.
 *
 * Requirements:
 * - A QApplication must already exist (created once in test_main.cpp).
 * - Q_OS env vars XDG_*_HOME are overridden for the lifetime of this fixture.
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
  [[nodiscard]] QString sandboxPath() const { return m_sandbox.path(); }

private:
  // Saved env vars so the fixture restores the previous environment on
  // teardown. Tests run in-process and may interleave, so we must not leave
  // XDG paths permanently pointing at our temp dir.
  struct EnvSnapshot {
    QString name;
    bool wasSet;
    QString previousValue;
  };

  void overrideEnv(const QString &name, const QString &value);
  void restoreEnv();

  QTemporaryDir m_sandbox;
  QList<EnvSnapshot> m_envSnapshots;
  std::unique_ptr<MainWindow> m_window;
};

} // namespace KartendTest

#endif // KARTEND_TESTS_MAINWINDOWFIXTURE_H
