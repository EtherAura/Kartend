#ifndef COVERFLOWWIDGET_H
#define COVERFLOWWIDGET_H

// Cover-flow view mode.
//
// Custom QPainter-based widget that renders a horizontal carousel of cards
// with the focused item centered and surrounding cards perspective-skewed
// into the background. Replaces the standard ItemWidget grid for the
// CoverFlow ViewType — sits as a sibling of gridContainer in the items page
// scroll area and is shown/hidden by ScrollManager based on the active view.
//
// Selection is bidirectional: user input (wheel, arrows, click) emits
// selectionChangeRequested(int) and itemActivated(int) to the caller, while
// programmatic selection updates from SelectionManager flow back in via
// setSelectedIndex(). Indices are visual indices — they include subcollection
// and virtual-folder prefix items, matching the rest of the scroll module.

#include <QFutureWatcher>
#include <QHash>
#include <QList>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QString>
#include <QWidget>

class CoverFlowGalleryStrip;
class QPaintEvent;
class QResizeEvent;
class QWheelEvent;
class QMouseEvent;
class QKeyEvent;
class VideoPreviewWidget;

struct CoverFlowCardData {
  QString title;
  QString artworkPath; ///< Absolute path to artwork file. Empty -> placeholder.
  QString videoPath;   ///< Absolute path to preview video. Empty -> no video.
};

/// one entry in the per-item gallery toolbar shown beneath
/// the carousel when in cover flow. Mirrors DetailsPane::GalleryEntry
/// so the same resolver pattern works for both views.
struct CoverFlowGalleryEntry {
  QString label;
  QString path;
  bool isVideo = false;
};

class CoverFlowWidget : public QWidget {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(CoverFlowWidget)
  Q_PROPERTY(qreal selectionPositionF READ selectionPositionF WRITE setSelectionPositionF)
  // Gallery toolbar lives in coverflowgallerystrip.{h,cpp} (Kartend-y3ia
  // step 1); friend so the strip can reach m_gallery / m_galleryActiveIndex
  // / m_galleryThumbCache / palette() / selectionColorOrFallback() without
  // a public accessor surface.
  friend class CoverFlowGalleryStrip;

public:
  explicit CoverFlowWidget(QWidget *parent = nullptr);
  ~CoverFlowWidget() override;

  void setCards(const QList<CoverFlowCardData> &cards);
  [[nodiscard]] int cardCount() const { return m_cards.size(); }

  void setSelectedIndex(int index, bool animate);
  [[nodiscard]] int selectedIndex() const { return m_selectedIndex; }

  /// video paths are resolved lazily by ScrollManager on
  /// selection change because filesystem-scanning a video directory for
  /// every one of 29k items at rebuild time freezes the UI thread. When
  /// the canonical selection lands, ScrollManager calls this to update
  /// the centered card's preview source. Empty path clears the slot.
  void setVideoPathForIndex(int index, const QString &videoPath);

  /// replace the gallery toolbar for the centered card.
  /// Each entry becomes a clickable thumbnail along the bottom edge that
  /// switches the centered card's display between artwork variants and
  /// preview video. Empty list hides the toolbar.
  void setGalleryForIndex(int index, const QList<CoverFlowGalleryEntry> &entries);

  void setCornerRadius(int radius);
  void setTileColor(const QString &color);
  void setSelectionColor(const QString &color);
  void setBackgroundColor(const QString &color);
  void setHideTitles(bool hide);
  void setFontSize(int size);
  void setFontFamily(const QString &family);

  qreal selectionPositionF() const { return m_selectionPositionF; }
  void setSelectionPositionF(qreal v);

signals:
  /// User shifted the selection (wheel/arrow/side-card click). The caller is
  /// expected to route this through SelectionManager so sidebar/restore/etc.
  /// stay coherent; the widget will receive the canonical update back via
  /// setSelectedIndex().
  void selectionChangeRequested(int visualIndex);
  /// User activated the currently centered card (single click on center /
  /// double click anywhere). The caller handles launch / preview.
  void itemActivated(int visualIndex);

protected:
  void paintEvent(QPaintEvent *e) override;
  void resizeEvent(QResizeEvent *e) override;
  void wheelEvent(QWheelEvent *e) override;
  void mousePressEvent(QMouseEvent *e) override;
  void mouseDoubleClickEvent(QMouseEvent *e) override;
  void keyPressEvent(QKeyEvent *e) override;
  void hideEvent(QHideEvent *e) override;

private:
  // Schedule an async pixmap load for @p path; the connect()ed lambda
  // owns the lookup-by-path so we don't have to reverse-scan m_pendingLoads
  // from a slot's sender() (QFutureWatcher<T> is not Q_OBJECT).
  void startArtworkLoad(const QString &path);

