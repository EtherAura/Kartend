// Forward declarations for commonly used types.
// Include this header when only pointers/references are needed.
#ifndef FORWARDDECL_H
#define FORWARDDECL_H

// Qt forward declarations
#include <QHash>
#include <QList>
#include <QString>

// Application types - use these when only pointers/references are needed.
// For full definitions, include the respective header files.

// From collectionutils.h
struct CollectionConfig;
struct CollectionContext;
struct CollectionHierarchyCache;
struct GeneralSettings;
enum class HorizontalAlignment;
enum class DetailsPaneMode;

// From applicationcontext.h
struct ApplicationContext;

// From errorutils.h
namespace ErrorUtils {
struct ErrorContext;
template <typename T> class Result;
} // namespace ErrorUtils

// From stateutils.h
struct SelectionRestoreState;
struct ScrollState;
struct ArtworkState;
struct ArrowNavigationState;
struct ClickState;
struct StreamScrollState;
struct SearchState;

// Manager forward declarations
class AnimationManager;
class ApplicationManager;
class ArtworkManager;
class CacheManager;
class DatabaseManager;
class EventManager;
class FilterManager;
class InteractionManager;
class InteractionStateHolder;
class KeyboardManager;
class LaunchManager;
class MouseManager;
class NavigationManager;
class QueryManager;
class ScrollManager;
class SearchManager;
class SelectionManager;
class SelectionRestoreManager;
class SessionManager;
class SettingsManager;
class DetailsPaneManager;
class ViewportManager;
class WidgetPoolManager;

// UI forward declarations
class ItemWidget;
class DetailsPane;
class SettingsDialog;

// Scroll module forward declarations
class ArrowKeyScrollHelper;
class GridLayoutCalculator;
class ItemWidgetFactory;
class ScrollEventHandler;
class SelectionCoordinator;
class SelectionOverlayManager;
class VirtualContainerManager;
class ArrowNavigationHandler;

#endif // FORWARDDECL_H
