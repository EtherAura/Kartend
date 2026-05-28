// Error dialog for displaying user-facing error messages
#include "errordialog.h"
#include "errorpresentation.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QTextEdit>
#include <QVBoxLayout>

namespace {
constexpr int DIALOG_MIN_WIDTH = 400;
constexpr int DIALOG_MAX_WIDTH = 600;
constexpr int DETAILS_HEIGHT = 150;
constexpr int ICON_SIZE = 48;
} // namespace

ErrorDialog::ErrorDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle(tr("Error"));
  setModal(true);
  setupUI();
}

void ErrorDialog::setupUI() {
  m_mainLayout = new QVBoxLayout(this);
  m_mainLayout->setSpacing(16);
  m_mainLayout->setContentsMargins(20, 20, 20, 20);

  // Top section: icon + message
  auto *topLayout = new QHBoxLayout();
  topLayout->setSpacing(16);

  m_iconLabel = new QLabel(this);
  m_iconLabel->setFixedSize(ICON_SIZE, ICON_SIZE);
  m_iconLabel->setAlignment(Qt::AlignTop);
  topLayout->addWidget(m_iconLabel);

  m_messageLabel = new QLabel(this);
  m_messageLabel->setWordWrap(true);
  m_messageLabel->setMinimumWidth(DIALOG_MIN_WIDTH - ICON_SIZE - 60);
  m_messageLabel->setMaximumWidth(DIALOG_MAX_WIDTH - ICON_SIZE - 60);
  m_messageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  topLayout->addWidget(m_messageLabel, 1);

  m_mainLayout->addLayout(topLayout);

  // Details section (initially hidden)
  m_detailsEdit = new QTextEdit(this);
  m_detailsEdit->setReadOnly(true);
  m_detailsEdit->setFixedHeight(DETAILS_HEIGHT);
  m_detailsEdit->setVisible(false);
  m_detailsEdit->setStyleSheet("QTextEdit { background-color: palette(window); "
                               "font-family: monospace; font-size: 11px; }");
  m_mainLayout->addWidget(m_detailsEdit);

  // Button row
  auto *buttonLayout = new QHBoxLayout();
  buttonLayout->setSpacing(8);

  m_detailsButton = new QPushButton(tr("Show Details"), this);
  m_detailsButton->setCheckable(true);
  connect(m_detailsButton, &QPushButton::clicked, this, &ErrorDialog::toggleDetails);
  buttonLayout->addWidget(m_detailsButton);

  m_copyButton = new QPushButton(tr("Copy"), this);
  m_copyButton->setToolTip(tr("Copy error details to clipboard"));
  connect(m_copyButton, &QPushButton::clicked, this, &ErrorDialog::copyToClipboard);
  buttonLayout->addWidget(m_copyButton);

  buttonLayout->addStretch();

  // Standard buttons (OK for normal, Continue/Quit for critical)
  m_okButton = new QPushButton(tr("OK"), this);
  m_okButton->setDefault(true);
  connect(m_okButton, &QPushButton::clicked, this, &QDialog::accept);
  buttonLayout->addWidget(m_okButton);

  m_continueButton = new QPushButton(tr("Continue"), this);
  m_continueButton->setVisible(false);
  connect(m_continueButton, &QPushButton::clicked, this, &QDialog::accept);
  buttonLayout->addWidget(m_continueButton);

  m_quitButton = new QPushButton(tr("Quit"), this);
  m_quitButton->setVisible(false);
  connect(m_quitButton, &QPushButton::clicked, this, &QDialog::reject);
  buttonLayout->addWidget(m_quitButton);

  m_mainLayout->addLayout(buttonLayout);

  setMinimumWidth(DIALOG_MIN_WIDTH);
  setMaximumWidth(DIALOG_MAX_WIDTH);
}

void ErrorDialog::setError(const ErrorUtils::ErrorContext &context) {
  m_context = context;

  updateIcon(context.severity);
  m_messageLabel->setText(formatUserMessage(context));
  m_detailsEdit->setText(formatTechnicalDetails(context));

  // Show/hide details button based on whether we have details
  bool hasDetails = !context.details.isEmpty() || !context.source.isEmpty();
  m_detailsButton->setVisible(hasDetails);
  m_copyButton->setVisible(hasDetails);

  // Update window title based on severity
  switch (context.severity) {
  case ErrorUtils::Severity::Info:
    setWindowTitle(tr("Information"));
    break;
  case ErrorUtils::Severity::Warning:
    setWindowTitle(tr("Warning"));
    break;
  case ErrorUtils::Severity::Error:
    setWindowTitle(tr("Error"));
    break;
  case ErrorUtils::Severity::Critical:
    setWindowTitle(tr("Critical Error"));
    break;
  }

  adjustSize();
}

