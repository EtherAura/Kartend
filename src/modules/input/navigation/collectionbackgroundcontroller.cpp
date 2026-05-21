// CollectionBackgroundController — per-collection background/overlay rendering.
// Extracted from NavigationManager (navigationmanagerappearance.cpp seam).
#include "collectionbackgroundcontroller.h"
#include "backdropbluroverlay.h"
#include "backgroundvideowidget.h"
#include "headerlogooverlay.h"
#include "itemwidget.h"
#include "vignetteoverlay.h"
#include <QAbstractSlider>
#include <QBoxLayout>
#include <QColor>
#include <QLayoutItem>
#include <QLineEdit>
#include <QMenuBar>
#include <QPixmap>
#include <QScrollArea>
#include <QScrollBar>
#include <QString>
#include <QStringLiteral>
#include <QTimer>
#include <QWidget>

CollectionBackgroundController::CollectionBackgroundController(QObject *parent) : QObject(parent) {}

void CollectionBackgroundController::setupReferences(
    const CollectionBackgroundControllerSetup &setup) {
  m_itemScrollArea = setup.itemScrollArea;
  m_itemsTopBar = setup.itemsTopBar;
  m_menubar = setup.menubar;
  m_searchBar = setup.searchBar;
  m_currentCollectionIndex = setup.currentCollectionIndex;
  m_collections = setup.collections;
}

