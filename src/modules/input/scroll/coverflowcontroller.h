#ifndef COVERFLOWCONTROLLER_H
#define COVERFLOWCONTROLLER_H

#include "collection/collectioncontext.h"
#include <QObject>

class QScrollArea;
class QWidget;
class CoverFlowWidget;
class ScrollDataStore;
class IFilterManager;
#include "applicationcontext_fwd.h"

namespace TimerUtils {
class DebouncedTimer;
}

/**
 * @brief Setup struct for CoverFlowController dependencies.
 *
 * Every field is borrowed — CoverFlowController owns only its CoverFlowWidget
 * (Qt-parented to the items-page content widget) and the resolve debouncer.
 */
struct CoverFlowControllerSetup {
  const ApplicationContext *ctx = nullptr;
  QScrollArea *mediaScrollArea = nullptr;
  QWidget *gridContainer = nullptr;
  const CollectionContext *context = nullptr;
  const QList<CollectionConfig> *collections = nullptr;
  ScrollDataStore *dataManager = nullptr;
  // Kartend-yeik: removed FilterManager *filterManager; CoverFlowController
  // now reads filter state via m_ctx->filterManager(), which the
  // initializeAppContext seed populates from ScrollManager.
};

/**
 * @brief Drives the CoverFlow ViewType — owns the CoverFlowWidget and keeps its
 * card list, appearance config, and visibility in sync with the grid.
 *
 * Extracted from ScrollManager. CoverFlowWidget lives as a sibling of the grid
 * container in the items-page layout; when the active collection's view type is
 * CoverFlow the grid + scrollbars hide and the carousel shows. Selection
 * changes from carousel input round-trip back through SelectionManager via the
 * selectItemByIndex signal so sidebar / restore / persistence stay coherent.
 *
 * Borrows every dependency; the per-item preview video + artwork gallery
 * lookups are resolved lazily and debounced so a wheel sweep across the
 * carousel triggers one DB + filesystem pass at the trailing edge.
 */
class CoverFlowController : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(CoverFlowController)
public:
  explicit CoverFlowController(QObject *parent = nullptr);
  ~CoverFlowController() override = default;

  void setupReferences(const CoverFlowControllerSetup &setup);

  /// True when the active collection's view type is CoverFlow.
  [[nodiscard]] bool isActive() const;

  /// Idempotent: lazily creates the CoverFlowWidget and wires its signals.
  void ensureWidget();
  /// Push the active collection's appearance config onto the widget.
  void applyConfig();
  /// Show/hide the carousel vs the grid and adjust scrollbar policy. Emits
  /// activeChanged() so listeners can yield viewport space to the carousel.
  void applyVisibility();
  /// Rebuild the flat card list from the current (filtered) visual-index space.
  void rebuildCards();
  /// rebuildCards() gated on the widget existing and being active — for the
  /// data/filter refresh paths that should not pay the per-item descriptor
  /// cost while in grid/list mode.
  void rebuildCardsIfActive();
  /// ensure + config + rebuild + applyVisibility, all unconditional. Safe on
  /// every view-type transition because ensureWidget() is idempotent.
  void refreshForViewTypeChange();

  /// Keep the carousel's centered card in sync with the canonical selection
  /// and (debounced) resolve its preview video + artwork gallery.
  void onSelectionChanged(int selectedIndex);

  [[nodiscard]] CoverFlowWidget *widget() const { return m_widget; }

signals:
  void selectItemByIndex(int index);
  void subcollectionEntered(int subcollectionIndex);
  void virtualFolderEntered(const QString &folderPath);
  void itemActivated(int visualIndex);
  void activeChanged(bool active);

private:
  void resolveAndPushVideo(int visualIndex);
  void resolveAndPushGallery(int visualIndex);

  /// Kartend-yeik: ctx-routed FilterManager accessor. Replaces the old
  /// m_filterManager pointer-as-setup-struct-field pattern. Returns the
  /// IFilterManager interface — the cover-flow controller only needs the
  /// read-side surface (isFiltered/getActualIndex/filteredCount).
  [[nodiscard]] IFilterManager *filterMgr() const;

  // Borrowed dependencies — never owned, never deleted through these.
  const ApplicationContext *m_ctx = nullptr;
  QScrollArea *m_mediaScrollArea = nullptr;
  QWidget *m_gridContainer = nullptr;
  const CollectionContext *m_context = nullptr;
  const QList<CollectionConfig> *m_collections = nullptr;
  ScrollDataStore *m_dataManager = nullptr;

  // The carousel widget is parented to the items-page content widget, not to
  // this controller — Qt owns its lifetime. Raw pointer, never deleted here.
  CoverFlowWidget *m_widget = nullptr;
  TimerUtils::DebouncedTimer *m_resolveDebouncer = nullptr;
  int m_pendingVisualIndex = -1;
};

#endif
