#ifndef SELECTIONDISPLAYMANAGER_H
#define SELECTIONDISPLAYMANAGER_H

#include "collectionutils.h"
#include "gridlayoutcalculator.h"
#include <QHash>
#include <QObject>
#include <QString>
#include <memory>

class QWidget;
class QScrollArea;
class ItemWidget;
class ItemWidgetFactory;
class SelectionOverlayManager;
class SelectionStateTracker;
class ListHeaderWidget;
class ArtworkPreviewOverlay;
struct GeneralSettings;
enum class ListSortColumn;

/**
 * @brief Owns selection-related visual elements extracted from ScrollManager.
 *
 * Bundles three concerns previously interleaved in ScrollManager:
 *  - Glide overlay (SelectionOverlayManager)
 *  - Selection state (SelectionStateTracker)
 *  - List view chrome (header widget, column widths, artwork preview overlay)
 *
 * ScrollManager holds a unique_ptr to this class and accesses the inner
 * sub-objects through the overlay()/state()/listHeader() getters. The selection
 * update logic in ScrollManager continues to run there, but routes through
 * these getters instead of owning the sub-objects directly.
 *
 * Memory ownership:
 *  - Owns SelectionOverlayManager and SelectionStateTracker via unique_ptr
 *  - Owns ArtworkPreviewOverlay via unique_ptr (lazy-created on first request)
 *  - ListHeaderWidget is parented to the scroll area's viewport (Qt parent
 *    ownership) and is destroyed via destroyListHeader() before the viewport
 *    goes away.
 *  - Borrowed pointers (scroll area, virtual container, factory, active widgets
 *    map, metrics, context, settings) are not owned.
 */
class SelectionDisplayManager : public QObject {
  Q_OBJECT
public:
  explicit SelectionDisplayManager(QObject *parent = nullptr);
  ~SelectionDisplayManager() override;

  // ─────────────────────────────────────────────────────────────────────
  // Configuration (called from ScrollManager::setupReferences and around
  // virtual container lifecycle)
  // ─────────────────────────────────────────────────────────────────────

  void setMediaScrollArea(QScrollArea *area) { m_mediaScrollArea = area; }
  void setVirtualContainer(QWidget *container) {
    m_virtualContainer = container;
  }
  void setWidgetFactory(ItemWidgetFactory *factory) {
    m_widgetFactory = factory;
  }
  void setActiveWidgets(const QHash<int, ItemWidget *> *widgets) {
    m_activeWidgets = widgets;
  }
  void setMetrics(const GridMetrics *metrics) { m_metrics = metrics; }
  void setCollectionContext(const CollectionContext *context) {
    m_context = context;
  }
  /// Applies persisted column widths from settings, if any.
  void applyGeneralSettings(const GeneralSettings *settings);

  // ─────────────────────────────────────────────────────────────────────
  // Sub-object access (kept stable for ScrollManager's selection logic)
  // ─────────────────────────────────────────────────────────────────────

  [[nodiscard]] SelectionOverlayManager *overlay() const {
    return m_overlay.get();
  }
  [[nodiscard]] SelectionStateTracker *state() const {
    return m_stateTracker.get();
  }
  [[nodiscard]] ListHeaderWidget *listHeader() const { return m_listHeader; }

  [[nodiscard]] int collectionColumnWidth() const {
    return m_collectionColumnWidth;
  }
  [[nodiscard]] int artworkColumnWidth() const { return m_artworkColumnWidth; }

  // ─────────────────────────────────────────────────────────────────────
  // List header lifecycle and rendering
  // ─────────────────────────────────────────────────────────────────────

  /// Creates (lazily) the list header in list-view mode and positions it at
  /// the top of the viewport. Hides it in grid mode.
  void updateListHeader();
  /// Destroys the list header widget. Used by ScrollManager destructor before
  /// the viewport goes away.
  void destroyListHeader();

  // ─────────────────────────────────────────────────────────────────────
  // Artwork preview overlay (list-mode hover)
  // ─────────────────────────────────────────────────────────────────────

  [[nodiscard]] bool isArtworkPreviewVisible() const;
  /// Hides the preview if visible. Returns true if it was visible.
  bool hideArtworkPreview();
  /// Lazy-creates the overlay if needed and shows it for the given file.
  void showArtworkPreview(const QString &filePath, const QString &artworkDir);

signals:
  /// Emitted when the user clicks a list header column to change sort.
  void sortModeChangeRequested(SortMode sortMode);
  /// Emitted when the user resizes the collection column.
  void listColumnWidthChanged(int width);
  /// Emitted when the user resizes the artwork column.
  void listArtworkColumnWidthChanged(int width);

private slots:
  void onListColumnClicked(ListSortColumn column);
  void onListColumnWidthChanged(int collectionWidth);
  void onListArtworkColumnWidthChanged(int artworkWidth);

private:
  // Owned sub-objects
  std::unique_ptr<SelectionOverlayManager> m_overlay;
  std::unique_ptr<SelectionStateTracker> m_stateTracker;
  std::unique_ptr<ArtworkPreviewOverlay> m_artworkPreviewOverlay;
  ListHeaderWidget *m_listHeader = nullptr; // Qt-parented to viewport

  // Borrowed dependencies
  QScrollArea *m_mediaScrollArea = nullptr;
  QWidget *m_virtualContainer = nullptr;
  ItemWidgetFactory *m_widgetFactory = nullptr;
  const QHash<int, ItemWidget *> *m_activeWidgets = nullptr;
  const GridMetrics *m_metrics = nullptr;
  const CollectionContext *m_context = nullptr;

  // List view column widths
  int m_collectionColumnWidth = 150;
  int m_artworkColumnWidth = 32;
};

#endif // SELECTIONDISPLAYMANAGER_H
