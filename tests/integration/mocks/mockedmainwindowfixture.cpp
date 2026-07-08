#include "mockedmainwindowfixture.h"

#include "applicationmanager.h"
#include "mockdatabasemanager.h"
#include "mocksettingsmanager.h"

namespace KartendTest {

MockedMainWindowFixture::MockedMainWindowFixture()
    : MockedMainWindowFixture(QList<CollectionConfig>{}) {}

MockedMainWindowFixture::MockedMainWindowFixture(QList<CollectionConfig> seedCollections) {
  ApplicationManager::setDatabaseManagerFactory(
      [](const ApplicationContext *, QObject *parent) -> std::unique_ptr<IDatabaseManager> {
        return std::make_unique<MockDatabaseManager>(parent);
      });
  ApplicationManager::setSettingsManagerFactory(
      [seed = std::move(seedCollections)](const ApplicationContext *,
                                          QObject *parent) -> std::unique_ptr<ISettingsManager> {
        auto mock = std::make_unique<MockSettingsManager>(parent);
        mock->setSeedCollections(seed);
        return mock;
      });
  m_inner = std::make_unique<MainWindowFixture>();
}

MockedMainWindowFixture::~MockedMainWindowFixture() {
  m_inner.reset();
  ApplicationManager::setDatabaseManagerFactory(nullptr);
  ApplicationManager::setSettingsManagerFactory(nullptr);
}

} // namespace KartendTest
