#ifndef APPLICATIONCONTEXT_H
#define APPLICATIONCONTEXT_H

#include "collectionutils.h"
#include <QList>
#include <functional>

QT_BEGIN_NAMESPACE
class QScrollArea;
class QStackedWidget;
class QWidget;
class QLineEdit;
class QPushButton;
class QLabel;
class QMenuBar;
QT_END_NAMESPACE

class MetadataSidebar;
class InteractionStateHolder;
class LoadingOverlay;

// Forward declarations for managers
class ScrollManager;
class ArtworkManager;
class SettingsManager;
class SessionManager;
class SidebarManager;
class DatabaseManager;
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
class CacheManager;

/**
 * @brief Shared application context containing common dependencies.
 *
 * This struct aggregates frequently-shared pointers and state references
 * that are passed to multiple managers. Individual manager setup structs
 * can include a pointer to this context instead of duplicating all fields.
 *
 * Field Categories:
 *
 * REQUIRED (must be set before any manager setup):
 * - collections, currentCollectionIndex - Core collection state
 * - isShuttingDown - Shutdown coordination flag
 *
 * REQUIRED FOR UI (must be set for any UI-related manager):
 * - itemScrollArea, gridContainer - Primary UI containers
 * - stackedWidget, itemsPage - Page switching
 *
 * OPTIONAL (set based on feature needs):
 * - searchBar, searchModeButton - Search functionality
 * - sidebar - Metadata display
 * - menubar - Menu integration
 * - loadingLabel, loadingOverlay - Loading feedback
 *
 * POPULATED AFTER SETUP:
 * - All manager pointers (scrollManager, artworkManager, etc.)
 * - interactionState - Set after InteractionManager created
 *
 * Usage pattern:
 *   // In MainWindow, create once:
 *   m_appContext.collections = &m_collections;
 *   m_appContext.currentCollectionIndex = &currentCollectionIndex;
 *   ...
 *
 *   // In setup structs:
 *   SomeManagerSetup setup;
 *   setup.ctx = &m_appContext;
 *   setup.managerSpecificField = someValue;
 */
struct ApplicationContext {
  // ─────────────────────────────────────────────────────────────────────────
  // Required: Core collection state (must be set before any manager setup)
  // ─────────────────────────────────────────────────────────────────────────
  QList<CollectionConfig> *collections = nullptr;
  int *currentCollectionIndex = nullptr;
  const CollectionHierarchyCache *hierarchyCache = nullptr;
  GeneralSettings *generalSettings = nullptr;
  const bool *isShuttingDown = nullptr;

  // ─────────────────────────────────────────────────────────────────────────
  // Required for UI: Common UI elements (must be set for UI managers)
  // ─────────────────────────────────────────────────────────────────────────
  QScrollArea *itemScrollArea = nullptr;
  QStackedWidget *stackedWidget = nullptr;
  QWidget *itemsPage = nullptr;
  QWidget *itemsTopBar = nullptr;
  QWidget *gridContainer = nullptr;

  // ─────────────────────────────────────────────────────────────────────────
  // Optional: Feature-specific UI elements
  // ─────────────────────────────────────────────────────────────────────────
  QMenuBar *menubar = nullptr;
  QLineEdit *searchBar = nullptr;
  QPushButton *searchModeButton = nullptr;
  MetadataSidebar *sidebar = nullptr;
  QLabel *loadingLabel = nullptr;
  LoadingOverlay *loadingOverlay = nullptr;

  // ─────────────────────────────────────────────────────────────────────────
  // Populated after setup: Manager references
  // These are set in ApplicationManager after managers are created
  // ─────────────────────────────────────────────────────────────────────────
  ScrollManager *scrollManager = nullptr;
  ArtworkManager *artworkManager = nullptr;
  SettingsManager *settingsManager = nullptr;
  SessionManager *sessionManager = nullptr;
  SidebarManager *sidebarManager = nullptr;
  DatabaseManager *databaseManager = nullptr;
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
  CacheManager *cacheManager = nullptr;

  // Centralized interaction state (owned by InteractionManager)
  InteractionStateHolder *interactionState = nullptr;

  // Convenience accessors with null-safety
  [[nodiscard]] bool isValid() const {
    return collections && currentCollectionIndex;
  }

  [[nodiscard]] int currentIndex() const {
    return currentCollectionIndex ? *currentCollectionIndex : -1;
  }

  [[nodiscard]] bool shuttingDown() const {
    return isShuttingDown ? *isShuttingDown : false;
  }

  // Collection validation helper
  [[nodiscard]] bool isValidCollectionIndex() const {
    return collections && currentCollectionIndex &&
           *currentCollectionIndex >= 0 &&
           *currentCollectionIndex < collections->size();
  }
};

#endif // APPLICATIONCONTEXT_H