  struct CardLayout {
    int index = -1;
    qreal offset = 0.0; ///< distance from current center, fractional
    QRect rect;         ///< target rect after transform
  };

  [[nodiscard]] QList<CardLayout> computeVisibleLayout() const;
  [[nodiscard]] int hitTestCard(const QPoint &pt) const;
  [[nodiscard]] QPixmap pixmapForIndex(int idx);
  [[nodiscard]] QPixmap buildPlaceholderPixmap(int width, int height) const;
  [[nodiscard]] QColor tileColorOrFallback() const;
  [[nodiscard]] QColor selectionColorOrFallback() const;
  [[nodiscard]] QColor backgroundColorOrFallback() const;
  [[nodiscard]] int cardSize() const;
  [[nodiscard]] qreal currentPositionF() const { return m_selectedIndex + m_selectionPositionF; }
  void requestArtworkLoad(int idx);
  // Schedule a worker-thread Smooth-scale of @p sourcePm to @p targetSize
  // and insert the result into m_scaledPixmapCache under @p key. No-op if
  // a scale for @p key is already in flight. Triggers update() on
  // completion. See Kartend-g6ft for rationale.
  void requestScaledPixmap(const QString &key, const QPixmap &sourcePm, const QSize &targetSize);
  void cancelPendingScales();
  void pruneScaledPixmapCache();
  void prunePixmapCache();
  void updateVideoPreviewGeometry();
  void applyVideoPreviewState();

  // Gallery toolbar moved to CoverFlowGalleryStrip (Kartend-y3ia step 1).
  // m_galleryStrip below is the owner; its public methods replace the
  // four declarations that lived here.
  CoverFlowGalleryStrip *m_galleryStrip = nullptr;

  QList<CoverFlowCardData> m_cards;
  int m_selectedIndex = 0;
  qreal m_selectionPositionF = 0.0; ///< animated offset (-N .. +N) added to m_selectedIndex
  QPropertyAnimation *m_glide = nullptr;

  QHash<QString, QPixmap> m_pixmapCache;
  QHash<QString, QFutureWatcher<QPixmap> *> m_pendingLoads;

  // Cache of source pixmaps already scaled to card-target size + smooth
  // transform, keyed on "path|WxH" (Kartend-g6ft). paintEvent's
  // pm.scaled(..., Smooth) call ran ~11 times per paint (kVisibleSideCards*2+1)
  // and dominated the per-frame cost (profile: p50 paint = 20.9ms, well
  // over the 16ms 60fps budget). Population happens on a worker thread via
  // requestScaledPixmap so a cache miss never blocks paint. Invalidated on
  // resize, setCards, and setTileColor — between those events every paint
  // hits, and the steady-state paint cost drops to ~8-11ms.
  QHash<QString, QPixmap> m_scaledPixmapCache;
  QHash<QString, QFutureWatcher<QPixmap> *> m_pendingScales;

  // Per-instance placeholder cache (was function-local static — multiple
  // CoverFlowWidget instances with different tile colors would thrash a
  // shared single-slot cache when switching collections).
  mutable QPixmap m_placeholderCache;
  mutable quint64 m_placeholderCacheKey = 0;
  mutable int m_placeholderCachedRadius = 0;
  mutable QString m_placeholderCachedTile;

  int m_cornerRadius = 0;
  QString m_tileColor;
  QString m_selectionColor;
  QString m_backgroundColor;
  bool m_hideTitles = false;
  int m_fontSize = 12;
  QString m_fontFamily;

  // middle-click toggles between artwork and video preview on
  // the centered card. The QLabel-based VideoPreviewWidget is reused so we
  // get the same Wayland-friendly QVideoSink pipeline the sidebar uses.
  // The widget is sized + repositioned every paintEvent to track the
  // animated center card; when m_videoMode is false or the centered card
  // has no videoPath, the preview is hidden so the painter renders the
  // artwork normally.
  VideoPreviewWidget *m_videoPreview = nullptr;
  bool m_videoMode = false;
  int m_videoPreviewIndex = -1;
  QRect m_lastCenterRect;

  // Per-item gallery toolbar. m_gallery is replaced on each
  // selection change; m_galleryActiveIndex selects which entry drives the
  // centered card's display (-1 = the card's default artworkPath).
  QList<CoverFlowGalleryEntry> m_gallery;
  int m_galleryOwnerIndex = -1; ///< index of the card the gallery belongs to
  int m_galleryActiveIndex = -1;
  QHash<QString, QPixmap> m_galleryThumbCache;
};

#endif // COVERFLOWWIDGET_H
