#include "generalsettingspanel.h"

#include "collectionutils.h"
#include "settingsformbinding.h"
#include "settingsmodel.h"
#include "ui_generalsettingspanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>

GeneralSettingsPanel::GeneralSettingsPanel(QWidget *parent)
    : QWidget(parent), ui(new Ui::GeneralSettingsPanel) {
  ui->setupUi(this);

  connect(ui->browseHomeViewIconButton, &QPushButton::clicked, this,
          &GeneralSettingsPanel::onBrowseHomeViewIcon);
  connect(ui->browseStartupVideoButton, &QPushButton::clicked, this,
          &GeneralSettingsPanel::onBrowseStartupVideo);
  connect(ui->browseRetroarchConfigButton, &QPushButton::clicked, this,
          &GeneralSettingsPanel::onBrowseRetroarchConfig);

  connectChangeSignals();
}

GeneralSettingsPanel::~GeneralSettingsPanel() {
  delete ui;
}

void GeneralSettingsPanel::setModel(SettingsModel *model) {
  m_model = model;
  refresh();
}

void GeneralSettingsPanel::refresh() {
  if (!m_model || !m_model->generalSettings) {
    return;
  }
  GeneralSettings *s = m_model->generalSettings;
  // Startup
  SettingsFormBinding::loadInto(ui->useHomeViewCheckBox, s->useHomeView);
  SettingsFormBinding::loadInto(ui->homeViewLabelLineEdit, s->homeViewLabel);
  SettingsFormBinding::loadInto(ui->homeViewIconLineEdit, s->homeViewIcon);
  SettingsFormBinding::loadInto(ui->startupVideoEnabledCheckBox, s->startupVideoEnabled);
  SettingsFormBinding::loadInto(ui->startupVideoPathLineEdit, s->startupVideoPath);
  SettingsFormBinding::loadInto(ui->retroarchConfigLineEdit, s->retroarchConfigPath);

  // Selection & Display
  SettingsFormBinding::loadInto(ui->rememberSelectionCheckBox, s->rememberSelection);
  SettingsFormBinding::loadInto(ui->wrapNavigationCheckBox, s->wrapNavigation);
  SettingsFormBinding::loadInto(ui->selectItemOnHoverCheckBox, s->selectItemOnHover);
  SettingsFormBinding::loadInto(ui->showTitleInPlaceholderCheckBox, s->showTitleInPlaceholder);

  // Input & Scroll Timing
  SettingsFormBinding::loadInto(ui->mouseWheelSpeedSpinBox, s->mouseWheelRows);
  SettingsFormBinding::loadInto(ui->scrollVelocityMultiplierSpinBox, s->scrollVelocityMultiplier);
  SettingsFormBinding::loadInto(ui->scrollAnimationSpeedSpinBox, s->scrollAnimationDurationMs);
  SettingsFormBinding::loadInto(ui->clickHoldDelaySpinBox, s->clickHoldDelayMs);
  SettingsFormBinding::loadInto(ui->clickHoldRepeatIntervalSpinBox, s->clickHoldRepeatIntervalMs);
  SettingsFormBinding::loadInto(ui->listClickHoldRepeatSpinBox, s->listClickHoldRepeatIntervalMs);
  SettingsFormBinding::loadInto(ui->keyboardSpeedSpinBox, s->keyboardRepeatIntervalMs);
  SettingsFormBinding::loadInto(ui->keyboardRepeatDelaySpinBox, s->keyboardRepeatDelayMs);
  SettingsFormBinding::loadInto(ui->listKeyboardRepeatSpinBox, s->listKeyboardRepeatIntervalMs);

  // Performance & History
  SettingsFormBinding::loadInto(ui->pixmapCacheSpinBox, s->pixmapCacheSizeMB);
  SettingsFormBinding::loadInto(ui->runtimeDetectionCheckBox, s->runtimeDetectionEnabled);
  SettingsFormBinding::loadInto(ui->historyEnabledCheckBox, s->historyEnabled);
  SettingsFormBinding::loadInto(ui->historyMaxEntriesSpinBox, s->historyMaxEntries);
  SettingsFormBinding::loadInto(ui->videoThumbnailTimeoutSpinBox,
                                s->videoThumbnailExtractionTimeoutMs);
}