void CollectionBackgroundController::applyBackgroundForCollection(int collectionIndex) {
  if (!m_itemScrollArea || !m_collections || collectionIndex < 0 ||
      collectionIndex >= (*m_collections).size()) {
    return;
  }

  const CollectionConfig &collection = (*m_collections)[collectionIndex];
  QWidget *viewport = m_itemScrollArea->viewport();
  if (!viewport) {
    return;
  }

  // video bg routes through BackgroundVideoWidget instead of
  // the QSS path because Qt stylesheets can't render video. The widget is
  // lazy-created on first need and hidden (with decoder released) whenever
  // the active collection doesn't use video.
  const bool wantsVideo = (collection.background.backgroundType == BackgroundType::Video) &&
                          !collection.background.backgroundVideo.isEmpty();
  if (wantsVideo) {
    if (!m_backgroundVideo) {
      m_backgroundVideo = new BackgroundVideoWidget(viewport);
    }
    m_backgroundVideo->setVideoPath(collection.background.backgroundVideo);
    // Lower so the scroll widget (and item grid) renders on top. show()
    // before lower() avoids a one-frame flash where it'd come up raised.
    m_backgroundVideo->show();
    m_backgroundVideo->lower();
  } else if (m_backgroundVideo) {
    m_backgroundVideo->clear();
    m_backgroundVideo->hide();
  }

  // header logo as a real toolbar layout member. Insertion
  // index depends on the chosen anchor so the logo pushes neighbouring
  // controls aside instead of floating on top of them. TopLeft inserts at
  // index 0 (truly leftmost); TopRight appends (truly rightmost); TopCenter
  // sits just after the existing horizontal spacer that separates the
  // title block from the right-side controls — i.e. roughly mid-toolbar.
  auto *toolbarLayout =
      m_itemsTopBar ? qobject_cast<QHBoxLayout *>(m_itemsTopBar->layout()) : nullptr;
  const bool wantsLogo = !collection.background.headerLogoImage.isEmpty() && toolbarLayout;
  if (wantsLogo) {
    if (!m_headerLogo) {
      m_headerLogo = new HeaderLogoOverlay(m_itemsTopBar);
    }
    // Always remove first so re-applying with a different position moves
    // the widget rather than leaving stale duplicates in the layout.
    toolbarLayout->removeWidget(m_headerLogo);

    int insertIndex = 0;
    switch (collection.background.headerLogoPosition) {
    case HeaderLogoPosition::TopLeft:
      insertIndex = 0;
      break;
    case HeaderLogoPosition::TopRight:
      insertIndex = toolbarLayout->count();
      break;
    case HeaderLogoPosition::TopCenter: {
      // Find the spacer that splits left and right toolbar groups; place
      // the logo immediately after it. Falls back to layout end if the
      // spacer ever gets removed in a future .ui edit.
      int spacerIdx = -1;
      for (int i = 0; i < toolbarLayout->count(); ++i) {
        if (toolbarLayout->itemAt(i)->spacerItem()) {
          spacerIdx = i;
          break;
        }
      }
      insertIndex = (spacerIdx >= 0) ? spacerIdx + 1 : toolbarLayout->count();
      break;
    }
    }
    toolbarLayout->insertWidget(insertIndex, m_headerLogo);
    m_headerLogo->setLogo(collection.background.headerLogoImage,
                          collection.background.headerLogoPosition);
    m_headerLogo->show();
  } else if (m_headerLogo) {
    if (toolbarLayout) {
      toolbarLayout->removeWidget(m_headerLogo);
    }
    m_headerLogo->clear();
    m_headerLogo->hide();
  }

  // vignette overlay. Layered above the items grid (so it
  // darkens grid edges) but below the header logo (so the logo stays
  // bright in a corner if the user picks TopLeft/TopRight).
  const bool wantsVignette =
      collection.background.vignetteEnabled && collection.background.vignetteIntensity > 0;
  if (wantsVignette) {
    if (!m_vignette) {
      m_vignette = new VignetteOverlay(viewport);
    }
    m_vignette->setIntensity(collection.background.vignetteIntensity);
    m_vignette->show();
  } else if (m_vignette) {
    m_vignette->hide();
  }

  // Re-establish viewport z-order every apply so any external raise()/lower()
  // since last navigation can't leave the layers stacked wrong. Order:
  // video → (scroll widget, default) → vignette. The header logo lives on
  // m_itemsTopBar instead, so it isn't part of this stack.
  if (m_backgroundVideo) {
    m_backgroundVideo->lower();
  }
  if (m_vignette) {
    m_vignette->raise();
  }

  // toolbar backdrop blur. Image bg only — sampling video
  // frames every paint is too expensive without GPU shaders, so the
  // toggle is treated as a no-op for video bgs. Parented to m_itemsTopBar
  // and lowered behind the toolbar's button widgets so they render on top.
  const bool wantsBlur = collection.background.toolbarBackdropBlur && m_itemsTopBar &&
                         collection.background.backgroundType == BackgroundType::Image &&
                         !collection.background.backgroundImage.isEmpty();
  if (wantsBlur) {
    if (!m_toolbarBlur) {
      m_toolbarBlur = new BackdropBlurOverlay(m_itemsTopBar);
    }
    QPixmap source(collection.background.backgroundImage);
    if (!source.isNull()) {
      m_toolbarBlur->setSource(source, collection.background.backdropBlurRadius);
      m_toolbarBlur->show();
      m_toolbarBlur->lower();
    } else if (m_toolbarBlur) {
      m_toolbarBlur->clear();
      m_toolbarBlur->hide();
    }
  } else if (m_toolbarBlur) {
    m_toolbarBlur->clear();
    m_toolbarBlur->hide();
  }

  // connect the items scroll bar once so parallax can adjust
  // the wallpaper's vertical position as the user scrolls. The handler
  // self-bails when parallax is disabled for the active collection, so
  // the connection is harmless when no collection wants the effect.
  if (!m_scrollListenerConnected && m_itemScrollArea && m_itemScrollArea->verticalScrollBar()) {
    connect(m_itemScrollArea->verticalScrollBar(), &QAbstractSlider::valueChanged, this,
            [this](int /*v*/) { onItemsScrolled(); });
    m_scrollListenerConnected = true;
  }

  QString styleSheet;
  if (wantsVideo) {
    // Make the viewport (and its descendants — the scroll widget and grid
    // container) transparent so the BackgroundVideoWidget child shows
    // through. ItemWidgets paint their own pixmaps so they stay opaque
    // where they need to be.
    styleSheet = QStringLiteral("QWidget { background-color: transparent; }");
  } else if (collection.background.backgroundType == BackgroundType::Image &&
             !collection.background.backgroundImage.isEmpty()) {
    // Background image mode. Parallax adjusts the vertical
    // position by `scrollValue * strength / 100` — strength 0 keeps the bg
    // locked (fully static), strength 100 makes it move at content speed.
    QString imagePath = collection.background.backgroundImage;
    imagePath.replace("\\", "/");
    int parallaxOffset = 0;
    if (collection.background.wallpaperParallax && collection.background.parallaxStrength > 0 &&
        m_itemScrollArea && m_itemScrollArea->verticalScrollBar()) {
      const int v = m_itemScrollArea->verticalScrollBar()->value();
      parallaxOffset = (v * collection.background.parallaxStrength) / 100;
    }
    styleSheet = QString("QWidget { "
                         "background-image: url(\"%1\"); "
                         "background-repeat: no-repeat; "
                         "background-position: center -%2px; "
                         "background-attachment: fixed; "
                         "}")
                     .arg(imagePath)
                     .arg(parallaxOffset);
  } else if (!collection.background.backgroundColor.isEmpty()) {
    // Background color mode
    styleSheet =
        QString("QWidget { background-color: %1; }").arg(collection.background.backgroundColor);
  } else {
    // Clear any custom background (use system default)
    styleSheet.clear();
  }

  viewport->setStyleSheet(styleSheet);
}

