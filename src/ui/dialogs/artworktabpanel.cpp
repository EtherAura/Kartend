#include "artworktabpanel.h"

#include "settingsformbinding.h"
#include "settingsmodel.h"
#include "ui_artworktabpanel.h"

#include <QFileDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QStringList>

ArtworkTabPanel::ArtworkTabPanel(QWidget *parent) : QWidget(parent), ui(new Ui::ArtworkTabPanel) {
  ui->setupUi(this);

  connect(ui->browseArtworkDirButton, &QPushButton::clicked, this,
          &ArtworkTabPanel::onBrowseArtworkDir);
  connect(ui->browseVideoDirButton, &QPushButton::clicked, this,
          &ArtworkTabPanel::onBrowseVideoDir);
  connect(ui->browseManualDirButton, &QPushButton::clicked, this,
          &ArtworkTabPanel::onBrowseManualDir);
  connect(ui->browsePlaceholderArtworkButton, &QPushButton::clicked, this,
          &ArtworkTabPanel::onBrowsePlaceholderArtwork);

  for (auto *edit : {ui->artworkDirLineEdit, ui->videoDirLineEdit, ui->manualDirLineEdit,
                     ui->placeholderArtworkLineEdit, ui->customArtworkTypesLineEdit}) {
    connect(edit, &QLineEdit::textChanged, this, [this](const QString &) { emit changed(); });
  }
}

ArtworkTabPanel::~ArtworkTabPanel() {
  delete ui;
}

void ArtworkTabPanel::setModel(SettingsModel *model) {
  m_model = model;
}

void ArtworkTabPanel::load() {
  if (!m_model || !m_model->workingCollections || !m_model->currentIndex ||
      *m_model->currentIndex < 0 ||
      *m_model->currentIndex >= m_model->workingCollections->size()) {
    return;
  }
  const CollectionConfig &config = (*m_model->workingCollections)[*m_model->currentIndex];
  SettingsFormBinding::loadInto(ui->artworkDirLineEdit, config.artworkDirectory);
  SettingsFormBinding::loadInto(ui->videoDirLineEdit, config.videoDirectory);
  SettingsFormBinding::loadInto(ui->manualDirLineEdit, config.manualDirectory);
  SettingsFormBinding::loadInto(ui->placeholderArtworkLineEdit, config.placeholderArtwork);
  SettingsFormBinding::loadInto(ui->customArtworkTypesLineEdit,
                                config.customArtworkTypes.join(", "));
}

void ArtworkTabPanel::clear() {
  ui->artworkDirLineEdit->clear();
  ui->videoDirLineEdit->clear();
  ui->manualDirLineEdit->clear();
  ui->placeholderArtworkLineEdit->clear();
  ui->customArtworkTypesLineEdit->clear();
}

void ArtworkTabPanel::save() const {
  if (!m_model || !m_model->workingCollections || !m_model->currentIndex ||
      *m_model->currentIndex < 0 ||
      *m_model->currentIndex >= m_model->workingCollections->size()) {
    return;
  }
  CollectionConfig &config = (*m_model->workingCollections)[*m_model->currentIndex];
  // Asset directory paths preserve the user's exact text (no trim) — matches
  // the prior extractUIFieldValues behavior for these fields.
  config.artworkDirectory = ui->artworkDirLineEdit->text();
  config.videoDirectory = ui->videoDirLineEdit->text();
  config.manualDirectory = ui->manualDirLineEdit->text();
  config.placeholderArtwork = ui->placeholderArtworkLineEdit->text();

  config.customArtworkTypes = parseCustomArtworkTypes();
}

bool ArtworkTabPanel::hasChanges() const {
  if (!m_model || !m_model->originalCollection) return false;
  const CollectionConfig &o = *m_model->originalCollection;
  if (ui->artworkDirLineEdit->text() != o.artworkDirectory) return true;
  if (ui->videoDirLineEdit->text() != o.videoDirectory) return true;
  if (ui->manualDirLineEdit->text() != o.manualDirectory) return true;
  if (ui->placeholderArtworkLineEdit->text() != o.placeholderArtwork) return true;
  // Compare against the parsed-and-deduped form so a stylistic comma-spacing
  // tweak ("a, b" vs "a,b") doesn't register as dirty.
  if (parseCustomArtworkTypes() != o.customArtworkTypes) return true;
  return false;
}

QStringList ArtworkTabPanel::parseCustomArtworkTypes() const {
  // Custom artwork types are comma-separated; parse, trim each entry, drop
  // empties + duplicates. Mirrors the legacy parser inline in
  // extractUIFieldValues.
  QStringList parsed = ui->customArtworkTypesLineEdit->text().split(',', Qt::SkipEmptyParts);
  QStringList cleaned;
  cleaned.reserve(parsed.size());
  for (QString &type : parsed) {
    type = type.trimmed();
    if (!type.isEmpty() && !cleaned.contains(type)) {
      cleaned.append(type);
    }
  }
  return cleaned;
}

void ArtworkTabPanel::onBrowseArtworkDir() {
  const QString dir = QFileDialog::getExistingDirectory(this, tr("Select Artwork Directory"));
  if (!dir.isEmpty()) {
    ui->artworkDirLineEdit->setText(dir);
  }
}
void ArtworkTabPanel::onBrowseVideoDir() {
  const QString dir = QFileDialog::getExistingDirectory(this, tr("Select Video Directory"));
  if (!dir.isEmpty()) {
    ui->videoDirLineEdit->setText(dir);
  }
}
void ArtworkTabPanel::onBrowseManualDir() {
  const QString dir = QFileDialog::getExistingDirectory(this, tr("Select Manual Directory"));
  if (!dir.isEmpty()) {
    ui->manualDirLineEdit->setText(dir);
  }
}
void ArtworkTabPanel::onBrowsePlaceholderArtwork() {
  const QString file = QFileDialog::getOpenFileName(
      this, tr("Select Placeholder Artwork"), {},
      tr("Image Files (*.png *.jpg *.jpeg *.bmp *.gif *.webp);;All Files (*)"));
  if (!file.isEmpty()) {
    ui->placeholderArtworkLineEdit->setText(file);
  }
}
