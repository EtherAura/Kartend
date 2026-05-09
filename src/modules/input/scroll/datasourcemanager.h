#ifndef DATASOURCEMANAGER_H
#define DATASOURCEMANAGER_H

#include <memory>
#include <QObject>

class FilterManager;
class ScrollDataManager;
class PreSearchStateManager;
class SearchLoadingOverlay;
class DatabaseManager;
class QWidget;

/**
 * @brief Owns the data-source sub-managers extracted from ScrollManager
 *.
 *
 * Bundles four concerns previously held directly by ScrollManager:
 *  - FilterManager (search/subcollection filtering)
 *  - ScrollDataManager (file paths, names, subcollections, virtual folders)
 *  - PreSearchStateManager (saved widget state for fast search restoration)
 *  - SearchLoadingOverlay (visual feedback during searches)
 *
 * ScrollManager keeps thin raw aliases (m_filterManager, m_dataManager,
 * m_preSearchStateManager, m_searchLoadingOverlay) into this manager so the
 * filter/data update logic in scrollmanagerfilter.cpp continues to work
 * unchanged.
 *
 * Memory ownership: all four sub-managers are owned via unique_ptr.
 */
class DataSourceManager : public QObject {
  Q_OBJECT
public:
  explicit DataSourceManager(QObject *parent = nullptr);
  ~DataSourceManager() override;

  // ─────────────────────────────────────────────────────────────────────
  // Sub-object access (raw, non-owning pointers)
  // ─────────────────────────────────────────────────────────────────────

  [[nodiscard]] FilterManager *filterManager() const { return m_filterManager.get(); }
  [[nodiscard]] ScrollDataManager *dataManager() const { return m_dataManager.get(); }
  [[nodiscard]] PreSearchStateManager *preSearchStateManager() const {
    return m_preSearchStateManager.get();
  }
  [[nodiscard]] SearchLoadingOverlay *searchLoadingOverlay() const {
    return m_searchLoadingOverlay.get();
  }

  // ─────────────────────────────────────────────────────────────────────
  // Configuration helpers
  // ─────────────────────────────────────────────────────────────────────

  /// Forwards a database-manager pointer to FilterManager. ScrollManager
  /// receives the database manager later than its sub-managers are constructed.
  void setDatabaseManager(DatabaseManager *manager);
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
  std::unique_ptr<ScrollDataManager> m_dataManager;
  std::unique_ptr<PreSearchStateManager> m_preSearchStateManager;
  std::unique_ptr<SearchLoadingOverlay> m_searchLoadingOverlay;
};

#endif // DATASOURCEMANAGER_H
