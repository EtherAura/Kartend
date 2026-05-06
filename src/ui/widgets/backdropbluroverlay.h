#ifndef BACKDROPBLUROVERLAY_H
#define BACKDROPBLUROVERLAY_H

#include <QPixmap>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QEvent;
class QGraphicsBlurEffect;
class QPaintEvent;
QT_END_NAMESPACE

/// Kartend-eq8r: simulated backdrop blur. Paints a copy of the wallpaper
/// scaled cover-fit to the widget's rect, with a QGraphicsBlurEffect on
/// the widget itself so Qt blurs the rendered output. Used as a child of
/// the items top bar to mimic macOS Vibrancy: the toolbar shows a blurred
/// version of the wallpaper instead of a flat color.
///
/// Mouse-event transparent so toolbar buttons layered above stay clickable.
/// True backdrop sampling (CSS backdrop-filter) isn't feasible in Qt6
/// without a layout restructure or custom GL shader, so this is "good
/// enough" — visually themed rather than pixel-accurate continuation of
/// the wallpaper.
class BackdropBlurOverlay : public QWidget {
  Q_OBJECT
public:
  explicit BackdropBlurOverlay(QWidget *parent = nullptr);

  /// Set the source image (typically the per-collection wallpaper) and the
  /// blur radius. Empty pixmap clears the overlay.
  void setSource(const QPixmap &source, int blurRadius);

  void clear();

protected:
  void paintEvent(QPaintEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;
  void showEvent(QShowEvent *event) override;

private:
  QPixmap m_pixmap;
  QGraphicsBlurEffect *m_blurEffect = nullptr;
};

#endif // BACKDROPBLUROVERLAY_H