void GeneralSettingsPanel::setStartupCollections(const QStringList &names,
                                                 const QString &currentValue) {
  QSignalBlocker blocker(ui->startupCollectionComboBox);
  ui->startupCollectionComboBox->clear();
  ui->startupCollectionComboBox->addItem(tr("(Default)"), QString());
  for (const QString &name : names) {
    ui->startupCollectionComboBox->addItem(name, name);
  }
  const int idx = ui->startupCollectionComboBox->findData(currentValue);
  ui->startupCollectionComboBox->setCurrentIndex(idx >= 0 ? idx : 0);
}

void GeneralSettingsPanel::onBrowseHomeViewIcon() {
  const QString fileName = QFileDialog::getOpenFileName(
      this, tr("Select Home View Icon"), QDir::homePath(),
      tr("Images (*.png *.svg *.jpg *.jpeg *.bmp *.webp);;All Files (*)"));
  if (!fileName.isEmpty()) {
    ui->homeViewIconLineEdit->setText(fileName);
  }
}

void GeneralSettingsPanel::onBrowseStartupVideo() {
  const QString fileName = QFileDialog::getOpenFileName(
      this, tr("Select Startup Video"), QDir::homePath(),
      tr("Videos (*.mp4 *.webm *.mkv *.mov *.avi *.m4v);;All Files (*)"));
  if (!fileName.isEmpty()) {
    ui->startupVideoPathLineEdit->setText(fileName);
  }
}

void GeneralSettingsPanel::onBrowseRetroarchConfig() {
  // The override accepts either a retroarch.cfg file or a core
  // directory; offer a file picker filtered to .cfg, with All Files
  // so a user can still aim at any config the install uses.
  const QString fileName =
      QFileDialog::getOpenFileName(this, tr("Select retroarch.cfg"), QDir::homePath(),
                                   tr("RetroArch config (*.cfg);;All Files (*)"));
  if (!fileName.isEmpty()) {
    ui->retroarchConfigLineEdit->setText(fileName);
  }
}

void GeneralSettingsPanel::writeBack() {
  if (!m_model || !m_model->generalSettings) {
    return;
  }
  GeneralSettings *s = m_model->generalSettings;
  // Startup
  s->startupCollection = ui->startupCollectionComboBox->currentData().toString();
  s->useHomeView = ui->useHomeViewCheckBox->isChecked();
  s->homeViewLabel = ui->homeViewLabelLineEdit->text();
  s->homeViewIcon = ui->homeViewIconLineEdit->text();
  s->startupVideoEnabled = ui->startupVideoEnabledCheckBox->isChecked();
  s->startupVideoPath = ui->startupVideoPathLineEdit->text();
  s->retroarchConfigPath = ui->retroarchConfigLineEdit->text();

  // Selection & Display
  s->rememberSelection = ui->rememberSelectionCheckBox->isChecked();
  s->wrapNavigation = ui->wrapNavigationCheckBox->isChecked();
  s->selectItemOnHover = ui->selectItemOnHoverCheckBox->isChecked();
  s->showTitleInPlaceholder = ui->showTitleInPlaceholderCheckBox->isChecked();

  // Input & Scroll Timing
  s->mouseWheelRows = ui->mouseWheelSpeedSpinBox->value();
  s->scrollVelocityMultiplier = ui->scrollVelocityMultiplierSpinBox->value();
  s->scrollAnimationDurationMs = ui->scrollAnimationSpeedSpinBox->value();
  s->clickHoldDelayMs = ui->clickHoldDelaySpinBox->value();
  s->clickHoldRepeatIntervalMs = ui->clickHoldRepeatIntervalSpinBox->value();
  s->listClickHoldRepeatIntervalMs = ui->listClickHoldRepeatSpinBox->value();
  s->keyboardRepeatIntervalMs = ui->keyboardSpeedSpinBox->value();
  s->keyboardRepeatDelayMs = ui->keyboardRepeatDelaySpinBox->value();
  s->listKeyboardRepeatIntervalMs = ui->listKeyboardRepeatSpinBox->value();

  // Performance & History
  s->pixmapCacheSizeMB = ui->pixmapCacheSpinBox->value();
  s->runtimeDetectionEnabled = ui->runtimeDetectionCheckBox->isChecked();
  s->historyEnabled = ui->historyEnabledCheckBox->isChecked();
  s->historyMaxEntries = ui->historyMaxEntriesSpinBox->value();
  s->videoThumbnailExtractionTimeoutMs = ui->videoThumbnailTimeoutSpinBox->value();

  emit changed();
}

