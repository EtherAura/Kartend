#ifndef KARTEND_TESTS_MOCKSETTINGSMANAGER_H
#define KARTEND_TESTS_MOCKSETTINGSMANAGER_H

#include "isettingsmanager.h"

namespace KartendTest {

/**
 * In-memory ISettingsManager double used by integration tests that don't
 * exercise the QSettings layer.
 *
 * Every method is a no-op: loadCollections leaves its argument untouched,
 * saves are dropped, the settings dialog is a stub. The selection cache is
 * an in-memory QHash so tests that rely on round-tripping last-selected
 * indices still work without disk.
 */
class MockSettingsManager : public ISettingsManager {
  Q_OBJECT
public:
  using ISettingsManager::ISettingsManager;

  void loadCollections(QList<CollectionConfig> &) override {}
  void saveCollections(const QList<CollectionConfig> &) override {}
  void openSettingsDialog(const SettingsDialogContext &) override {}
  void loadGeneralSettings(GeneralSettings &) override {}
  void saveGeneralSettings(const GeneralSettings &) override {}

  void setLastSelectedItem(int collectionIndex, int itemIndex) override {
    m_lastSelected.insert(collectionIndex, itemIndex);
  }
  [[nodiscard]] int getLastSelectedItem(int collectionIndex) const override {
    return m_lastSelected.value(collectionIndex, -1);
  }

  void handleReloadRequired(const QList<CollectionConfig> &, const QList<CollectionConfig> &,
                            const QList<CollectionConfig> &, int, IDetailsPaneManager *,
                            IScrollManager *, INavigationManager *, IArtworkManager *,
                            ICacheManager *, int) override {}

  void handleLayoutChanges(QWidget *, const QList<CollectionConfig> &, int, bool, bool, bool, bool,
                           bool, bool, bool, bool, IDetailsPaneManager *, IScrollManager *,
                           IArtworkManager *, int) override {}

private:
  QHash<int, int> m_lastSelected;
};

} // namespace KartendTest

#endif // KARTEND_TESTS_MOCKSETTINGSMANAGER_H
