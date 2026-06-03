#include "collectionhealthdialog.h"

#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QLabel>
#include <QTextEdit>
#include <QVBoxLayout>

#include "uiconstants/color.h"

CollectionHealthDialog::CollectionHealthDialog(QWidget *parent) : QDialog(parent) {
  setupUi();
}

void CollectionHealthDialog::setupUi() {
  setWindowTitle(tr("Collection health"));
  resize(640, 520);

  auto *outer = new QVBoxLayout(this);

  m_headerLabel = new QLabel(this);
  m_headerLabel->setWordWrap(true);
  outer->addWidget(m_headerLabel);

  m_summaryLabel = new QLabel(this);
  m_summaryLabel->setWordWrap(true);
  m_summaryLabel->setStyleSheet("font-weight: bold;");
  outer->addWidget(m_summaryLabel);

  m_detailView = new QTextEdit(this);
  m_detailView->setReadOnly(true);
  m_detailView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  // Use plain text so the file paths render as-is. Rich text would
  // interpret < / > / & in unusual filenames.
  m_detailView->setLineWrapMode(QTextEdit::NoWrap);
  outer->addWidget(m_detailView, /*stretch=*/1);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  outer->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void CollectionHealthDialog::setReport(const QString &collectionName,
                                       const CollectionHealth::Report &report) {
  m_headerLabel->setText(tr("Health report for: <b>%1</b>").arg(collectionName.toHtmlEscaped()));

  // Single-line summary at the top so the user can scan severity without
  // reading the detail panel.
  const int issueCount =
      report.missingFileCount + report.missingArtworkCount + report.launcherIssues.size();
  if (issueCount == 0) {
    m_summaryLabel->setText(tr("No issues detected across %1 item(s).").arg(report.totalItems));
    m_summaryLabel->setStyleSheet("color: palette(highlight); font-weight: bold;");
  } else {
    m_summaryLabel->setText(
        tr("%1 issue(s) across %2 item(s).").arg(issueCount).arg(report.totalItems));
    m_summaryLabel->setStyleSheet(UIConstants::Color::errorLabelStyleSheet(true));
  }

  // Detail panel: per-category section, each with the count and a
  // truncated sample list. The trailing "and N more…" line is the user's
  // cue that the dashboard caps samples — see CollectionHealth::kMaxSamples.
  QStringList sections;

  const auto formatSamples = [&](const QString &label, int count, const QStringList &samples) {
    if (count <= 0) {
      sections << tr("%1: 0").arg(label);
      return;
    }
    QString block = tr("%1: %2").arg(label).arg(count);
    if (!samples.isEmpty()) {
      block += QLatin1Char('\n');
      for (const QString &s : samples) {
        block += QStringLiteral("  ") + s + QLatin1Char('\n');
      }
      const int overflow = count - samples.size();
      if (overflow > 0) {
        block += tr("  …and %1 more").arg(overflow);
      }
    }
    sections << block;
  };

  formatSamples(tr("Missing files"), report.missingFileCount, report.missingFileSamples);
  formatSamples(tr("Missing artwork"), report.missingArtworkCount, report.missingArtworkSamples);

  // Launcher issues are short structured records, not paths, so we
  // render them with their own block style.
  if (report.launcherIssues.isEmpty()) {
    sections << tr("Launcher validity: OK");
  } else {
    QString block = tr("Launcher issues: %1").arg(report.launcherIssues.size());
    block += QLatin1Char('\n');
    for (const CollectionHealth::LauncherIssue &issue : report.launcherIssues) {
      block += QStringLiteral("  [%1] %2: %3")
                   .arg(issue.launcherIndex)
                   .arg(issue.launcherLabel, issue.issue);
      block += QLatin1Char('\n');
    }
    sections << block;
  }

  m_detailView->setPlainText(sections.join(QStringLiteral("\n\n")));
}
