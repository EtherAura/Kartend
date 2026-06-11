#ifndef APPLICATIONCONTEXT_H
#define APPLICATIONCONTEXT_H

// Headers that only need to name `ApplicationContext` as a pointer type
// (typically `const ApplicationContext *ctx = nullptr` inside a setup
// struct) should include applicationcontext_fwd.h instead — that header
// is a single struct fwd-decl and doesn't drag collectionutils.h + the
// 30+ collection/ leaf headers along the include graph. Translation units
// that actually dereference ctx (read fields, call accessors) still need
// this full header.
#include "applicationcontext_fwd.h"
#include "collection/collectionconfig.h"
#include "collection/collectionhierarchycache.h"
#include "collection/generalsettings.h"
#include <cstddef>
#include <functional>
#include <QList>

QT_BEGIN_NAMESPACE
class QScrollArea;
class QStackedWidget;
class QWidget;
class QLineEdit;
class QPushButton;
class QLabel;
class QMenuBar;
class QAction;
QT_END_NAMESPACE

class IDetailsPane;
class InteractionStateHolder;
class LoadingOverlay;
class EmptyStateWidget;
class OverlayZOrderRegistry;

// Forward declarations for managers
class IScrollManager;
// Role views of the scroll layer (Kartend-h1l8f) — six plain abstract
// interfaces IScrollManager unions. The role pointers below alias the same
// ScrollManager object; consumers take the narrowest role they actually use.
class IVirtualScrollLifecycle;
class IGridLayoutScroll;
class ISelectionOverlayScroll;
class ISearchStateScroll;
class IArtworkPreviewScroll;
class IScrollDataSource;
class IArtworkManager;
class ISettingsManager;
class ISessionManager;
class IDetailsPaneManager;
class IDetailPageManager;
class IDatabaseManager;
class INavigationManager;
class IAnimationManager;
class ISelectionManager;
class IViewportManager;
class IInteractionManager;
class IMouseManager;
class IKeyboardManager;
class EventManager;
class IFilterManager;
class SearchManager;
class LaunchManager;
class ICacheManager;
class IPlaylistManager;

/**
 * @brief Shared application context containing common dependencies.
 *
 * This struct aggregates frequently-shared pointers and state references
 * that are passed to multiple managers. Individual manager setup structs
 * can include a pointer to this context instead of duplicating all fields.
 *
 * Fields are grouped into three nested sub-structs for discoverability:
 *
 *   ctx->collection.*   — Core collection state (collections, indices,
 *                         hierarchy, general settings, shutdown flag).
 *                         Must be set before any manager setup.
 *
 *   ctx->ui.*           — Qt widget references (scroll area, containers,
 *                         menubar, search bar, sidebar, overlays).
 *                         Must be set for any UI-related manager.
 *
 *   ctx->managers.*     — Pointers to fully-constructed managers and the
 *                         centralized InteractionStateHolder. Populated
 *                         after each manager is created.
 *
 * Usage pattern:
 *   // In MainWindow, populate sub-structs:
 *   m_appContext.collection.collections = &m_collections;
 *   m_appContext.collection.currentCollectionIndex = &currentCollectionIndex;
 *   m_appContext.ui.itemScrollArea = ui->itemScrollArea;
 *   m_appContext.managers.seedScrollRoles(getScrollManager());
 *
 *   // In setup structs, ctx pointer fans out via SETUP_GETTER macros:
 *   SomeManagerSetup setup;
 *   setup.ctx = &m_appContext;
 */
struct ApplicationContext {
  // ─────────────────────────────────────────────────────────────────────────
  // Collection state — required before any manager setup
  // ─────────────────────────────────────────────────────────────────────────
  struct CollectionState {
    QList<CollectionConfig> *collections = nullptr;
    int *currentCollectionIndex = nullptr;
    const CollectionHierarchyCache *hierarchyCache = nullptr;
    GeneralSettings *generalSettings = nullptr;
    const bool *isShuttingDown = nullptr;
  } collection;

  // ─────────────────────────────────────────────────────────────────────────
  // UI elements — required for UI managers, optional fields where noted
  // ─────────────────────────────────────────────────────────────────────────
  struct UIElements {
    // Required for UI managers
    QScrollArea *itemScrollArea = nullptr;
    QStackedWidget *stackedWidget = nullptr;
    QWidget *itemsPage = nullptr;
    QWidget *itemsTopBar = nullptr;
    QWidget *gridContainer = nullptr;

