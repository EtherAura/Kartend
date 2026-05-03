#include "launcherchooserdialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

LauncherChooserDialog::LauncherChooserDialog(QWidget *parent, const QString &collectionName,
                                             const QStringList &launcherNames, int defaultIndex)
    : QDialog(parent) {
  setWindowTitle(tr("Choose Launcher"));
  setModal(true);

  auto *layout = new QVBoxLayout(this);

  auto *header = new QLabel(this);
  if (collectionName.isEmpty()) {
    header->setText(tr("Select a launcher:"));
  } else {
    header->setText(tr("Select a launcher for %1:").arg(collectionName));
  }
  layout->addWidget(header);

  m_list = new QListWidget(this);
  m_list->addItems(launcherNames);
  if (defaultIndex >= 0 && defaultIndex < launcherNames.size()) {
    m_list->setCurrentRow(defaultIndex);
  } else if (!launcherNames.isEmpty()) {
    m_list->setCurrentRow(0);
  }
  // Double-clicking an entry should launch with that pick rather than only
  // selecting the row.
  connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) { accept(); });
  layout->addWidget(m_list);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  buttons->button(QDialogButtonBox::Ok)->setText(tr("Launch"));
  buttons->button(QDialogButtonBox::Ok)->setDefault(true);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);

  m_list->setFocus();
  resize(360, 240);
}

int LauncherChooserDialog::chosenIndex() const {
  if (!m_list) {
    return -1;
  }
  return m_list->currentRow();
}

int LauncherChooserDialog::choose(QWidget *parent, const QString &collectionName,
                                  const QStringList &launcherNames, int defaultIndex) {
  if (launcherNames.isEmpty()) {
    return -1;
  }
  LauncherChooserDialog dialog(parent, collectionName, launcherNames, defaultIndex);
  if (dialog.exec() != QDialog::Accepted) {
    return -1;
  }
  return dialog.chosenIndex();
}
