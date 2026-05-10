#ifndef KARTEND_TESTS_MOCKEDMAINWINDOWFIXTURE_H
#define KARTEND_TESTS_MOCKEDMAINWINDOWFIXTURE_H

#include "mainwindowfixture.h"

#include <memory>

class MainWindow;

namespace KartendTest {

/**
 * MainWindowFixture variant that installs MockDatabaseManager and
 * MockSettingsManager factories on ApplicationManager before constructing the
 * window, then clears them on destruction.
 *
 * Use this in tests that don't exercise the persistence layer — UI state
 * coordinators (ScrollManager, NavigationManager, layout flags) work the same
 * against the mocks but don't pay for SQLite/QSettings setup, and never hit
 * the qttest sandbox filesystem.
 *
 * Construction order is significant: the factory hooks must be set before
 * MainWindow construction (which builds ApplicationManager synchronously),
 * and reset only after the MainWindow has been destroyed (so nothing else
 * in this binary inadvertently picks up a mock). Both rules are honoured by
 * the inner-fixture pointer's RAII order here.
 */
class MockedMainWindowFixture {
public:
  MockedMainWindowFixture();
  ~MockedMainWindowFixture();

  MockedMainWindowFixture(const MockedMainWindowFixture &) = delete;
  MockedMainWindowFixture &operator=(const MockedMainWindowFixture &) = delete;

  [[nodiscard]] MainWindow *window() const { return m_inner->window(); }

private:
  std::unique_ptr<MainWindowFixture> m_inner;
};

} // namespace KartendTest

#endif // KARTEND_TESTS_MOCKEDMAINWINDOWFIXTURE_H
