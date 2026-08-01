#ifndef KARTEND_TESTS_MOCKSETTINGSMANAGER_H
#define KARTEND_TESTS_MOCKSETTINGSMANAGER_H

#include "isettingsmanager.h"

namespace KartendTest {

/**
 * In-memory ISettingsManager double used by integration tests that don't
 * exercise the QSettings layer.
 *
 * Every method is a no-op by default: loadCollections leaves its argument
 * untouched (unless a seed list was set — see setSeedCollections), saves are
 * dropped. The selection cache is an in-memory QHash so tests that rely on
 * round-tripping last-selected indices still work without disk. One further
 * exception: loadGeneralSettings seeds firstRunComplete=true so the modal
 * first-run wizard never queues under this fixture.
 */
class MockSettingsManager : public ISettingsManager {
  Q_OBJECT
public:
  using ISettingsManager::ISettingsManager;

  void loadCollections(QList<CollectionConfig> &collections) override {
    if (!m_seedCollections.isEmpty()) collections = m_seedCollections;
  }
  /// Seed the list loadCollections hands to MainWindow (empty = no-op, the
  /// historical behavior). Set via MockedMainWindowFixture's seeding ctor
  /// BEFORE the window constructs — loop-pumping tests need a non-empty
  /// library or the empty-collections modal prompt blocks the pumped loop.
  void setSeedCollections(QList<CollectionConfig> seed) { m_seedCollections = std::move(seed); }
  [[nodiscard]] QStringList lastCollectionUuidCollisions() const override { return {}; }
  ErrorUtils::Result<void> saveCollections(const QList<CollectionConfig> &) override {
    ++m_saveCollectionsCalls;
    return ErrorUtils::Result<void>::success();
  }
  /// Number of saveCollections invocations. Burst-coalescing tests assert a
  /// storm of interactive toggles produces one debounced write, not one per
  /// click — the write itself stays dropped like every other mock save.
  [[nodiscard]] int saveCollectionsCalls() const { return m_saveCollectionsCalls; }
  void loadGeneralSettings(GeneralSettings &settings) override {
    // Mirror MainWindowFixture's sandbox pre-seed: without this, the default
    // firstRunComplete=false queues the modal first-run wizard during
    // MainWindow construction, and any test that pumps the event loop then
    // blocks forever inside QDialog::exec().
    settings.startup.firstRunComplete = true;
  }
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
  QList<CollectionConfig> m_seedCollections;
  int m_saveCollectionsCalls = 0;
};

} // namespace KartendTest

#endif // KARTEND_TESTS_MOCKSETTINGSMANAGER_H
