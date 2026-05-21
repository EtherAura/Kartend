#ifndef COVERFLOWGALLERYSTRIP_H
#define COVERFLOWGALLERYSTRIP_H

#include <QList>
#include <QObject>
#include <QPixmap>
#include <QPoint>
#include <QRect>

class CoverFlowWidget;
QT_BEGIN_NAMESPACE
class QPainter;
QT_END_NAMESPACE

namespace CoverFlowGalleryStripConstants {
inline constexpr int kThumbSize = 48;   ///< thumbnail edge in px
inline constexpr int kThumbSpacing = 8; ///< inter-thumbnail gap
inline constexpr int kStripHeight = 64; ///< vertical slot reserved at bottom
} // namespace CoverFlowGalleryStripConstants

/// Owns the per-item gallery toolbar that lives at the bottom of
/// CoverFlowWidget: thumbnail rect layout, paint, click hit-testing,
/// and the QHash<QString, QPixmap> thumb cache that backs the strip.
/// Lifts the gallery sub-widget out of the 1122-LOC coverflowwidget.cpp
/// (Kartend-y3ia first half) so the host's paintEvent + input handlers
/// only deal with the carousel cards.
///
/// Coupling: takes the host CoverFlowWidget via setHost so the helper
/// can reach m_gallery / m_galleryOwnerIndex / m_galleryActiveIndex /
/// m_galleryThumbCache, plus the widget geometry (width(), height(),
/// rect()) needed to compute thumbnail positions. Friend-of-host
/// pattern matches DetailsPaneGalleryView + DetailsPaneArtwork from
/// Kartend-5nxz / cd2u. State stays on the host so existing access
/// sites in coverflowwidget.cpp (setGalleryForIndex, mousePressEvent,
/// paintEvent's strip-invocation, etc.) don't change.
class CoverFlowGalleryStrip : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(CoverFlowGalleryStrip)

public:
  explicit CoverFlowGalleryStrip(QObject *parent = nullptr);

  /// Bind to the owning CoverFlowWidget. Idempotent — call once during
  /// CoverFlowWidget's setup.
  void setHost(CoverFlowWidget *host);

  /// Compute the thumbnail rects laid out left-to-right along the
  /// bottom of the host widget. Empty list when no gallery is active.
  [[nodiscard]] QList<QRect> thumbRects() const;
  /// Return the gallery entry index under @p pt, or -1 if no thumbnail
  /// is under the cursor. Called from CoverFlowWidget::mousePressEvent.
  [[nodiscard]] int hitTest(const QPoint &pt) const;
  /// Resolve (and cache) the QPixmap for a gallery entry. @p size is
  /// the target edge in pixels; the cache key folds in the size so
  /// rebuilds at different sizes don't blow the cache out.
  [[nodiscard]] QPixmap thumbPixmap(int entryIdx, int size);
  /// Paint the strip on @p painter using the host's current state.
  /// Called from CoverFlowWidget::paintEvent after the carousel cards
  /// have been drawn.
  void paint(QPainter &painter);

private:
  CoverFlowWidget *m_host = nullptr;
};

#endif // COVERFLOWGALLERYSTRIP_H
