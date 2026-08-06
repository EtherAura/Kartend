#include "launcherimportdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

LauncherImportDialog::LauncherImportDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle(tr("Import from Launcher"));

  auto *layout = new QVBoxLayout(this);
  auto *intro = new QLabel(
      tr("Create collections for games installed through these launchers. Each collection "
         "keeps itself in sync with the launcher's library and launches games through it — "
         "no shortcuts to maintain by hand."),
      this);
  intro->setWordWrap(true);
  layout->addWidget(intro);

  m_rowsLayout = new QVBoxLayout();
  layout->addLayout(m_rowsLayout);

  // Parent picker: without it every import landed at the hierarchy root,
  // which is unreachable-by-browsing for users whose whole library nests
  // under one shell collection (review finding on Kartend-wuq2c).
  auto *parentRow = new QHBoxLayout();
  parentRow->addWidget(new QLabel(tr("Create in:"), this));
  m_parentCombo = new QComboBox(this);
  m_parentCombo->addItem(tr("Top level"), QString());
  parentRow->addWidget(m_parentCombo, 1);
  layout->addLayout(parentRow);
  layout->addStretch();

  m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Import"));
  connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(m_buttons);

  updateImportEnabled();
}

void LauncherImportDialog::setSources(const QList<LauncherImportSourceRow> &rows) {
  while (QLayoutItem *item = m_rowsLayout->takeAt(0)) {
    delete item->widget();
    delete item;
  }
  m_checks.clear();

  for (const LauncherImportSourceRow &row : rows) {
    QString label;
    if (!row.available) {
      label = tr("%1 — not detected").arg(row.displayName);
    } else {
      label = tr("%1 — %n game(s) found", nullptr, row.gameCount).arg(row.displayName);
      if (row.alreadyImported) {
        label += tr(" (already imported — selecting re-syncs it)");
      }
    }
    auto *check = new QCheckBox(label, this);
    check->setEnabled(row.available);
    // Pre-select the sources that would actually import something new; a
    // re-sync or an empty library stays an explicit opt-in.
    check->setChecked(row.available && row.gameCount > 0 && !row.alreadyImported);
    connect(check, &QCheckBox::toggled, this, &LauncherImportDialog::updateImportEnabled);
    m_rowsLayout->addWidget(check);
    m_checks.append({row.id, check});
  }
  updateImportEnabled();
}

void LauncherImportDialog::setParentCollectionOptions(
    const QList<QPair<QString, QString>> &options) {
  while (m_parentCombo->count() > 1) {
    m_parentCombo->removeItem(1);
  }
  for (const auto &option : options) {
    m_parentCombo->addItem(option.first, option.second);
  }
}

auto LauncherImportDialog::parentCollectionUuid() const -> QString {
  return m_parentCombo->currentData().toString();
}

auto LauncherImportDialog::selectedSourceIds() const -> QStringList {
  QStringList ids;
  for (const auto &pair : m_checks) {
    if (pair.second->isChecked()) {
      ids.append(pair.first);
    }
  }
  return ids;
}

void LauncherImportDialog::updateImportEnabled() {
  m_buttons->button(QDialogButtonBox::Ok)->setEnabled(!selectedSourceIds().isEmpty());
}
