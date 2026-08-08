#include "launcherimportdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

LauncherImportDialog::LauncherImportDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle(tr("Import from Launcher"));

  auto *layout = new QVBoxLayout(this);
  auto *intro = new QLabel(
      tr("Create collections for games from these launchers. Each collection keeps itself in "
         "sync with the launcher's library and launches games through it — no shortcuts to "
         "maintain by hand."),
      this);
  intro->setWordWrap(true);
  layout->addWidget(intro);

  m_rowsLayout = new QVBoxLayout();
  layout->addLayout(m_rowsLayout);

  // Scope picker (Kartend-el5st). Steam is the only launcher that knows about
  // games it hasn't installed, so the label says so rather than implying the
  // choice moves the Flatpak/Lutris counts too.
  auto *scopeRow = new QHBoxLayout();
  scopeRow->addWidget(new QLabel(tr("Steam library:"), this));
  m_scopeCombo = new QComboBox(this);
  m_scopeCombo->addItem(tr("Installed games only"),
                        static_cast<int>(LauncherImportScopeChoice::InstalledOnly));
  m_scopeCombo->addItem(tr("Games you own (played on this computer)"),
                        static_cast<int>(LauncherImportScopeChoice::Owned));
  m_scopeCombo->addItem(tr("Every game Steam recognises"),
                        static_cast<int>(LauncherImportScopeChoice::AllRecognized));
  // Owned is the default: it is the widest tier that cannot import a game the
  // user does not have.
  m_scopeCombo->setCurrentIndex(static_cast<int>(LauncherImportScopeChoice::Owned));
  scopeRow->addWidget(m_scopeCombo, 1);
  layout->addLayout(scopeRow);

  m_scopeHint = new QLabel(this);
  m_scopeHint->setWordWrap(true);
  m_scopeHint->setEnabled(false); // secondary text, not an interactive control
  layout->addWidget(m_scopeHint);
  connect(m_scopeCombo, &QComboBox::currentIndexChanged, this, &LauncherImportDialog::relabelRows);

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
  m_rows = rows;

  for (const LauncherImportSourceRow &row : rows) {
    auto *check = new QCheckBox(this);
    check->setEnabled(row.available);
    // Programmatic re-defaults below go through QSignalBlocker, so reaching
    // this slot means the user clicked: from then on their choice sticks and
    // changing the scope only relabels the row.
    connect(check, &QCheckBox::toggled, this, [this, check]() {
      check->setProperty("userToggled", true);
      updateImportEnabled();
    });
    m_rowsLayout->addWidget(check);
    m_checks.append({row.id, check});
  }
  relabelRows();
}

auto LauncherImportDialog::countForScope(const LauncherImportSourceRow &row) const -> int {
  switch (selectedScope()) {
  case LauncherImportScopeChoice::Owned:
    return row.ownedGameCount;
  case LauncherImportScopeChoice::AllRecognized:
    return row.recognizedGameCount;
  case LauncherImportScopeChoice::InstalledOnly:
    break;
  }
  return row.gameCount;
}

void LauncherImportDialog::relabelRows() {
  for (int i = 0; i < m_rows.size() && i < m_checks.size(); ++i) {
    const LauncherImportSourceRow &row = m_rows.at(i);
    QCheckBox *check = m_checks.at(i).second;
    const int count = countForScope(row);
    QString label;
    if (!row.available) {
      label = tr("%1 — not detected").arg(row.displayName);
    } else {
      label = tr("%1 — %n game(s) found", nullptr, count).arg(row.displayName);
      if (row.alreadyImported) {
        label += tr(" (already imported — selecting re-syncs it)");
      }
    }
    check->setText(label);
    // Pre-select the sources that would actually import something new; a
    // re-sync or an empty library stays an explicit opt-in. Only re-apply the
    // default while the user has not touched the box themselves.
    if (!check->property("userToggled").toBool()) {
      const QSignalBlocker blocker(check);
      check->setChecked(row.available && count > 0 && !row.alreadyImported);
    }
  }

  switch (selectedScope()) {
  case LauncherImportScopeChoice::InstalledOnly:
    m_scopeHint->setText(tr("Only games currently installed on this computer."));
    break;
  case LauncherImportScopeChoice::Owned:
    m_scopeHint->setText(tr("Installed games, plus games you have played on this computer but "
                            "no longer have installed. Starting one asks Steam to install it. "
                            "A game you own but have never launched here will not be listed."));
    break;
  case LauncherImportScopeChoice::AllRecognized:
    m_scopeHint->setText(tr("Every game in Steam's local metadata cache. This reaches the widest, "
                            "but the cache also describes games you do not own — including the "
                            "free Valve titles every Steam install carries — so expect entries "
                            "you cannot play."));
    break;
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

auto LauncherImportDialog::selectedScope() const -> LauncherImportScopeChoice {
  return static_cast<LauncherImportScopeChoice>(m_scopeCombo->currentData().toInt());
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
