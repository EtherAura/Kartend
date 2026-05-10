#include "mockedmainwindowfixture.h"

#include "applicationmanager.h"
#include "mockdatabasemanager.h"
#include "mocksettingsmanager.h"

namespace KartendTest {

MockedMainWindowFixture::MockedMainWindowFixture() {
  ApplicationManager::setDatabaseManagerFactory(
      [](const ApplicationContext *, QObject *parent) -> std::unique_ptr<IDatabaseManager> {
        return std::make_unique<MockDatabaseManager>(parent);
      });
  ApplicationManager::setSettingsManagerFactory(
      [](const ApplicationContext *, QObject *parent) -> std::unique_ptr<ISettingsManager> {
        return std::make_unique<MockSettingsManager>(parent);
      });
  m_inner = std::make_unique<MainWindowFixture>();
}

MockedMainWindowFixture::~MockedMainWindowFixture() {
  m_inner.reset();
  ApplicationManager::setDatabaseManagerFactory(nullptr);
  ApplicationManager::setSettingsManagerFactory(nullptr);
}

} // namespace KartendTest