    // Optional / feature-specific
    QMenuBar *menubar = nullptr;
    QLineEdit *searchBar = nullptr;
    /// Search-mode toggle: a QAction added to the searchBar QLineEdit at
    /// LeadingPosition (no longer a standalone button).
    QAction *searchModeAction = nullptr;
    IDetailsPane *sidebar = nullptr;
    EmptyStateWidget *loadingLabel = nullptr;
    LoadingOverlay *loadingOverlay = nullptr;
    /// Centralized z-order coordinator for overlays. Lower-layer managers
    /// that own an overlay widget (SelectionOverlayManager,
    /// SearchLoadingOverlay) pick this up via ctx->ui.overlayLayerManager
    /// and forward it through their own setLayerManager() to register their
    /// owned widget. Optional — null means each overlay falls back to
    /// direct QWidget::raise().
    OverlayZOrderRegistry *overlayLayerManager = nullptr;
  } ui;

  // ─────────────────────────────────────────────────────────────────────────
  // Manager references — populated after each manager is created
  // ─────────────────────────────────────────────────────────────────────────
  struct ManagerRefs {
    IScrollManager *scrollManager = nullptr;
    /// Role views of the scroll layer (Kartend-h1l8f): raw aliases to the
    /// same object as scrollManager, stored separately because this header
    /// only forward-declares the interfaces (the upcasts need the complete
    /// IScrollManager hierarchy). Seed/unseed via seedScrollRoles() so the
    /// seven pointers never diverge.
    IVirtualScrollLifecycle *scrollLifecycle = nullptr;
    IGridLayoutScroll *scrollGrid = nullptr;
    ISelectionOverlayScroll *scrollOverlay = nullptr;
    ISearchStateScroll *scrollSearch = nullptr;
    IArtworkPreviewScroll *scrollPreview = nullptr;
    IScrollDataSource *scrollData = nullptr;
    IArtworkManager *artworkManager = nullptr;
    ISettingsManager *settingsManager = nullptr;
    ISessionManager *sessionManager = nullptr;
    IDetailsPaneManager *detailsPaneManager = nullptr;
    IDetailPageManager *detailPageManager = nullptr;
    IDatabaseManager *databaseManager = nullptr;
    INavigationManager *navigationManager = nullptr;
    IAnimationManager *animationManager = nullptr;
    ISelectionManager *selectionManager = nullptr;
    IViewportManager *viewportManager = nullptr;
    IInteractionManager *interactionManager = nullptr;
    IMouseManager *mouseManager = nullptr;
    IKeyboardManager *keyboardManager = nullptr;
    EventManager *eventManager = nullptr;
    SearchManager *searchManager = nullptr;
    LaunchManager *launchManager = nullptr;
    ICacheManager *cacheManager = nullptr;
    IPlaylistManager *playlistManager = nullptr;
    /// CoverFlowController + other scroll-side helpers reach the filter via
    /// ctx (Kartend-yeik). Owned by ScrollManager's DataSourceCoordinator;
    /// this pointer is a raw alias to the IFilterManager interface that
    /// FilterManager implements, seeded after ScrollManager construction.
    /// Exposed via interface so external callers don't need to #include
    /// the concrete filtermanager.h.
    IFilterManager *filterManager = nullptr;

    // Centralized interaction state (owned by InteractionManager)
    InteractionStateHolder *interactionState = nullptr;

    /// Seed the scroll facade plus its six role views in lockstep
    /// (Kartend-h1l8f). Template so the upcasts resolve at the call site,
    /// where the complete IScrollManager hierarchy is visible — this header
    /// only forward-declares the interfaces.
    template <typename ScrollT> void seedScrollRoles(ScrollT *scroll) {
      scrollManager = scroll;
      scrollLifecycle = scroll;
      scrollGrid = scroll;
      scrollOverlay = scroll;
      scrollSearch = scroll;
      scrollPreview = scroll;
      scrollData = scroll;
    }
    /// Unseed overload (teardown): null all seven scroll pointers together.
    void seedScrollRoles(std::nullptr_t) {
      scrollManager = nullptr;
      scrollLifecycle = nullptr;
      scrollGrid = nullptr;
      scrollOverlay = nullptr;
      scrollSearch = nullptr;
      scrollPreview = nullptr;
      scrollData = nullptr;
    }
  } managers;

