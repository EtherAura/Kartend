#include "datauditprofilepanel.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVariant>

#include "datauditprofiledialog.h"
#include "errorutils.h"

DatAuditProfilePanel::DatAuditProfilePanel(DatAuditProfileStore &profileStore, QWidget *parent)
    : QWidget(parent), m_controller(profileStore) {
  buildUi();
  connect(m_profileCombo, &QComboBox::currentIndexChanged, this,
          &DatAuditProfilePanel::onProfileSelected);
  connect(m_newProfileButton, &QPushButton::clicked, this, &DatAuditProfilePanel::onNewProfile);
  connect(m_editProfileButton, &QPushButton::clicked, this, &DatAuditProfilePanel::onEditProfile);
  connect(m_duplicateProfileButton, &QPushButton::clicked, this,
          &DatAuditProfilePanel::onDuplicateProfile);
  connect(m_renameProfileButton, &QPushButton::clicked, this,
          &DatAuditProfilePanel::onRenameProfile);
  connect(m_deleteProfileButton, &QPushButton::clicked, this,
          &DatAuditProfilePanel::onDeleteProfile);
  reloadProfiles();
}

void DatAuditProfilePanel::buildUi() {
  auto *row = new QHBoxLayout(this);
  row->setContentsMargins(0, 0, 0, 0);
  row->addWidget(new QLabel(tr("Profile:"), this));
  m_profileCombo = new QComboBox(this);
  m_newProfileButton = new QPushButton(tr("New…"), this);
  m_editProfileButton = new QPushButton(tr("Edit…"), this);
  m_duplicateProfileButton = new QPushButton(tr("Duplicate"), this);
  m_renameProfileButton = new QPushButton(tr("Rename"), this);
  m_deleteProfileButton = new QPushButton(tr("Delete"), this);
  row->addWidget(m_profileCombo, 1);
  row->addWidget(m_newProfileButton);
  row->addWidget(m_editProfileButton);
  row->addWidget(m_duplicateProfileButton);
  row->addWidget(m_renameProfileButton);
  row->addWidget(m_deleteProfileButton);
}

void DatAuditProfilePanel::setCollections(QList<CollectionConfig> *collections) {
  m_collections = collections;
}

void DatAuditProfilePanel::setWorkingProfileProvider(
    std::function<DatAuditProfile::Profile()> provider) {
  m_workingProfile = std::move(provider);
}

void DatAuditProfilePanel::setBaseProfileProvider(
    std::function<DatAuditProfile::Profile()> provider) {
  m_baseProfile = std::move(provider);
}

void DatAuditProfilePanel::reloadProfiles() {
  const QSignalBlocker block(m_profileCombo);
  m_profileCombo->clear();
  m_profileCombo->addItem(tr("(unsaved)"), QVariant(qlonglong(-1)));
  if (auto all = m_controller.list(); all.isOk()) {
    for (const DatAuditProfile::Profile &p : all.value()) {
      m_profileCombo->addItem(p.name, QVariant(qlonglong(p.id)));
    }
  }
}

void DatAuditProfilePanel::selectProfileById(qint64 id) {
  for (int i = 0; i < m_profileCombo->count(); ++i) {
    if (m_profileCombo->itemData(i).toLongLong() == id) {
      m_profileCombo->setCurrentIndex(i);
      return;
    }
  }
}

void DatAuditProfilePanel::selectUnsavedSilently() {
  const QSignalBlocker block(m_profileCombo);
  selectProfileById(-1); // the "(unsaved)" row
}

void DatAuditProfilePanel::onProfileSelected(int index) {
  if (index < 0) {
    return;
  }
  const qint64 id = m_profileCombo->itemData(index).toLongLong();
  if (id < 0) {
    emit unsavedSelected(); // "(unsaved)" — the page keeps its ad-hoc lists
    return;
  }
  auto res = m_controller.load(id);
  if (res.isError()) {
    ErrorUtils::logError(res.error());
    return;
  }
  // Bind the success-payload optional once so the has_value() guard and the
  // dereference act on the same object (a second res.value() call reads as an
  // unchecked access to clang-tidy's flow analysis).
  const auto &profileOpt = res.value();
  if (profileOpt.has_value()) {
    emit profileChanged(*profileOpt);
  }
}