void CollectionBackgroundController::applyPrimaryColorForCollection(int collectionIndex) {
  if (!m_collections || collectionIndex < 0 || collectionIndex >= (*m_collections).size()) {
    return;
  }

  const CollectionConfig &collection = (*m_collections)[collectionIndex];
  ItemWidget::setPrimaryColor(collection.background.primaryColor);
  ItemWidget::setTileColor(collection.background.tileColor);
  ItemWidget::setSelectionColor(collection.background.selectionColor);
  ItemWidget::setListRowColor(collection.listView.listRowColor);
  ItemWidget::setListAltRowColor(collection.listView.listAltRowColor);
  ItemWidget::setCustomFontFamily(collection.customFontFamily);

  bool hasPrimaryColor = !collection.background.primaryColor.isEmpty() &&
                         QColor::isValidColorName(collection.background.primaryColor);

  // when toolbar backdrop blur is active over an image bg,
  // skip the primary-color fill — it would paint a flat color over the
  // blurred wallpaper child and defeat the effect.
  const bool blurActive = collection.background.toolbarBackdropBlur &&
                          collection.background.backgroundType == BackgroundType::Image &&
                          !collection.background.backgroundImage.isEmpty();
  // Apply primary color to toolbar/top bar (exact color, not tinted)
  if (m_itemsTopBar) {
    QString toolbarStyle;
    if (hasPrimaryColor && !blurActive) {
      toolbarStyle = QString("QWidget#itemsTopBar { background-color: %1; }")
                         .arg(collection.background.primaryColor);
    }
    m_itemsTopBar->setStyleSheet(toolbarStyle);
  }

  // Apply primary color to menubar
  if (m_menubar) {
    QString menubarStyle;
    if (hasPrimaryColor) {
      menubarStyle = QString("QMenuBar { background-color: %1; }"
                             "QMenuBar::item { background-color: transparent; }"
                             "QMenuBar::item:selected { background-color: "
                             "rgba(255,255,255,0.2); }")
                         .arg(collection.background.primaryColor);
    }
    m_menubar->setStyleSheet(menubarStyle);
  }

  // Apply primary color to search bar background
  if (m_searchBar) {
    QString searchBarStyle;
    if (hasPrimaryColor) {
      // Tint the primary color slightly for the search bar background
      QColor baseColor(collection.background.primaryColor);
      QColor bgColor = baseColor.lighter(130);
      searchBarStyle = QString("QLineEdit { background-color: %1; border: 1px "
                               "solid %2; border-radius: 4px; padding: 4px; }"
                               "QLineEdit:focus { border-color: %3; }")
                           .arg(bgColor.name())
                           .arg(baseColor.darker(110).name())
                           .arg(baseColor.name());
    }
    m_searchBar->setStyleSheet(searchBarStyle);
  }
}

// throttled scroll handler for wallpaper parallax. The
// scroll bar can emit valueChanged dozens of times per second; rebuilding
// the viewport stylesheet that often is wasteful, so we coalesce updates
// to ~60Hz. The first edge fires immediately so the bg responds without
// perceptible lag; subsequent emissions inside the window are dropped
// and the timeout flush picks up the latest value.
void CollectionBackgroundController::onItemsScrolled() {
  if (!m_currentCollectionIndex || !m_collections || !m_itemScrollArea) {
    return;
  }
  const int idx = *m_currentCollectionIndex;
  if (idx < 0 || idx >= m_collections->size()) {
    return;
  }
  const CollectionConfig &c = (*m_collections)[idx];
  if (!c.background.wallpaperParallax || c.background.parallaxStrength <= 0 ||
      c.background.backgroundType != BackgroundType::Image ||
      c.background.backgroundImage.isEmpty()) {
    return;
  }

  if (!m_parallaxThrottle) {
    m_parallaxThrottle = new QTimer(this);
    m_parallaxThrottle->setSingleShot(true);
    connect(m_parallaxThrottle, &QTimer::timeout, this,
            &CollectionBackgroundController::applyParallaxOffset);
  }
  if (m_parallaxThrottle->isActive()) {
    // An update is already queued; the timeout will pick up the latest
    // scroll value when it fires.
    return;
  }
  applyParallaxOffset();
  // ~60Hz throttle. Higher cadence wastes QSS rebuilds; lower starts to
  // feel laggy on fast scrolls.
  m_parallaxThrottle->start(16);
}

void CollectionBackgroundController::applyParallaxOffset() {
  if (!m_currentCollectionIndex || !m_collections || !m_itemScrollArea ||
      !m_itemScrollArea->viewport() || !m_itemScrollArea->verticalScrollBar()) {
    return;
  }
  const int idx = *m_currentCollectionIndex;
  if (idx < 0 || idx >= m_collections->size()) {
    return;
  }
  const CollectionConfig &c = (*m_collections)[idx];
  if (c.background.backgroundType != BackgroundType::Image ||
      c.background.backgroundImage.isEmpty()) {
    return;
  }
  const int strength =
      c.background.wallpaperParallax ? qBound(0, c.background.parallaxStrength, 100) : 0;
  const int v = m_itemScrollArea->verticalScrollBar()->value();
  const int offset = (v * strength) / 100;

  QString imagePath = c.background.backgroundImage;
  imagePath.replace("\\", "/");
  const QString styleSheet = QString("QWidget { "
                                     "background-image: url(\"%1\"); "
                                     "background-repeat: no-repeat; "
                                     "background-position: center -%2px; "
                                     "background-attachment: fixed; "
                                     "}")
                                 .arg(imagePath)
                                 .arg(offset);
  m_itemScrollArea->viewport()->setStyleSheet(styleSheet);
}
