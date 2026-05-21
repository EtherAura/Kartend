#ifndef DATASOURCEMANAGER_H
#define DATASOURCEMANAGER_H

#include <memory>
#include <QObject>

class FilterManager;
class ScrollDataStore;
class PreSearchStateCache;
class SearchLoadingOverlay;
class QWidget;
struct ApplicationContext;

/**
 * @brief Owns the data-source sub-managers extracted from ScrollManager
 *.
 *
 * Bundles four concerns previously held directly by ScrollManager:
 *  - FilterManager (search/subcollection filtering)
 *  - ScrollDataStore (file paths, names, subcollections, virtual folders)
 *  - PreSearchStateCache (saved widget state for fast search restoration)
 *  - SearchLoadingOverlay (visual feedback during searches)
 *
 * ScrollManager keeps thin raw aliases (m_filterManager, m_dataManager,
 * m_preSearchStateManager, m_searchLoadingOverlay) into this manager so the
 * filter/data update logic in scrollmanagerfilter.cpp continues to work
 * unchanged.
 *
 * Memory ownership: all four sub-managers are owned via unique_ptr.
 */
class DataSourceCoordinator : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(DataSourceCoordinator)
public:
  explicit DataSourceCoordinator(QObject *parent = nullptr);
  ~DataSourceCoordinator() override;

  // ─────────────────────────────────────────────────────────────────────
  // Sub-object access (raw, non-owning pointers)
  // ─────────────────────────────────────────────────────────────────────

  [[nodiscard]] FilterManager *filterManager() const { return m_filterManager.get(); }
  [[nodiscard]] ScrollDataStore *dataManager() const { return m_dataManager.get(); }
  [[nodiscard]] PreSearchStateCache *preSearchStateManager() const {
    return m_preSearchStateManager.get();
  }
  [[nodiscard]] SearchLoadingOverlay *searchLoadingOverlay() const {
    return m_searchLoadingOverlay.get();
  }

  // ─────────────────────────────────────────────────────────────────────
  // Configuration helpers
  // ─────────────────────────────────────────────────────────────────────

  /// Kartend-davi: forwards the ApplicationContext to FilterManager so it
  /// can read its sibling-manager pointers (currently IDatabaseManager)
  /// through ctx instead of caching the raw pointer.
  void setApplicationContext(const ApplicationContext *ctx);
  /// Sets the parent widget for the search loading overlay (typically the
  /// scroll-area viewport).
  void setSearchOverlayParent(QWidget *parent);

  // ─────────────────────────────────────────────────────────────────────
  // Thin facade helpers used directly by ScrollManager
  // ─────────────────────────────────────────────────────────────────────

  void showSearchLoadingOverlay();
  void hideSearchLoadingOverlay();

  [[nodiscard]] bool hasPreSearchState() const;

  /// Returns the actual data index for a visual index when a filter is
  /// active; otherwise returns visualIndex unchanged.
  [[nodiscard]] int getFilteredIndex(int visualIndex) const;

signals:
  /// Re-emitted from FilterManager so ScrollManager can forward outward.
  void filterChanged(int visibleItems, int totalOriginal);

private:
  std::unique_ptr<FilterManager> m_filterManager;
  std::unique_ptr<ScrollDataStore> m_dataManager;
  std::unique_ptr<PreSearchStateCache> m_preSearchStateManager;
  std::unique_ptr<SearchLoadingOverlay> m_searchLoadingOverlay;
};

#endif // DATASOURCEMANAGER_H
