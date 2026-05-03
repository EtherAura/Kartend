#include "launchereditordialog.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

LauncherEditorDialog::LauncherEditorDialog(QWidget *parent, const LauncherConfig &initial,
                                           const QString &title)
    : QDialog(parent) {
  setWindowTitle(title);
  setModal(true);

  auto *layout = new QVBoxLayout(this);
  auto *form = new QFormLayout();

  m_nameEdit = new QLineEdit(initial.name, this);
  m_nameEdit->setPlaceholderText(tr("Display name (optional)"));
  form->addRow(tr("Name:"), m_nameEdit);

  m_launcherEdit = new QLineEdit(initial.launcherPath, this);
  m_launcherEdit->setPlaceholderText(tr("Executable path or command in PATH"));
  auto *launcherRow = new QHBoxLayout();
  launcherRow->addWidget(m_launcherEdit);
  auto *browseLauncher = new QPushButton(tr("Browse"), this);
  connect(browseLauncher, &QPushButton::clicked, this, &LauncherEditorDialog::onBrowseLauncher);
  launcherRow->addWidget(browseLauncher);
  form->addRow(tr("Launcher:"), launcherRow);

  m_coreEdit = new QLineEdit(initial.corePath, this);
  m_coreEdit->setPlaceholderText(tr("RetroArch core (.so/.dll/.dylib) — leave blank for non-RA"));
  auto *coreRow = new QHBoxLayout();
  coreRow->addWidget(m_coreEdit);
  auto *browseCore = new QPushButton(tr("Browse"), this);
  connect(browseCore, &QPushButton::clicked, this, &LauncherEditorDialog::onBrowseCore);
  coreRow->addWidget(browseCore);
  form->addRow(tr("Core Path:"), coreRow);

  m_paramsEdit = new QLineEdit(initial.launchParameters, this);
  m_paramsEdit->setPlaceholderText(tr("Optional parameters (e.g. -fullscreen)"));
  form->addRow(tr("Launch Parameters:"), m_paramsEdit);

  layout->addLayout(form);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);

  resize(480, 200);
}

LauncherConfig LauncherEditorDialog::launcher() const {
  LauncherConfig out;
  out.name = m_nameEdit ? m_nameEdit->text().trimmed() : QString();
  out.launcherPath = m_launcherEdit ? m_launcherEdit->text().trimmed() : QString();
  out.corePath = m_coreEdit ? m_coreEdit->text().trimmed() : QString();
  out.launchParameters = m_paramsEdit ? m_paramsEdit->text().trimmed() : QString();
  return out;
}

void LauncherEditorDialog::onBrowseLauncher() {
  QString fileName = QFileDialog::getOpenFileName(this, tr("Select Launcher Executable"));
  if (!fileName.isEmpty() && m_launcherEdit) {
    m_launcherEdit->setText(fileName);
  }
}

void LauncherEditorDialog::onBrowseCore() {
  QString fileName = QFileDialog::getOpenFileName(this, tr("Select Core File"));
  if (!fileName.isEmpty() && m_coreEdit) {
    m_coreEdit->setText(fileName);
  }
}