void ErrorDialog::updateIcon(ErrorUtils::Severity severity) {
  QStyle::StandardPixmap pixmap;
  switch (severity) {
  case ErrorUtils::Severity::Info:
    pixmap = QStyle::SP_MessageBoxInformation;
    break;
  case ErrorUtils::Severity::Warning:
    pixmap = QStyle::SP_MessageBoxWarning;
    break;
  case ErrorUtils::Severity::Error:
  case ErrorUtils::Severity::Critical:
    pixmap = QStyle::SP_MessageBoxCritical;
    break;
  }

  QIcon icon = style()->standardIcon(pixmap);
  m_iconLabel->setPixmap(icon.pixmap(ICON_SIZE, ICON_SIZE));
}

auto ErrorDialog::formatUserMessage(const ErrorUtils::ErrorContext &context) const -> QString {
  QString message = context.message;

  // Add user-friendly suggestions based on error code
  switch (context.code) {
  case ErrorUtils::ErrorCode::DatabaseConnectionFailed:
  case ErrorUtils::ErrorCode::DatabaseNotOpen:
    message += tr("\n\nPlease check that the database file exists and is accessible.");
    break;
  case ErrorUtils::ErrorCode::MediaDirectoryNotFound:
    message += tr("\n\nPlease verify the media directory path in settings.");
    break;
  case ErrorUtils::ErrorCode::ArtworkDirectoryNotFound:
    message += tr("\n\nPlease verify the artwork directory path in settings.");
    break;
  case ErrorUtils::ErrorCode::ConfigLoadFailed:
  case ErrorUtils::ErrorCode::ConfigSaveFailed:
    message += tr("\n\nPlease check file permissions for the configuration file.");
    break;
  case ErrorUtils::ErrorCode::InvalidFilePath:
    message += tr("\n\nPlease check that the file path is valid.");
    break;
  default:
    break;
  }

  return message;
}

auto ErrorDialog::formatTechnicalDetails(const ErrorUtils::ErrorContext &context) const -> QString {
  QStringList lines;

  lines << tr("Error Code: %1 (%2)")
               .arg(static_cast<int>(context.code))
               .arg(ErrorUtils::errorCodeToString(context.code));

  if (!context.source.isEmpty()) {
    lines << tr("Source: %1").arg(context.source);
  }

  if (!context.details.isEmpty()) {
    lines << QString() << tr("Details:") << context.details;
  }

  return lines.join("\n");
}

void ErrorDialog::toggleDetails() {
  m_detailsVisible = !m_detailsVisible;
  m_detailsEdit->setVisible(m_detailsVisible);
  m_detailsButton->setText(m_detailsVisible ? tr("Hide Details") : tr("Show Details"));
  adjustSize();
}

void ErrorDialog::copyToClipboard() {
  QString text = m_context.message;
  if (!m_context.source.isEmpty()) {
    text += "\nSource: " + m_context.source;
  }
  if (!m_context.details.isEmpty()) {
    text += "\nDetails: " + m_context.details;
  }
  text += "\nError Code: " + QString::number(static_cast<int>(m_context.code));

  QApplication::clipboard()->setText(text);
}

void ErrorDialog::showError(QWidget *parent, const ErrorUtils::ErrorContext &context) {
  ErrorDialog dialog(parent);
  dialog.setError(context);
  dialog.exec();
}

auto ErrorDialog::showCriticalError(QWidget *parent, const ErrorUtils::ErrorContext &context,
                                    bool allowContinue) -> bool {
  ErrorDialog dialog(parent);
  dialog.setError(context);

  // Switch to critical mode buttons
  dialog.m_okButton->setVisible(false);
  dialog.m_continueButton->setVisible(allowContinue);
  dialog.m_quitButton->setVisible(true);

  if (allowContinue) {
    dialog.m_continueButton->setDefault(true);
  } else {
    dialog.m_quitButton->setDefault(true);
  }

  return dialog.exec() == QDialog::Accepted;
}

// Kartend-hx6l: the ErrorPresentation namespace implementation moved
// to src/utils/app/errorpresentation_default.cpp so kartend_data callers
// (settingsdialogcontroller.cpp) don't drag a kartend_data → kartend_ui
// link edge into every test executable. Production binaries register
// the ErrorDialog-backed override at MainWindow startup (see
// mainwindow_setup.cpp). The header in src/api/errorpresentation.h
// still declares the public entry points; ErrorDialog::showError /
// showCriticalError above remain as the concrete dialog impl callers
// register via setShowErrorOverride.
