#ifndef SPLASHOVERLAY_H
#define SPLASHOVERLAY_H

#include <QTimer>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QPropertyAnimation;
QT_END_NAMESPACE

class OverlayLayerManager;

/**
 * @brief Transient full-window splash overlay for startup and focus return.
 *
 * The overlay is parented to the main central widget, follows parent resizes,
 * auto-hides after a short timeout, and dismisses early on user input without
 * consuming that input.
 */
class SplashOverlay : public QWidget {
  Q_OBJECT
public:
  enum class Reason { Startup, FocusReturn };

  explicit SplashOverlay(QWidget *parent = nullptr);
  ~SplashOverlay() override;

  // Empty title/subtitle fall back to the built-in localized defaults.
  void showSplash(Reason reason, const QString &titleOverride = {},
                  const QString &subtitleOverride = {});
  void hideSplash(bool animated = true);
  [[nodiscard]] bool isActive() const { return m_active; }

  /// Register this overlay with the application's centralized z-order
  /// coordinator. After install, showSplash() routes its raise() through
  /// the manager so the overlay restacks against every other registered
  /// overlay in their documented order instead of relying on call-order
  /// luck. Safe to call once after construction; null restores legacy
  /// QWidget::raise() behaviour.
  void setLayerManager(OverlayLayerManager *manager) { m_layerManager = manager; }

protected:
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void setupUI();
  void updatePosition();
  void updateContent(Reason reason, const QString &titleOverride, const QString &subtitleOverride);
  [[nodiscard]] bool shouldDismissForEvent(QObject *watched, QEvent *event) const;
  [[nodiscard]] bool isObjectInOverlayWindow(QObject *watched) const;

  QWidget *m_cardWidget = nullptr;
  QLabel *m_iconLabel = nullptr;
  QLabel *m_titleLabel = nullptr;
  QLabel *m_subtitleLabel = nullptr;
  QLabel *m_versionLabel = nullptr;
  QPropertyAnimation *m_fadeAnimation = nullptr;
  QTimer m_hideTimer;
  bool m_active = false;
  OverlayLayerManager *m_layerManager = nullptr;
};

#endif // SPLASHOVERLAY_H