void GeneralSettingsPanel::connectChangeSignals() {
  const auto onCombo = QOverload<int>::of(&QComboBox::currentIndexChanged);
  const auto onSpin = QOverload<int>::of(&QSpinBox::valueChanged);
  const auto onDoubleSpin = QOverload<double>::of(&QDoubleSpinBox::valueChanged);

  // Startup
  connect(ui->startupCollectionComboBox, onCombo, this, [this](int) { writeBack(); });
  connect(ui->useHomeViewCheckBox, &QCheckBox::toggled, this, [this](bool) { writeBack(); });
  connect(ui->homeViewLabelLineEdit, &QLineEdit::textChanged, this,
          [this](const QString &) { writeBack(); });
  connect(ui->homeViewIconLineEdit, &QLineEdit::textChanged, this,
          [this](const QString &) { writeBack(); });
  connect(ui->startupVideoEnabledCheckBox, &QCheckBox::toggled, this,
          [this](bool) { writeBack(); });
  connect(ui->startupVideoPathLineEdit, &QLineEdit::textChanged, this,
          [this](const QString &) { writeBack(); });
  connect(ui->retroarchConfigLineEdit, &QLineEdit::textChanged, this,
          [this](const QString &) { writeBack(); });

  // Selection & Display
  for (auto *box : {ui->rememberSelectionCheckBox, ui->wrapNavigationCheckBox,
                    ui->selectItemOnHoverCheckBox, ui->showTitleInPlaceholderCheckBox}) {
    connect(box, &QCheckBox::toggled, this, [this](bool) { writeBack(); });
  }

  // Input & Scroll Timing — every QSpinBox plus the lone QDoubleSpinBox.
  for (auto *spin :
       {ui->mouseWheelSpeedSpinBox, ui->scrollAnimationSpeedSpinBox, ui->clickHoldDelaySpinBox,
        ui->clickHoldRepeatIntervalSpinBox, ui->listClickHoldRepeatSpinBox,
        ui->keyboardSpeedSpinBox, ui->keyboardRepeatDelaySpinBox, ui->listKeyboardRepeatSpinBox}) {
    connect(spin, onSpin, this, [this](int) { writeBack(); });
  }
  connect(ui->scrollVelocityMultiplierSpinBox, onDoubleSpin, this, [this](double) { writeBack(); });

  // Performance & History
  connect(ui->pixmapCacheSpinBox, onSpin, this, [this](int) { writeBack(); });
  connect(ui->runtimeDetectionCheckBox, &QCheckBox::toggled, this, [this](bool) { writeBack(); });
  connect(ui->historyEnabledCheckBox, &QCheckBox::toggled, this, [this](bool) { writeBack(); });
  connect(ui->historyMaxEntriesSpinBox, onSpin, this, [this](int) { writeBack(); });
  connect(ui->videoThumbnailTimeoutSpinBox, onSpin, this, [this](int) { writeBack(); });
}