  // ─────────────────────────────────────────────────────────────────────────
  // Convenience accessors with null-safety
  // ─────────────────────────────────────────────────────────────────────────
  [[nodiscard]] bool isValid() const {
    return collection.collections && collection.currentCollectionIndex;
  }

  [[nodiscard]] int currentIndex() const {
    return collection.currentCollectionIndex ? *collection.currentCollectionIndex : -1;
  }

  [[nodiscard]] bool shuttingDown() const {
    return collection.isShuttingDown ? *collection.isShuttingDown : false;
  }

  // Collection validation helper
  [[nodiscard]] bool isValidCollectionIndex() const {
    return collection.collections && collection.currentCollectionIndex &&
           *collection.currentCollectionIndex >= 0 &&
           *collection.currentCollectionIndex < collection.collections->size();
  }

  // ─────────────────────────────────────────────────────────────────────────
  // Manager accessors — terse, ctx-scoped reads. After ctx is fully populated
  // (initializeAppContext), these are the canonical access path for sibling
  // managers; managers should not cache sibling-manager pointers as fields.
  // ─────────────────────────────────────────────────────────────────────────
  [[nodiscard]] IScrollManager *scrollManager() const { return managers.scrollManager; }
  // Scroll-layer role accessors (Kartend-h1l8f) — same object as
  // scrollManager(), narrowed to one role each. All seven are null together.
  [[nodiscard]] IVirtualScrollLifecycle *scrollLifecycle() const {
    return managers.scrollLifecycle;
  }
  [[nodiscard]] IGridLayoutScroll *scrollGrid() const { return managers.scrollGrid; }
  [[nodiscard]] ISelectionOverlayScroll *scrollOverlay() const { return managers.scrollOverlay; }
  [[nodiscard]] ISearchStateScroll *scrollSearch() const { return managers.scrollSearch; }
  [[nodiscard]] IArtworkPreviewScroll *scrollPreview() const { return managers.scrollPreview; }
  [[nodiscard]] IScrollDataSource *scrollData() const { return managers.scrollData; }
  [[nodiscard]] IArtworkManager *artworkManager() const { return managers.artworkManager; }
  [[nodiscard]] ISettingsManager *settingsManager() const { return managers.settingsManager; }
  [[nodiscard]] ISessionManager *sessionManager() const { return managers.sessionManager; }
  [[nodiscard]] IDetailsPaneManager *detailsPaneManager() const {
    return managers.detailsPaneManager;
  }
  [[nodiscard]] IDetailPageManager *detailPageManager() const { return managers.detailPageManager; }
  [[nodiscard]] IDatabaseManager *databaseManager() const { return managers.databaseManager; }
  [[nodiscard]] INavigationManager *navigationManager() const { return managers.navigationManager; }
  [[nodiscard]] IAnimationManager *animationManager() const { return managers.animationManager; }
  [[nodiscard]] ISelectionManager *selectionManager() const { return managers.selectionManager; }
  [[nodiscard]] IViewportManager *viewportManager() const { return managers.viewportManager; }
  [[nodiscard]] IInteractionManager *interactionManager() const {
    return managers.interactionManager;
  }
  [[nodiscard]] IMouseManager *mouseManager() const { return managers.mouseManager; }
  [[nodiscard]] IKeyboardManager *keyboardManager() const { return managers.keyboardManager; }
  [[nodiscard]] EventManager *eventManager() const { return managers.eventManager; }
  [[nodiscard]] SearchManager *searchManager() const { return managers.searchManager; }
  [[nodiscard]] LaunchManager *launchManager() const { return managers.launchManager; }
  [[nodiscard]] ICacheManager *cacheManager() const { return managers.cacheManager; }
  [[nodiscard]] IPlaylistManager *playlistManager() const { return managers.playlistManager; }
  [[nodiscard]] IFilterManager *filterManager() const { return managers.filterManager; }
  [[nodiscard]] InteractionStateHolder *interactionState() const {
    return managers.interactionState;
  }
};

#endif // APPLICATIONCONTEXT_H
