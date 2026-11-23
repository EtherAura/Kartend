#ifndef BACKGROUNDIMAGEWIDGET_H
#define BACKGROUNDIMAGEWIDGET_H

#include <QPixmap>
#include <QRect>
#include <QRectF>
#include <QSizeF>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QEvent;
class QPaintEvent;
class QShowEvent;
QT_END_NAMESPACE

/// Image wallpaper painted behind the items grid, with optional parallax.
///
/// A child of the items viewport, lowered behind the scroll contents and
/// transparent for mouse events, like BackgroundVideoWidget. The image is
/// cover-fit to the viewport, so a wallpaper made for the screen looks the way
/// it was made at any device pixel ratio.
///
/// Parallax is PROPORTIONAL: the strength (0-100) adds a vertical margin of
/// that percentage of the viewport's height, the image is cover-fit to the
/// viewport plus that margin, and it slides up through the margin as the grid
/// scrolls from top to bottom. It therefore never runs out on a long list.
/// Strength 0 is a static wallpaper that stays put while the tiles scroll.
///
/// Replaces a viewport stylesheet that could not do either (Kartend-w0zi1):
/// the `QWidget { background-image }` rule also styled the plain-QWidget
/// scroll contents, which painted the image in its own coordinates and so
/// scrolled it 1:1 with the tiles, and QSS `background-position` accepts
/// alignment keywords only, so the pixel offset meant to drive parallax was
/// silently dropped.
class BackgroundImageWidget : public QWidget {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(BackgroundImageWidget)
public:
  explicit BackgroundImageWidget(QWidget *parent = nullptr);

  /// Show @p image. A null pixmap is equivalent to clear().
  void setImage(const QPixmap &image);
  void clear();
  [[nodiscard]] bool hasImage() const { return !m_source.isNull(); }

  /// 0 = static, 100 = drift through a margin as tall as the viewport.
  /// Clamped on input.
  void setParallaxStrength(int strength);
  [[nodiscard]] int parallaxStrength() const { return m_strength; }

  /// Where the grid is in its scroll range: 0 = top, 1 = bottom. Clamped.
  void setScrollFraction(qreal fraction);
  [[nodiscard]] qreal scrollFraction() const { return m_fraction; }

  /// Where the wallpaper lands in a view of @p viewSize, in view coordinates:
  /// cover-fit to the view plus `strength`% of its height, centred
  /// horizontally, slid up through that margin as @p scrollFraction goes from
  /// 0 to 1. Any height beyond the margin (an image taller than the view's
  /// aspect needs) is split evenly above and below the drift band. Empty if
  /// either size is empty.
  [[nodiscard]] static QRectF paintRect(const QSizeF &imageSize, const QSizeF &viewSize,
                                        int strength, qreal scrollFraction);

  /// A scroll bar's position as a 0-1 fraction of its range; 0 when it has
  /// no range, so a grid that cannot scroll shows the same framing as one
  /// scrolled to the top.
  [[nodiscard]] static qreal scrollFractionOf(int value, int minimum, int maximum);

protected:
  void paintEvent(QPaintEvent *event) override;
  void showEvent(QShowEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  QPixmap m_source;
  // The visible columns of the source scaled to the paint rect at device
  // pixels. Its size does not depend on the scroll position, so scrolling
  // only moves it; it is rebuilt when the viewport, strength, image or device
  // pixel ratio changes. m_scaledColumns is the source rect it was cut from.
  QPixmap m_scaled;
  QRect m_scaledColumns;
  int m_strength = 0;
  qreal m_fraction = 0.0;
};

#endif // BACKGROUNDIMAGEWIDGET_H
