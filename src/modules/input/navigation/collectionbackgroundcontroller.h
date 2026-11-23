#ifndef COLLECTIONBACKGROUNDCONTROLLER_H
#define COLLECTIONBACKGROUNDCONTROLLER_H

#include "collection/collectionconfig.h"
#include <QList>
#include <QObject>

QT_BEGIN_NAMESPACE
class QLineEdit;
class QMenuBar;
class QScrollArea;
class QWidget;
QT_END_NAMESPACE

class BackgroundImageWidget;
class BackgroundVideoWidget;
class HeaderLogoOverlay;
class VignetteOverlay;
class BackdropBlurOverlay;

/**
 * @brief Setup struct for CollectionBackgroundController dependencies.
 *
 * Every field is borrowed — the controller owns only the lazily-created
 * overlay widgets (Qt-parented to the items viewport / toolbar).
 */
struct CollectionBackgroundControllerSetup {
  QScrollArea *itemScrollArea = nullptr;
  QWidget *itemsTopBar = nullptr;
  QMenuBar *menubar = nullptr;
  QLineEdit *searchBar = nullptr;
  const int *currentCollectionIndex = nullptr;
  QList<CollectionConfig> *collections = nullptr;
};

/**
 * @brief Drives the items-page background/overlay UI surface for a collection.
 *
 * Extracted from NavigationManager. Owns the per-collection background render:
 * the video- and image-background widgets, header-logo overlay, vignette
 * overlay, and toolbar backdrop-blur overlay, plus the viewport/toolbar/
 * menubar/search-bar stylesheets that carry the colour background and
 * primary-color theming. Each overlay
 * widget is lazily created on first need and hidden (decoder released, for
 * video) whenever the active collection doesn't use it, so collections without
 * a given effect pay zero cost.
 *
 * Borrows every dependency; holds no back-pointer into NavigationManager. The
 * items scroll bar is connected once so the image wallpaper can follow the
 * grid for parallax; a static wallpaper ignores the position.
 */
class CollectionBackgroundController : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(CollectionBackgroundController)
public:
  explicit CollectionBackgroundController(QObject *parent = nullptr);
  ~CollectionBackgroundController() override = default;

  void setupReferences(const CollectionBackgroundControllerSetup &setup);

  /// Apply the collection's background (video / image / color) plus the
  /// header-logo, vignette, and toolbar-blur overlays, and wire the scroll
  /// listener that drives wallpaper parallax.
  void applyBackgroundForCollection(int collectionIndex);

  /// Apply the collection's primary color to the ItemWidget static palette and
  /// the toolbar / menubar / search-bar stylesheets.
  void applyPrimaryColorForCollection(int collectionIndex);

  /// Re-apply the chrome after the DESKTOP colours changed — a Plasma
  /// activity switch with a per-activity wallpaper hands the session a new
  /// titlebar/accent (user request 2026-08-19). The toolbar tint is baked
  /// into a STYLESHEET STRING, so the palette re-broadcast that handles the
  /// rest of the app cannot touch it; it has to be rebuilt. Only
  /// desktop-derived collections repaint — one tinted from its own primary
  /// colour has nothing to refresh, and re-applying would needlessly
  /// restart a video background. A collection set to its own colour with
  /// none chosen counts as desktop-derived: its bar falls back to the
  /// titlebar colour (Kartend-b8955).
  void refreshDesktopDerivedChrome();

private:
  /// The collection whose chrome is on screen, so a colour change can
  /// repaint it without the caller re-supplying the index.
  int m_lastAppliedIndex = -1;

  // Pass the items grid's scroll position to the image wallpaper.
  void syncWallpaperToScroll();

  // Borrowed dependencies — never owned, never deleted through these.
  QScrollArea *m_itemScrollArea = nullptr;
  QWidget *m_itemsTopBar = nullptr;
  QMenuBar *m_menubar = nullptr;
  QLineEdit *m_searchBar = nullptr;
  const int *m_currentCollectionIndex = nullptr;
  QList<CollectionConfig> *m_collections = nullptr;

  // lazy-created on first collection that requests a video background.
  // Parented to the items viewport so its lifetime mirrors the scroll area's.
  // Hidden + decoder-released when the active collection doesn't use video.
  BackgroundVideoWidget *m_backgroundVideo = nullptr;
  // lazy-created on first collection with an image background; same
  // parenting and lifetime as the video widget. Hidden + image released when
  // the active collection doesn't use one.
  BackgroundImageWidget *m_backgroundImage = nullptr;
  // lazy-created header logo overlay; parented to the items top bar.
  HeaderLogoOverlay *m_headerLogo = nullptr;
  // lazy-created vignette overlay layered above the logo so the logo isn't
  // darkened along with the corners.
  VignetteOverlay *m_vignette = nullptr;
  // lazy-created toolbar backdrop blur. Hidden whenever the active collection
  // has the toggle off or uses a video bg.
  BackdropBlurOverlay *m_toolbarBlur = nullptr;
  bool m_scrollListenerConnected = false;
};

#endif
