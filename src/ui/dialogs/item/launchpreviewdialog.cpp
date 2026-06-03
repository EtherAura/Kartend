#include "launchpreviewdialog.h"

#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QLabel>
#include <QPlainTextEdit>
#include <QVBoxLayout>

#include "uiconstants/color.h"

namespace {

QString quoteIfNeeded(const QString &arg) {
  // Wrap arguments containing whitespace in quotes for display so the
  // user can read a shell-style line without misparsing multi-word
  // arguments. Pure display formatting — the real launch uses the
  // QProcess argument list, which is immune to whitespace.
  if (arg.contains(QLatin1Char(' ')) || arg.contains(QLatin1Char('\t'))) {
    return QStringLiteral("\"") + QString(arg).replace(QLatin1Char('"'), QStringLiteral("\\\"")) +
           QStringLiteral("\"");
  }
  return arg;
}

QString joinForDisplay(const QString &program, const QStringList &args) {
  QStringList parts;
  parts.reserve(args.size() + 1);
  parts.append(quoteIfNeeded(program));
  for (const QString &a : args) {
    parts.append(quoteIfNeeded(a));
  }
  return parts.join(QLatin1Char(' '));
}

} // namespace

LaunchPreviewDialog::LaunchPreviewDialog(QWidget *parent) : QDialog(parent) {
  setupUi();
}

void LaunchPreviewDialog::setupUi() {
  setWindowTitle(tr("Launch preview"));
  resize(620, 460);

  auto *outer = new QVBoxLayout(this);

  m_headerLabel = new QLabel(this);
  m_headerLabel->setWordWrap(true);
  outer->addWidget(m_headerLabel);

  m_launcherLabel = new QLabel(this);
  m_launcherLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_launcherLabel->setWordWrap(true);
  outer->addWidget(m_launcherLabel);

  m_resolvedLabel = new QLabel(this);
  m_resolvedLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_resolvedLabel->setWordWrap(true);
  outer->addWidget(m_resolvedLabel);

  m_fileLabel = new QLabel(this);
  m_fileLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_fileLabel->setWordWrap(true);
  outer->addWidget(m_fileLabel);

  m_archiveLabel = new QLabel(this);
  m_archiveLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_archiveLabel->setWordWrap(true);
  outer->addWidget(m_archiveLabel);

  auto *cmdHeading = new QLabel(tr("Command:"), this);
  outer->addWidget(cmdHeading);

  m_commandView = new QPlainTextEdit(this);
  m_commandView->setReadOnly(true);
  // Monospace so the user can scan the args without proportional drift.
  m_commandView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  outer->addWidget(m_commandView, /*stretch=*/1);

  m_warningsLabel = new QLabel(this);
  m_warningsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_warningsLabel->setWordWrap(true);
  // Red-ish accent so warnings catch the user's eye against the rest of
  // the read-only text. palette() colors don't have a "warning" slot so a
  // muted red works in both light and dark themes.
  m_warningsLabel->setStyleSheet(UIConstants::Color::errorLabelStyleSheet(true));
  outer->addWidget(m_warningsLabel);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  outer->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
}

void LaunchPreviewDialog::setPreview(const QString &itemTitle, const QString &launcherName,
                                     const QString &filePath, const LaunchPreview &preview) {
  m_headerLabel->setText(itemTitle.isEmpty()
                             ? tr("Launch preview")
                             : tr("Launch preview for: <b>%1</b>").arg(itemTitle.toHtmlEscaped()));

  const QString launcherDisplay = launcherName.isEmpty() ? tr("(unnamed)") : launcherName;
  m_launcherLabel->setText(tr("Launcher: %1").arg(launcherDisplay));

  if (preview.resolvedProgram.isEmpty()) {
    m_resolvedLabel->setText(tr("Resolved executable: <i>(not found on PATH or disk)</i>"));
  } else {
    m_resolvedLabel->setText(
        tr("Resolved executable: %1").arg(preview.resolvedProgram.toHtmlEscaped()));
  }

  const QString fileStatus = preview.fileExists ? tr("exists") : tr("MISSING");
  m_fileLabel->setText(tr("File: %1 — %2").arg(filePath.toHtmlEscaped(), fileStatus));

  if (preview.wouldExtractArchive) {
    const QString ext = preview.archiveTargetExtension.trimmed().isEmpty()
                            ? tr("(no target extension set)")
                            : QStringLiteral("*.") + preview.archiveTargetExtension;
    m_archiveLabel->setText(tr("Archive handling: would extract to a temp dir, then launch %1 from "
                               "inside.")
                                .arg(ext));
    m_archiveLabel->setVisible(true);
  } else {
    m_archiveLabel->clear();
    m_archiveLabel->setVisible(false);
  }

  if (preview.buildOk) {
    m_commandView->setPlainText(joinForDisplay(preview.program, preview.arguments));
  } else {
    m_commandView->setPlainText(tr("(no command — see warnings below)"));
  }

  if (preview.warnings.isEmpty()) {
    m_warningsLabel->setText(tr("No problems detected."));
    m_warningsLabel->setStyleSheet("color: palette(highlight); font-weight: bold;");
  } else {
    QStringList lines;
    lines.reserve(preview.warnings.size());
    for (const QString &w : preview.warnings) {
      lines.append(QStringLiteral("• ") + w);
    }
    m_warningsLabel->setText(lines.join(QLatin1Char('\n')));
    m_warningsLabel->setStyleSheet(UIConstants::Color::errorLabelStyleSheet(true));
  }
}
