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
#include <QImage>
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

/// Kartend-el0fr: key for the scaled-pixmap cache. Replaces the per-paint-frame
/// "<path>|<W>x<H>" string concatenation (one temporary QString + two number
/// conversions per visible card per frame during the glide) with a lightweight
/// struct, so paintEvent does a hash lookup with no allocation.
struct CoverFlowScaledKey {
  QString path;
  int width = 0;
  int height = 0;
  bool operator==(const CoverFlowScaledKey &o) const {
    return width == o.width && height == o.height && path == o.path;
  }
};
inline size_t qHash(const CoverFlowScaledKey &k, size_t seed = 0) {
  return qHashMulti(seed, k.path, k.width, k.height);
}

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
  /// Value-copy of the card at @p index; default-constructed when out of
  /// range. Lets callers (and tests) observe per-slot state without exposing
  /// the backing list.
  [[nodiscard]] CoverFlowCardData cardAt(int index) const { return m_cards.value(index); }

  /// Patch a single card in place (Kartend-x7bn8). Incremental alternative to
  /// setCards() for range-chunk arrivals: replaces only title + artworkPath,
  /// preserves the slot's lazily-resolved videoPath, keeps selection / glide /
  /// gallery state untouched, and invalidates only the scaled-pixmap entries
  /// belonging to the replaced artwork instead of dropping the whole cache.
  /// No-op when @p index is out of range or the slot already matches.
  void updateCard(int index, const CoverFlowCardData &card);

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

  // Test-visible state getters. The setters above clamp/early-out and clear
  // caches; without these the clamp and cache-invalidation contracts are
  // unobservable from outside a paint pass (tests/ui/widgets/
  // test_coverflowwidget.cpp asserts through them).
  [[nodiscard]] int cornerRadius() const { return m_cornerRadius; }
  [[nodiscard]] int fontSize() const { return m_fontSize; }
  [[nodiscard]] bool hideTitles() const { return m_hideTitles; }
  [[nodiscard]] QString tileColor() const { return m_tileColor; }
  /// Number of decoded source pixmaps currently cached (artwork-path keyed).
  [[nodiscard]] int pixmapCacheSize() const { return static_cast<int>(m_pixmapCache.size()); }

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
  // Drop palette-derived placeholder/thumbnail caches when the system palette
  // changes at runtime so they re-render against the new accent on next paint.
  void changeEvent(QEvent *e) override;

