#ifndef LOADINGOVERLAY_H
#define LOADINGOVERLAY_H

#include <QLabel>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

class OverlayZOrderRegistry;

/**
 * @brief Semi-transparent overlay widget showing loading state.
 *
 * Displays a spinner animation, status message, and optional progress bar.
 * Automatically positions itself over its parent widget.
 */
class LoadingOverlay : public QWidget {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(LoadingOverlay)
  Q_PROPERTY(int spinnerAngle READ spinnerAngle WRITE setSpinnerAngle)

public:
  explicit LoadingOverlay(QWidget *parent = nullptr);
  ~LoadingOverlay() override = default;

  /// Show overlay with message (indeterminate progress)
  void show(const QString &message = "Loading...");

  /// Show overlay with determinate progress (0-100)
  void showWithProgress(const QString &message, int current, int total);

  /// Update progress while visible
  void setProgress(int current, int total);

  /// Update message while visible
  void setMessage(const QString &message);

  /// Hide overlay with optional fade animation
  void hide(bool animated = true);

  /// Check if overlay is currently visible
  [[nodiscard]] bool isActive() const { return m_active; }

  // Spinner angle for animation
  [[nodiscard]] int spinnerAngle() const { return m_spinnerAngle; }
  void setSpinnerAngle(int angle);

  /// See OverlayZOrderRegistry. Routes show() / showWithProgress() raise()
  /// through the central z-order coordinator when installed.
  void setLayerManager(OverlayZOrderRegistry *manager) { m_layerManager = manager; }

protected:
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void setupUI();
  void startSpinnerAnimation();
  void stopSpinnerAnimation();
  void updatePosition();
  void updateAccentColor();
  void updateSpinnerPosition();

  QLabel *m_messageLabel = nullptr;
  QProgressBar *m_progressBar = nullptr;
  QWidget *m_contentWidget = nullptr;
  QWidget *m_spinnerWidget = nullptr;

  QPropertyAnimation *m_spinnerAnimation = nullptr;
  QPropertyAnimation *m_fadeAnimation = nullptr;

  int m_spinnerAngle = 0;
  bool m_active = false;
  bool m_showProgress = false;
  QColor m_accentColor;
  OverlayZOrderRegistry *m_layerManager = nullptr;
};

#endif // LOADINGOVERLAY_H
