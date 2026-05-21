#ifndef VIGNETTEOVERLAY_H
#define VIGNETTEOVERLAY_H

#include <QWidget>

QT_BEGIN_NAMESPACE
class QPaintEvent;
QT_END_NAMESPACE

/// edge-darkening vignette painted over the items viewport.
/// Uses a radial gradient that's transparent at center and progressively
/// darker toward the corners, controlled by `intensity` (0-100). Mouse-event
/// transparent so the items grid below stays interactive.
class VignetteOverlay : public QWidget {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(VignetteOverlay)
public:
  explicit VignetteOverlay(QWidget *parent = nullptr);

  /// 0 = fully transparent corners (no effect); 100 = fully opaque black at
  /// the corners. Clamped on input.
  void setIntensity(int percent);
  [[nodiscard]] int intensity() const { return m_intensity; }

protected:
  void paintEvent(QPaintEvent *event) override;
  void showEvent(QShowEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  int m_intensity = 60;
};

#endif // VIGNETTEOVERLAY_H