bool DatAuditProfilePanel::persistProfile(DatAuditProfile::Profile &p) {
  // The insert-vs-update decision + DatRef metadata refresh live in the
  // controller (headlessly tested); the panel keeps only the user-facing error
  // report. Capture the insert-vs-update intent before persist() assigns p.id.
  const bool inserting = p.id < 0;
  auto res = m_controller.persist(p);
  if (res.isError()) {
    QMessageBox::warning(
        this, tr("DAT Audit"),
        (inserting ? tr("Could not save profile: %1") : tr("Could not update profile: %1"))
            .arg(res.error().message));
    return false;
  }
  return true;
}

void DatAuditProfilePanel::onNewProfile() {
  DatAuditProfileDialog dlg(DatAuditProfile::Profile{}, m_collections, this);
  if (dlg.exec() != QDialog::Accepted) {
    return;
  }
  DatAuditProfile::Profile p = dlg.profile();
  if (!persistProfile(p)) {
    return;
  }
  // Announce first so the page adopts + derives immediately; the select below
  // then reloads the persisted copy from the DB through the normal selection
  // flow (the same double-load the pre-extraction inline flow performed).
  emit profileChanged(p);
  reloadProfiles();
  selectProfileById(p.id);
}

void DatAuditProfilePanel::onEditProfile() {
  if (!m_workingProfile) {
    return;
  }
  DatAuditProfileDialog dlg(m_workingProfile(), m_collections, this);
  if (dlg.exec() != QDialog::Accepted) {
    return;
  }
  const DatAuditProfile::Profile edited = dlg.profile();
  // The page adopts the edit first (collection derivation + list sync run
  // there), so the copy persisted below carries the derived scan roots / DATs
  // exactly as the pre-extraction flow did.
  emit profileChanged(edited);
  if (edited.id < 0) {
    return; // unsaved working profile — nothing to persist
  }
  DatAuditProfile::Profile persisted = m_baseProfile ? m_baseProfile() : edited;
  persistProfile(persisted); // failure already reported; the reload reverts the UI
  reloadProfiles();
  selectProfileById(edited.id);
}

void DatAuditProfilePanel::onDuplicateProfile() {
  DatAuditProfile::Profile seed =
      m_workingProfile ? m_workingProfile() : DatAuditProfile::Profile{};
  seed.id = -1;
  seed.name = seed.name.isEmpty() ? tr("New profile") : tr("%1 (copy)").arg(seed.name);
  DatAuditProfileDialog dlg(seed, m_collections, this);
  if (dlg.exec() != QDialog::Accepted) {
    return;
  }
  DatAuditProfile::Profile p = dlg.profile();
  if (!persistProfile(p)) {
    return;
  }
  emit profileChanged(p);
  reloadProfiles();
  selectProfileById(p.id);
}

void DatAuditProfilePanel::onRenameProfile() {
  DatAuditProfile::Profile base = m_baseProfile ? m_baseProfile() : DatAuditProfile::Profile{};
  if (base.id < 0) {
    QMessageBox::information(this, tr("DAT Audit"), tr("Select a saved profile to rename."));
    return;
  }
  bool ok = false;
  const QString name = QInputDialog::getText(this, tr("Rename profile"), tr("New name:"),
                                             QLineEdit::Normal, base.name, &ok);
  if (!ok || name.trimmed().isEmpty()) {
    return;
  }
  base.name = name.trimmed();
  persistProfile(base);
  reloadProfiles();
  selectProfileById(base.id); // → the selection flow reloads the renamed profile
}

void DatAuditProfilePanel::onDeleteProfile() {
  const qint64 id = m_profileCombo->itemData(m_profileCombo->currentIndex()).toLongLong();
  if (id < 0) {
    return;
  }
  if (QMessageBox::question(this, tr("Delete profile"),
                            tr("Delete profile \"%1\"?").arg(m_profileCombo->currentText())) !=
      QMessageBox::Yes) {
    return;
  }
  auto res = m_controller.remove(id);
  Q_UNUSED(res);
  emit profileDeleted();
  reloadProfiles();
}
