#ifndef COLLECTIONBACKGROUNDCONTROLLER_H
#define COLLECTIONBACKGROUNDCONTROLLER_H

#include "collectionutils.h"
#include <QList>
#include <QObject>
#include <QTimer>

QT_BEGIN_NAMESPACE
class QLineEdit;
class QMenuBar;
class QScrollArea;
class QWidget;
QT_END_NAMESPACE

class BackgroundVideoWidget;
class HeaderLogoOverlay;
class VignetteOverlay;
class BackdropBlurOverlay;

/**
 * @brief Setup struct for CollectionBackgroundController dependencies.
 *
 * Every field is borrowed — the controller owns only the four lazily-created
 * overlay widgets (Qt-parented to the items viewport / toolbar) and the
 * parallax throttle timer.
 */
struct CollectionBackgroundControllerSetup {
  QScrollArea *itemScrollArea = nullptr;
  QWidget *itemsTopBar = nullptr;
  QMenuBar *menubar = nullptr;
  QLineEdit *searchBar = nullptr;
  int *currentCollectionIndex = nullptr;
  QList<CollectionConfig> *collections = nullptr;
};

/**
 * @brief Drives the items-page background/overlay UI surface for a collection.
 *
 * Extracted from NavigationManager. Owns the per-collection background render:
 * the video-background widget, header-logo overlay, vignette overlay, and
 * toolbar backdrop-blur overlay, plus the viewport/toolbar/menubar/search-bar
 * stylesheets that carry the wallpaper and primary-color theming. Each overlay
 * widget is lazily created on first need and hidden (decoder released, for
 * video) whenever the active collection doesn't use it, so collections without
 * a given effect pay zero cost.
 *
 * Borrows every dependency; holds no back-pointer into NavigationManager. The
 * items scroll bar is connected once so wallpaper parallax can re-position the
 * background image as the user scrolls; the throttled handler self-bails when
 * parallax is disabled for the active collection.
 */
class CollectionBackgroundController : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(CollectionBackgroundController)
public:
  explicit CollectionBackgroundController(QObject *parent = nullptr);
  ~CollectionBackgroundController() override = default;

  void setupReferences(const CollectionBackgroundControllerSetup &setup);

  /// Apply the collection's background (video / image / color) plus the
  /// header-logo, vignette, and toolbar-blur overlays, and wire the parallax
  /// scroll listener.
  void applyBackgroundForCollection(int collectionIndex);

  /// Apply the collection's primary color to the ItemWidget static palette and
  /// the toolbar / menubar / search-bar stylesheets.
  void applyPrimaryColorForCollection(int collectionIndex);

private:
  // throttled scroll handler for wallpaper parallax (coalesced to ~60Hz).
  void onItemsScrolled();
  void applyParallaxOffset();

  // Borrowed dependencies — never owned, never deleted through these.
  QScrollArea *m_itemScrollArea = nullptr;
  QWidget *m_itemsTopBar = nullptr;
  QMenuBar *m_menubar = nullptr;
  QLineEdit *m_searchBar = nullptr;
  int *m_currentCollectionIndex = nullptr;
  QList<CollectionConfig> *m_collections = nullptr;

  // lazy-created on first collection that requests a video background.
  // Parented to the items viewport so its lifetime mirrors the scroll area's.
  // Hidden + decoder-released when the active collection doesn't use video.
  BackgroundVideoWidget *m_backgroundVideo = nullptr;
  // lazy-created header logo overlay; parented to the items top bar.
  HeaderLogoOverlay *m_headerLogo = nullptr;
  // lazy-created vignette overlay layered above the logo so the logo isn't
  // darkened along with the corners.
  VignetteOverlay *m_vignette = nullptr;
  // lazy-created toolbar backdrop blur. Hidden whenever the active collection
  // has the toggle off or uses a video bg.
  BackdropBlurOverlay *m_toolbarBlur = nullptr;
  // throttle for parallax stylesheet rebuilds. Lazy-created on first scroll
  // while parallax is active. Caps QSS updates at ~60Hz.
  QTimer m_parallaxThrottle; // Kartend-a911.5: value member; lifetime tracks owner
  bool m_parallaxThrottleInited = false;
  bool m_scrollListenerConnected = false;
};

#endif
