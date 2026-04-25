#ifndef ERRORDIALOG_H
#define ERRORDIALOG_H

#include "errorutils.h"
#include <QDialog>
#include <QPointer>

QT_BEGIN_NAMESPACE
class QLabel;
class QTextEdit;
class QPushButton;
class QVBoxLayout;
QT_END_NAMESPACE

/**
 * @brief Dialog for displaying errors to users with optional details.
 *
 * Provides user-friendly error messages with expandable technical details.
 * Supports different severity levels with appropriate icons and styling.
 */
class ErrorDialog : public QDialog {
  Q_OBJECT
public:
  explicit ErrorDialog(QWidget *parent = nullptr);

  // Set error content from ErrorContext
  void setError(const ErrorUtils::ErrorContext &context);

  // Convenience static method to show error dialog
  static void showError(QWidget *parent, const ErrorUtils::ErrorContext &context);

  // Show critical error with option to quit or continue
  static bool showCriticalError(QWidget *parent, const ErrorUtils::ErrorContext &context,
                                bool allowContinue = true);

private slots:
  void toggleDetails();
  void copyToClipboard();

private:
  void setupUI();
  void updateIcon(ErrorUtils::Severity severity);
  [[nodiscard]] QString formatUserMessage(const ErrorUtils::ErrorContext &context) const;
  [[nodiscard]] QString formatTechnicalDetails(const ErrorUtils::ErrorContext &context) const;

  QLabel *m_iconLabel = nullptr;
  QLabel *m_messageLabel = nullptr;
  QTextEdit *m_detailsEdit = nullptr;
  QPushButton *m_detailsButton = nullptr;
  QPushButton *m_copyButton = nullptr;
  QPushButton *m_okButton = nullptr;
  QPushButton *m_continueButton = nullptr;
  QPushButton *m_quitButton = nullptr;
  QVBoxLayout *m_mainLayout = nullptr;

  ErrorUtils::ErrorContext m_context;
  bool m_detailsVisible = false;
};

#endif // ERRORDIALOG_H
