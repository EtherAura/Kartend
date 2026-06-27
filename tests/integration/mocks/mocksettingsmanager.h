#ifndef KARTEND_TESTS_MOCKSETTINGSMANAGER_H
#define KARTEND_TESTS_MOCKSETTINGSMANAGER_H

#include "isettingsmanager.h"

namespace KartendTest {

/**
 * In-memory ISettingsManager double used by integration tests that don't
 * exercise the QSettings layer.
 *
 * Every method is a no-op: loadCollections leaves its argument untouched,
 * saves are dropped. The selection cache is an in-memory QHash so tests
 * that rely on round-tripping last-selected indices still work without
 * disk.
 */
class MockSettingsManager : public ISettingsManager {
  Q_OBJECT
public:
  using ISettingsManager::ISettingsManager;

  void loadCollections(QList<CollectionConfig> &) override {}
  [[nodiscard]] QStringList lastCollectionUuidCollisions() const override { return {}; }
  ErrorUtils::Result<void> saveCollections(const QList<CollectionConfig> &) override {
    return ErrorUtils::Result<void>::success();
  }
  void loadGeneralSettings(GeneralSettings &) override {}
  ErrorUtils::Result<void> saveGeneralSettings(const GeneralSettings &) override {
    return ErrorUtils::Result<void>::success();
  }

  void setLastSelectedItem(int collectionIndex, int itemIndex) override {
    m_lastSelected.insert(collectionIndex, itemIndex);
  }
  [[nodiscard]] int getLastSelectedItem(int collectionIndex) const override {
    return m_lastSelected.value(collectionIndex, -1);
  }

  // No keychain in the in-memory double — credentials are never demoted.
  [[nodiscard]] QString credentialDemotionReason() const override { return {}; }

  // The dialog-orchestration methods (openSettingsDialog, handleReloadRequired,
  // handleLayoutChanges) left ISettingsManager for the ui-layer
  // SettingsDialogController in Kartend-q8p29, so there is nothing
  // dialog-shaped to stub here anymore.

private:
  QHash<int, int> m_lastSelected;
};

} // namespace KartendTest

#endif // KARTEND_TESTS_MOCKSETTINGSMANAGER_H
