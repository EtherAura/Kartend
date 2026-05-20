#include "appearancetoolbarpanel.h"

#include "settingsformbinding.h"
#include "settingsmodel.h"
#include "ui_appearancetoolbarpanel.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QPushButton>

AppearanceToolbarPanel::AppearanceToolbarPanel(QWidget *parent)
    : QWidget(parent), ui(new Ui::AppearanceToolbarPanel) {
  ui->setupUi(this);
  connect(ui->browseHeaderLogoButton, &QPushButton::clicked, this,
          &AppearanceToolbarPanel::onBrowse);
  connect(ui->headerLogoEdit, &QLineEdit::textChanged, this,
          [this](const QString &) { emit changed(); });
  connect(ui->headerLogoPositionComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](int) { emit changed(); });
}

AppearanceToolbarPanel::~AppearanceToolbarPanel() {
  delete ui;
}

void AppearanceToolbarPanel::setModel(SettingsModel *model) {
  m_model = model;
}

void AppearanceToolbarPanel::load() {
  if (!m_model || !m_model->workingCollections || !m_model->currentIndex ||
      *m_model->currentIndex < 0 || *m_model->currentIndex >= m_model->workingCollections->size()) {
    return;
  }
  const CollectionConfig &config = (*m_model->workingCollections)[*m_model->currentIndex];
  SettingsFormBinding::loadInto(ui->headerLogoEdit, config.background.headerLogoImage);
  SettingsFormBinding::loadIntoIndex(ui->headerLogoPositionComboBox,
                                     static_cast<int>(config.background.headerLogoPosition));
}

void AppearanceToolbarPanel::clear() {
  ui->headerLogoEdit->clear();
  ui->headerLogoPositionComboBox->setCurrentIndex(1);
}

void AppearanceToolbarPanel::save() const {
  if (!m_model || !m_model->workingCollections || !m_model->currentIndex ||
      *m_model->currentIndex < 0 || *m_model->currentIndex >= m_model->workingCollections->size()) {
    return;
  }
  CollectionConfig &config = (*m_model->workingCollections)[*m_model->currentIndex];
  config.background.headerLogoImage = ui->headerLogoEdit->text().trimmed();
  config.background.headerLogoPosition =
      static_cast<HeaderLogoPosition>(ui->headerLogoPositionComboBox->currentIndex());
}

bool AppearanceToolbarPanel::hasChanges() const {
  if (!m_model || !m_model->originalCollection) return false;
  const CollectionConfig &o = *m_model->originalCollection;
  if (ui->headerLogoEdit->text().trimmed() != o.background.headerLogoImage) return true;
  if (ui->headerLogoPositionComboBox->currentIndex() != static_cast<int>(o.background.headerLogoPosition))
    return true;
  return false;
}

void AppearanceToolbarPanel::onBrowse() {
  const QString currentPath = ui->headerLogoEdit->text().trimmed();
  const QString startDir =
      currentPath.isEmpty() ? QDir::homePath() : QFileInfo(currentPath).absolutePath();
  const QString filePath = QFileDialog::getOpenFileName(
      this, tr("Select Header Logo"), startDir,
      tr("Logos (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.svg *.apng *.mng *.mp4 *.webm *.mkv "
         "*.mov *.avi *.m4v);;Images (*.png *.jpg *.jpeg *.bmp *.svg);;Animated (*.gif *.webp "
         "*.apng *.mng);;Videos (*.mp4 *.webm *.mkv *.mov *.avi *.m4v);;All Files (*)"));
  if (!filePath.isEmpty()) {
    ui->headerLogoEdit->setText(filePath);
  }
}