private:
  // Cards drawn on each side of center. A class static (not a TU-local
  // constant) because the split partials — paint/layout, keyboard paging,
  // and pixmap-cache windowing — all need the same value.
  static constexpr int kVisibleSideCards = 5;

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
  // Center-card overlap reorder (Kartend-c91pg). When the glide has carried
  // the carousel more than halfway toward a neighbour (|frac| > 0.5), that
  // neighbour is geometrically closer to the stage center than the nominal
  // center card, so it must paint on top. Reorders @p layouts in place so the
  // center card sits second-to-last and the closer neighbour last, preserving
  // the exact z-order the previous erase-and-re-find block produced — without
  // the iterator-fragile erase/std::find_if dance.
  void ensureCenterCardOnTop(QList<CardLayout> &layouts, int center, qreal frac) const;
  [[nodiscard]] int hitTestCard(const QPoint &pt) const;
  // Returns the pixmap to draw for @p idx. On a miss in m_pixmapCache this
  // schedules the decode and returns a PLACEHOLDER for the meantime, so the
  // return value does not always correspond to the card's artworkPath.
  // @p sourcePath (optional) reports which image the pixmap actually is:
  // the resolved artwork/gallery path, or EMPTY for a placeholder. Callers
  // that key a cache on the pixmap must key it on this, not on
  // m_cards[idx].artworkPath — doing the latter stored a scaled placeholder
  // under the real artwork's key and pinned it there (Kartend-ce0b4).
  [[nodiscard]] QPixmap pixmapForIndex(int idx, QString *sourcePath = nullptr);
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
  // completion. Snapshots @p sourcePm as a QImage before dispatch — the
  // worker never touches QPixmap (GUI-thread-only). See Kartend-g6ft for
  // rationale.
  void requestScaledPixmap(const CoverFlowScaledKey &key, const QPixmap &sourcePm,
                           const QSize &targetSize);
  void cancelPendingScales();
  void cancelPendingLoads();

  // paintEvent render helpers (Kartend-c91pg). Each draws one concern for a
  // single card; paintEvent stays the orchestrator (backdrop, empty state,
  // selection border, title strip, gallery). The painter arrives with the
  // per-card opacity + perspective transform already applied and is restored
  // by the caller, so these helpers do not touch the transform stack except
  // where noted (renderCardReflection save()/restore()s its own flip).
  //
  // renderCardPixmap resolves the scaled pixmap for @p c from source @p pm
  // (cache hit -> Smooth entry; miss -> @p pm scaled at draw time with
  // FastTransformation while a worker fills the cache), draws the
  // (corner-radius-clipped) card, and writes the resolved pixmap + its
  // draw-size back out via @p scaled / @p scaledDrawSize so
  // renderCardReflection reuses them. @p useFastTransform returns whether the
  // miss path disabled SmoothPixmapTransform on @p painter.
  // @p sourcePath identifies what @p pm actually is (see pixmapForIndex);
  // empty means placeholder, which bypasses the scaled cache entirely.
  void renderCardPixmap(QPainter &painter, const CardLayout &c, const QPixmap &pm,
                        const QString &sourcePath, QPixmap &scaled, QSize &scaledDrawSize,
                        bool &useFastTransform);
  // Draws the flipped, faded reflection beneath the card using the already-
  // resolved @p scaled pixmap, then the bottom-fade gradient toward @p bg.
  void renderCardReflection(QPainter &painter, const CardLayout &c, const QPixmap &scaled,
                            qreal opacity, const QColor &bg);
  // Draws the centered card's elided title strip beneath the carousel,
  // above the optional gallery thumbnail row.
  void renderTitleStrip(QPainter &painter);
  void pruneScaledPixmapCache();
  void prunePixmapCache();
  // Re-derive the centered card's rect (m_lastCenterRect) from the current
  // layout inputs and reposition the video preview when it changed. Called
  // from the state setters that move the center (selection, glide frame,
  // resize, title/gallery slot changes) — NEVER from paintEvent: mutating
  // child geometry inside a paint pass schedules another layout/paint round
  // per glide frame (paint-driven layout). Paint and the preview helpers
  // below only read m_lastCenterRect.
  void refreshCenterRect();
  void updateVideoPreviewGeometry();
  void applyVideoPreviewState();
  // Mirror the centered card into the widget-level accessible name /
  // description ("<title>, item N of M"). The carousel is one custom-
  // painted widget — assistive tech can't see individual cards, so this
  // is the only channel that carries the selection. Called from every
  // mutation of m_selectedIndex or the card list.
  void updateAccessibleSelection();

  // Gallery toolbar moved to CoverFlowGalleryStrip (Kartend-y3ia step 1).
  // m_galleryStrip below is the owner; its public methods replace the
  // four declarations that lived here.
  CoverFlowGalleryStrip *m_galleryStrip = nullptr;

  QList<CoverFlowCardData> m_cards;
  int m_selectedIndex = 0;
  qreal m_selectionPositionF = 0.0; ///< animated offset (-N .. +N) added to m_selectedIndex
  QPropertyAnimation *m_glide = nullptr;

  QHash<QString, QPixmap> m_pixmapCache;
  // Workers deliver QImage; the finished slots convert to QPixmap on the
  // GUI thread (QPixmap must not be constructed or scaled off it).
  QHash<QString, QFutureWatcher<QImage> *> m_pendingLoads;

  // Cache of source pixmaps already scaled to card-target size + smooth
  // transform, keyed on "path|WxH" (Kartend-g6ft). paintEvent's
  // pm.scaled(..., Smooth) call ran ~11 times per paint (kVisibleSideCards*2+1)
  // and dominated the per-frame cost (profile: p50 paint = 20.9ms, well
  // over the 16ms 60fps budget). Population happens on a worker thread via
  // requestScaledPixmap so a cache miss never blocks paint. Invalidated on
  // resize, setCards, and setTileColor — between those events every paint
  // hits, and the steady-state paint cost drops to ~8-11ms.
  QHash<CoverFlowScaledKey, QPixmap> m_scaledPixmapCache;
  QHash<CoverFlowScaledKey, QFutureWatcher<QImage> *> m_pendingScales;

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
  // The widget is sized + repositioned by refreshCenterRect() as the center
  // card moves (selection change, glide frames, resizes); when m_videoMode
  // is false or the centered card has no videoPath, the preview is hidden
  // so the painter renders the artwork normally.
  VideoPreviewWidget *m_videoPreview = nullptr;
  bool m_videoMode = false;
  int m_videoPreviewIndex = -1;
  /// Centered card's rect, maintained by refreshCenterRect(); read-only
  /// everywhere else (video-preview positioning).
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
