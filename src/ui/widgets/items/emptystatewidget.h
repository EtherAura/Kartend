#ifndef EMPTYSTATEWIDGET_H
#define EMPTYSTATEWIDGET_H

#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QProgressBar;
class QPropertyAnimation;
class QVBoxLayout;
QT_END_NAMESPACE

/**
 * @brief Contextual empty/loading state widget shown in place of items.
 *
 * Replaces the bare "No items found" QLabel with a richer presentation:
 *   - An emoji icon for visual cue
 *   - A primary message
 *   - An optional secondary hint
 *   - An optional progress bar (determinate or indeterminate)
 *   - An optional rotating spinner
 *
 * Common usage:
 *   widget->showMessage("No items found", "Add files to the media directory.");
 *   widget->showSearchEmpty("foo");
 *   widget->showScanning(123, 500);
 */
class EmptyStateWidget : public QWidget {
  Q_OBJECT
  Q_PROPERTY(int spinnerAngle READ spinnerAngle WRITE setSpinnerAngle)

public:
  explicit EmptyStateWidget(QWidget *parent = nullptr);
  ~EmptyStateWidget() override;

  /// Show a message with optional hint and icon (emoji string).
  void showMessage(const QString &message, const QString &hint = QString(),
                   const QString &icon = QString());

  /// Convenience: "No matches for '<query>'" with hint to clear search.
  void showSearchEmpty(const QString &query);

  /// Convenience: indicate the collection has no media directory configured.
  void showNoMediaDirectory();

  /// Convenience: indicate a scan is in progress; pass total=0 for
  /// indeterminate progress (spinner pulses, no bar fill).
  void showScanning(int current = 0, int total = 0);

  /// Update progress while shown. total<=0 hides the bar (indeterminate).
  void setProgress(int current, int total);

  /// Hide the progress bar without hiding the widget.
  void clearProgress();

  /// Activate or deactivate the rotating spinner animation.
  void setSpinnerActive(bool active);

  /// Backwards-compat shim for old QLabel-style callers.
  /// Sets the primary message and clears hint/progress/spinner.
  void setText(const QString &text);

  // Spinner angle (animated property)
  [[nodiscard]] int spinnerAngle() const { return m_spinnerAngle; }
  void setSpinnerAngle(int angle);

private:
  void setupUI();
  void updateAccentColor();

  QLabel *m_iconLabel = nullptr;
  QLabel *m_messageLabel = nullptr;
  QLabel *m_hintLabel = nullptr;
  QProgressBar *m_progressBar = nullptr;
  QWidget *m_spinnerWidget = nullptr;

  QPropertyAnimation *m_spinnerAnimation = nullptr;

  int m_spinnerAngle = 0;
  QColor m_accentColor;
};

#endif // EMPTYSTATEWIDGET_H
