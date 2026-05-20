#ifndef TEXTZOOMHUD_H
#define TEXTZOOMHUD_H

#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QPropertyAnimation;
class QTimer;
QT_END_NAMESPACE

class OverlayLayerManager;

/// Transient on-screen indicator that surfaces the current text-zoom percent
/// for ~1s on each Ctrl+/-/0 press. Mirrors browser/IDE zoom HUDs so the user
/// has an immediate visual confirmation and a current-value reference.
///
/// Parented to MainWindow's central widget. Repositions itself top-center
/// when its parent resizes via eventFilter — there's no layout participation.
class TextZoomHud : public QWidget {
public:
  explicit TextZoomHud(QWidget *parent = nullptr);
  ~TextZoomHud() override;

  /// Display @p percent. Repeated calls reset the auto-hide timer so a rapid
  /// Ctrl++ burst keeps the HUD continuously visible until the user stops.
  void showZoom(int percent);

  /// See OverlayLayerManager. Routes showZoom()'s raise() through the
  /// central z-order coordinator when installed.
  void setLayerManager(OverlayLayerManager *manager) { m_layerManager = manager; }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void updatePosition();
  void scheduleHide();

  QLabel *m_label = nullptr;
  QPropertyAnimation *m_fadeAnimation = nullptr;
  QTimer *m_holdTimer = nullptr;
  OverlayLayerManager *m_layerManager = nullptr;
};

#endif // TEXTZOOMHUD_H
