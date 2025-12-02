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
QT_END_NAMESPACE

class MetadataSidebar;
class InteractionStateHolder;

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
  // Collection state
  QList<CollectionConfig> *collections = nullptr;
  int *currentCollectionIndex = nullptr;
  const CollectionHierarchyCache *hierarchyCache = nullptr;
  GeneralSettings *generalSettings = nullptr;
  const bool *isShuttingDown = nullptr;

  // Common UI elements
  QScrollArea *itemScrollArea = nullptr;
  QStackedWidget *stackedWidget = nullptr;
  QWidget *itemsPage = nullptr;
  QWidget *gridContainer = nullptr;
  QLineEdit *searchBar = nullptr;
  QPushButton *searchModeButton = nullptr;
  MetadataSidebar *sidebar = nullptr;
  QLabel *loadingLabel = nullptr;

  // Manager references (commonly shared across setup structs)
  // These are populated after managers are created in ApplicationManager
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
