#include "datauditlibrarypage.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QVBoxLayout>

DatAuditLibraryPage::DatAuditLibraryPage(QWidget *parent) : QWidget(parent) {
  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);

  auto *box = new QGroupBox(tr("Watched DAT library"), this);
  auto *boxLayout = new QVBoxLayout(box);
  boxLayout->addWidget(
      new QLabel(tr("Kartend checks this folder for new catalogues and proposes matches to your "
                    "collections. Attaching is always confirmed by you."),
                 box));
  m_pathLabel = new QLabel(tr("No library folder set."), box);
  m_pathLabel->setForegroundRole(QPalette::PlaceholderText);
  m_pathLabel->setWordWrap(true);
  boxLayout->addWidget(m_pathLabel);
  m_reviewButton = new QPushButton(tr("Scan & review proposals…"), box);
  m_reviewButton->setEnabled(false); // enabled once a controller hooks it up
  // Check downloaded catalogues against their source for newer revisions
  // (Kartend-m6qsb.26/.31). Hidden until a library path is set.
  m_checkUpdatesButton = new QPushButton(tr("Check for updates…"), box);
  m_checkUpdatesButton->setVisible(false);
  auto *btnRow = new QHBoxLayout();
  btnRow->addWidget(m_reviewButton);
  btnRow->addWidget(m_checkUpdatesButton);
  btnRow->addStretch();
  boxLayout->addLayout(btnRow);
  root->addWidget(box);

  // Import any cataloguer's DATs into the library (Kartend-m6qsb.22) — a
  // downloaded zip from Redump/TOSEC/No-Good, a folder of DATs, or a single
  // file. Buttons are hidden until a controller wires the import handler.
  auto *importBox = new QGroupBox(tr("Import DATs"), this);
  auto *importLayout = new QVBoxLayout(importBox);
  importLayout->addWidget(new QLabel(
      tr("Add catalogues from any source — point Kartend at a downloaded DAT zip, a "
         "folder of DATs, or a single .dat file; they're copied into the library and matched."),
      importBox));
  auto *importRow = new QHBoxLayout();
  m_importZipButton = new QPushButton(tr("Import DAT zip…"), importBox);
  m_importFolderButton = new QPushButton(tr("Import DAT folder…"), importBox);
  m_importZipButton->setVisible(false);
  m_importFolderButton->setVisible(false);
  importRow->addWidget(m_importZipButton);
  importRow->addWidget(m_importFolderButton);
  importRow->addStretch();
  importLayout->addLayout(importRow);
  root->addWidget(importBox);

  root->addStretch(1);

  connect(m_reviewButton, &QPushButton::clicked, this, &DatAuditLibraryPage::reviewRequested);
  connect(m_checkUpdatesButton, &QPushButton::clicked, this,
          &DatAuditLibraryPage::checkUpdatesRequested);
  connect(m_importZipButton, &QPushButton::clicked, this, &DatAuditLibraryPage::importZipRequested);
  connect(m_importFolderButton, &QPushButton::clicked, this,
          &DatAuditLibraryPage::importFolderRequested);
}

void DatAuditLibraryPage::setLibraryPathText(const QString &path) {
  m_pathLabel->setText(path.isEmpty() ? tr("No library folder set.") : path);
}

void DatAuditLibraryPage::setReviewEnabled(bool enabled) { m_reviewButton->setEnabled(enabled); }

void DatAuditLibraryPage::setCheckUpdatesVisible(bool visible) {
  m_checkUpdatesButton->setVisible(visible);
}

void DatAuditLibraryPage::setCheckUpdatesBusy(bool busy) {
  m_checkUpdatesButton->setEnabled(!busy);
  m_checkUpdatesButton->setText(busy ? tr("Checking…") : tr("Check for updates…"));
}

void DatAuditLibraryPage::setImportButtonsVisible(bool visible) {
  m_importZipButton->setVisible(visible);
  m_importFolderButton->setVisible(visible);
}
