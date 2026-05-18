#ifndef APPLICATIONCONTEXT_H
#define APPLICATIONCONTEXT_H

#include "collectionutils.h"
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

class DetailsPane;
class InteractionStateHolder;
class LoadingOverlay;
class EmptyStateWidget;

// Forward declarations for managers
class ScrollManager;
class ArtworkManager;
class ISettingsManager;
class ISessionManager;
class DetailsPaneManager;
class DetailPageManager;
class IDatabaseManager;
class NavigationManager;
class AnimationManager;
class SelectionManager;
class ViewportManager;
class InteractionManager;
class MouseManager;
class KeyboardManager;
class EventManager;
class SearchManager;
class LaunchManager;
class ICacheManager;
class PlaylistManager;

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
 *   m_appContext.managers.scrollManager = getScrollManager();
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
    DetailsPane *sidebar = nullptr;
    EmptyStateWidget *loadingLabel = nullptr;
    LoadingOverlay *loadingOverlay = nullptr;
  } ui;

  // ─────────────────────────────────────────────────────────────────────────
  // Manager references — populated after each manager is created
  // ─────────────────────────────────────────────────────────────────────────
  struct ManagerRefs {
    ScrollManager *scrollManager = nullptr;
    ArtworkManager *artworkManager = nullptr;
    ISettingsManager *settingsManager = nullptr;
    ISessionManager *sessionManager = nullptr;
    DetailsPaneManager *detailsPaneManager = nullptr;
    DetailPageManager *detailPageManager = nullptr;
    IDatabaseManager *databaseManager = nullptr;
    NavigationManager *navigationManager = nullptr;
    AnimationManager *animationManager = nullptr;
    SelectionManager *selectionManager = nullptr;
    ViewportManager *viewportManager = nullptr;
    InteractionManager *interactionManager = nullptr;
    MouseManager *mouseManager = nullptr;
    KeyboardManager *keyboardManager = nullptr;
    EventManager *eventManager = nullptr;
    SearchManager *searchManager = nullptr;
    LaunchManager *launchManager = nullptr;
    ICacheManager *cacheManager = nullptr;
    PlaylistManager *playlistManager = nullptr;

    // Centralized interaction state (owned by InteractionManager)
    InteractionStateHolder *interactionState = nullptr;
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
  [[nodiscard]] ScrollManager *scrollManager() const { return managers.scrollManager; }
  [[nodiscard]] ArtworkManager *artworkManager() const { return managers.artworkManager; }
  [[nodiscard]] ISettingsManager *settingsManager() const { return managers.settingsManager; }
  [[nodiscard]] ISessionManager *sessionManager() const { return managers.sessionManager; }
  [[nodiscard]] DetailsPaneManager *detailsPaneManager() const {
    return managers.detailsPaneManager;
  }
  [[nodiscard]] DetailPageManager *detailPageManager() const { return managers.detailPageManager; }
  [[nodiscard]] IDatabaseManager *databaseManager() const { return managers.databaseManager; }
  [[nodiscard]] NavigationManager *navigationManager() const { return managers.navigationManager; }
  [[nodiscard]] AnimationManager *animationManager() const { return managers.animationManager; }
  [[nodiscard]] SelectionManager *selectionManager() const { return managers.selectionManager; }
  [[nodiscard]] ViewportManager *viewportManager() const { return managers.viewportManager; }
  [[nodiscard]] InteractionManager *interactionManager() const {
    return managers.interactionManager;
  }
  [[nodiscard]] MouseManager *mouseManager() const { return managers.mouseManager; }
  [[nodiscard]] KeyboardManager *keyboardManager() const { return managers.keyboardManager; }
  [[nodiscard]] EventManager *eventManager() const { return managers.eventManager; }
  [[nodiscard]] SearchManager *searchManager() const { return managers.searchManager; }
  [[nodiscard]] LaunchManager *launchManager() const { return managers.launchManager; }
  [[nodiscard]] ICacheManager *cacheManager() const { return managers.cacheManager; }
  [[nodiscard]] PlaylistManager *playlistManager() const { return managers.playlistManager; }
  [[nodiscard]] InteractionStateHolder *interactionState() const {
    return managers.interactionState;
  }
};

#endif // APPLICATIONCONTEXT_H
