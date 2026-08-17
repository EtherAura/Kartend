#include "kartpreflightdialog.h"

#include "uiconstants/color.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

KartPreflightDialog::KartPreflightDialog(const KartPreflight::PreflightReport &report,
                                         QWidget *parent)
    : QDialog(parent) {
  setupUi(report);
}

void KartPreflightDialog::setupUi(const KartPreflight::PreflightReport &report) {
  setWindowTitle(tr("Import Kart — Preflight"));
  resize(560, 420);

  auto *outer = new QVBoxLayout(this);

  auto *header =
      new QLabel(tr("Review the bundle before importing. Apply continues with extraction "
                    "and merge; Cancel aborts without writing anything to disk."),
                 this);
  header->setWordWrap(true);
  header->setStyleSheet(UIConstants::Color::MUTED_ITALIC_TEXT);
  outer->addWidget(header);

  m_summaryLabel = new QLabel(this);
  m_summaryLabel->setTextFormat(Qt::RichText);
  m_summaryLabel->setWordWrap(true);
  outer->addWidget(m_summaryLabel);

  m_statusBanner = new QLabel(this);
  m_statusBanner->setWordWrap(true);
  m_statusBanner->setMargin(8);
  outer->addWidget(m_statusBanner);

  m_issuesTree = new QTreeWidget(this);
  m_issuesTree->setColumnCount(3);
  m_issuesTree->setHeaderLabels({tr("Concern"), tr("Status"), tr("Path")});
  m_issuesTree->setRootIsDecorated(true);
  m_issuesTree->setUniformRowHeights(true);
  m_issuesTree->header()->setStretchLastSection(true);
  outer->addWidget(m_issuesTree, /*stretch=*/1);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
  QPushButton *apply = buttons->button(QDialogButtonBox::Apply);
  apply->setDefault(true);
  apply->setText(tr("Apply import"));
  connect(apply, &QPushButton::clicked, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  outer->addWidget(buttons);

  populateSummary(report);
  populateIssues(report);
}

void KartPreflightDialog::populateSummary(const KartPreflight::PreflightReport &report) {
  QString html;
  html += QStringLiteral("<table cellpadding='3'>");
  html += QStringLiteral("<tr><td><b>%1</b></td><td>%2</td></tr>")
              .arg(tr("Collection"), report.collectionName.toHtmlEscaped());
  if (!report.collectionType.trimmed().isEmpty()) {
    html += QStringLiteral("<tr><td><b>%1</b></td><td>%2</td></tr>")
                .arg(tr("Type"), report.collectionType.toHtmlEscaped());
  }
  html += QStringLiteral("<tr><td><b>%1</b></td><td>%2</td></tr>")
              .arg(tr("Items"), QString::number(report.itemCount));
  html += QStringLiteral("<tr><td><b>%1</b></td><td>%2</td></tr>")
              .arg(tr("Launchers"), QString::number(report.launcherCount));
  html += QStringLiteral("<tr><td><b>%1</b></td><td>%2</td></tr>")
              .arg(tr("Bundled metadata"), report.hasMetadata ? tr("present") : tr("not bundled"));
  html += QStringLiteral("<tr><td><b>%1</b></td><td>%2</td></tr>")
              .arg(tr("Artwork overrides"),
                   report.hasArtworkOverrides ? tr("present") : tr("not bundled"));
  html += QStringLiteral("</table>");
  m_summaryLabel->setText(html);
}

void KartPreflightDialog::populateIssues(const KartPreflight::PreflightReport &report) {
  // Kartend-kxqqf: the all-clear is reserved for a bundle that asks for
  // nothing executable. A bundle carrying any launcher field lands in
  // launcherTrust, so hasIssues() is true and this branch is not reached —
  // the dialog must never affirm a launcher configuration it only listed.
  if (!report.hasIssues()) {
    m_statusBanner->setText(tr("No validation issues — this bundle carries no launcher "
                               "configuration, every required launcher resolves and no "
                               "externally-controlled paths are flagged."));
    m_statusBanner->setStyleSheet(
        "QLabel { background-color: rgba(80, 180, 100, 0.18); border: 1px solid "
        "rgba(80, 180, 100, 0.6); border-radius: 4px; }");
    m_issuesTree->setVisible(false);
    return;
  }

  if (report.hasElevatedLauncherTrust()) {
    m_statusBanner->setText(tr("This bundle asks to run something unusual. Review the launcher "
                               "configuration below before importing — a .kart chooses both the "
                               "program and the arguments it is started with, so importing one "
                               "you did not make is like running a program you were sent."));
    m_statusBanner->setStyleSheet(
        "QLabel { background-color: rgba(210, 80, 70, 0.18); border: 1px solid "
        "rgba(210, 80, 70, 0.7); border-radius: 4px; }");
  } else if (!report.launcherIssues.isEmpty() || !report.suspiciousPaths.isEmpty() ||
             report.nameConflicts) {
    m_statusBanner->setText(tr("Validation concerns — review each section. You can still proceed "
                               "but missing launchers or suspicious paths may require attention "
                               "after import."));
    m_statusBanner->setStyleSheet(
        "QLabel { background-color: rgba(220, 160, 50, 0.18); border: 1px solid "
        "rgba(220, 160, 50, 0.6); border-radius: 4px; }");
  } else {
    m_statusBanner->setText(tr("This bundle brings its own launcher configuration — the program "
                               "it will start, and the arguments it will be started with. Check "
                               "that it is what you expect before importing."));
    m_statusBanner->setStyleSheet(
        "QLabel { background-color: rgba(70, 130, 200, 0.18); border: 1px solid "
        "rgba(70, 130, 200, 0.6); border-radius: 4px; }");
  }
  m_issuesTree->setVisible(true);

  if (!report.launcherTrust.isEmpty()) {
    auto *group = new QTreeWidgetItem(
        m_issuesTree, {tr("Launcher configuration"), QString::number(report.launcherTrust.size()),
                       tr("What this bundle will run when you press Launch")});
    group->setFirstColumnSpanned(false);
    group->setExpanded(true);
    for (const kart::KartLauncherFinding &finding : report.launcherTrust) {
      new QTreeWidgetItem(
          group, {finding.field, kart::launcherTrustReasonLabel(finding.reason), finding.value});
    }
  }

  if (!report.launcherIssues.isEmpty()) {
    auto *group = new QTreeWidgetItem(
        m_issuesTree, {tr("Launchers"), QString::number(report.launcherIssues.size()),
                       tr("Paths that do not resolve on this machine")});
    group->setFirstColumnSpanned(false);
    group->setExpanded(true);
    for (const KartPreflight::LauncherIssue &issue : report.launcherIssues) {
      auto *row = new QTreeWidgetItem(
          group, {issue.label, KartPreflight::launcherCheckLabel(issue.reason), issue.path});
      Q_UNUSED(row);
    }
  }

  if (!report.suspiciousPaths.isEmpty()) {
    auto *group = new QTreeWidgetItem(
        m_issuesTree, {tr("Suspicious paths"), QString::number(report.suspiciousPaths.size()),
                       tr("Icon and placeholder paths outside the safe allowlist")});
    group->setFirstColumnSpanned(false);
    group->setExpanded(true);
    for (const QPair<QString, QString> &p : report.suspiciousPaths) {
      auto *row = new QTreeWidgetItem(group, {p.first, tr("Outside allowlist"), p.second});
      Q_UNUSED(row);
    }
  }

  if (report.nameConflicts) {
    auto *group =
        new QTreeWidgetItem(m_issuesTree, {tr("Name conflict"), QString(),
                                           tr("A collection with this name already exists. "
                                              "Items will go through the merge-conflict flow.")});
    group->setFirstColumnSpanned(false);
    group->setExpanded(true);
  }

  m_issuesTree->resizeColumnToContents(0);
  m_issuesTree->resizeColumnToContents(1);
}
