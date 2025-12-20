#ifndef SEARCHLOADINGOVERLAY_H
#define SEARCHLOADINGOVERLAY_H

#include <QObject>
#include <QPointer>

class QWidget;
class QLabel;
class QPropertyAnimation;
class QTimer;

/**
 * @brief Manages a subtle loading overlay shown during search operations.
 * 
 * The overlay covers the scroll area with a semi-transparent background
 * to hide non-filtered items while search results are being computed.
 * Includes a subtle pulsing animation to indicate activity.
 */
class SearchLoadingOverlay : public QObject {
  Q_OBJECT
public:
  explicit SearchLoadingOverlay(QObject *parent = nullptr);
  ~SearchLoadingOverlay() override;

  /// Set the parent widget (scroll area viewport) for the overlay
  void setParentWidget(QWidget *parent);

  /// Show the loading overlay with fade-in animation
  void show();

  /// Hide the loading overlay with fade-out animation
  void hide();

  /// Immediately hide without animation (for cleanup)
  void hideImmediate();

  /// Check if overlay is currently visible
  [[nodiscard]] bool isVisible() const;

  /// Update overlay geometry to match parent
  void updateGeometry();

private:
  void ensureOverlay();
  void startPulseAnimation();
  void stopPulseAnimation();

  QWidget *m_parentWidget = nullptr;
  QPointer<QWidget> m_overlay;
  QPointer<QLabel> m_label;
  QPointer<QPropertyAnimation> m_fadeAnimation;
  QPointer<QPropertyAnimation> m_pulseAnimation;
  QTimer *m_pulseTimer = nullptr;
  bool m_pulseDimming = true;
};

#endif // SEARCHLOADINGOVERLAY_H
