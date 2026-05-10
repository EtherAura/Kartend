#include "generalsettingspanel.h"

#include "collectionutils.h"
#include "settingsformbinding.h"
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

  connectChangeSignals();
}

GeneralSettingsPanel::~GeneralSettingsPanel() {
  delete ui;
}

void GeneralSettingsPanel::setSettings(GeneralSettings *settings) {
  m_settings = settings;
  refresh();
}

void GeneralSettingsPanel::refresh() {
  if (!m_settings) {
    return;
  }
  // Startup
  SettingsFormBinding::loadInto(ui->useHomeViewCheckBox, m_settings->useHomeView);
  SettingsFormBinding::loadInto(ui->homeViewLabelLineEdit, m_settings->homeViewLabel);
  SettingsFormBinding::loadInto(ui->homeViewIconLineEdit, m_settings->homeViewIcon);
  SettingsFormBinding::loadInto(ui->startupVideoEnabledCheckBox, m_settings->startupVideoEnabled);
  SettingsFormBinding::loadInto(ui->startupVideoPathLineEdit, m_settings->startupVideoPath);

  // Selection & Display
  SettingsFormBinding::loadInto(ui->rememberSelectionCheckBox, m_settings->rememberSelection);
  SettingsFormBinding::loadInto(ui->wrapNavigationCheckBox, m_settings->wrapNavigation);
  SettingsFormBinding::loadInto(ui->selectItemOnHoverCheckBox, m_settings->selectItemOnHover);
  SettingsFormBinding::loadInto(ui->showTitleInPlaceholderCheckBox,
                                m_settings->showTitleInPlaceholder);

  // Input & Scroll Timing
  SettingsFormBinding::loadInto(ui->mouseWheelSpeedSpinBox, m_settings->mouseWheelRows);
  SettingsFormBinding::loadInto(ui->scrollVelocityMultiplierSpinBox,
                                m_settings->scrollVelocityMultiplier);
  SettingsFormBinding::loadInto(ui->scrollAnimationSpeedSpinBox,
                                m_settings->scrollAnimationDurationMs);
  SettingsFormBinding::loadInto(ui->clickHoldDelaySpinBox, m_settings->clickHoldDelayMs);
  SettingsFormBinding::loadInto(ui->clickHoldRepeatIntervalSpinBox,
                                m_settings->clickHoldRepeatIntervalMs);
  SettingsFormBinding::loadInto(ui->listClickHoldRepeatSpinBox,
                                m_settings->listClickHoldRepeatIntervalMs);
  SettingsFormBinding::loadInto(ui->keyboardSpeedSpinBox, m_settings->keyboardRepeatIntervalMs);
  SettingsFormBinding::loadInto(ui->keyboardRepeatDelaySpinBox, m_settings->keyboardRepeatDelayMs);
  SettingsFormBinding::loadInto(ui->listKeyboardRepeatSpinBox,
                                m_settings->listKeyboardRepeatIntervalMs);

  // Performance & History
  SettingsFormBinding::loadInto(ui->pixmapCacheSpinBox, m_settings->pixmapCacheSizeMB);
  SettingsFormBinding::loadInto(ui->runtimeDetectionCheckBox, m_settings->runtimeDetectionEnabled);
  SettingsFormBinding::loadInto(ui->historyEnabledCheckBox, m_settings->historyEnabled);
  SettingsFormBinding::loadInto(ui->historyMaxEntriesSpinBox, m_settings->historyMaxEntries);
  SettingsFormBinding::loadInto(ui->videoThumbnailTimeoutSpinBox,
                                m_settings->videoThumbnailExtractionTimeoutMs);
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

void GeneralSettingsPanel::writeBack() {
  if (!m_settings) {
    return;
  }
  // Startup
  m_settings->startupCollection = ui->startupCollectionComboBox->currentData().toString();
  m_settings->useHomeView = ui->useHomeViewCheckBox->isChecked();
  m_settings->homeViewLabel = ui->homeViewLabelLineEdit->text();
  m_settings->homeViewIcon = ui->homeViewIconLineEdit->text();
  m_settings->startupVideoEnabled = ui->startupVideoEnabledCheckBox->isChecked();
  m_settings->startupVideoPath = ui->startupVideoPathLineEdit->text();

  // Selection & Display
  m_settings->rememberSelection = ui->rememberSelectionCheckBox->isChecked();
  m_settings->wrapNavigation = ui->wrapNavigationCheckBox->isChecked();
  m_settings->selectItemOnHover = ui->selectItemOnHoverCheckBox->isChecked();
  m_settings->showTitleInPlaceholder = ui->showTitleInPlaceholderCheckBox->isChecked();

  // Input & Scroll Timing
  m_settings->mouseWheelRows = ui->mouseWheelSpeedSpinBox->value();
  m_settings->scrollVelocityMultiplier = ui->scrollVelocityMultiplierSpinBox->value();
  m_settings->scrollAnimationDurationMs = ui->scrollAnimationSpeedSpinBox->value();
  m_settings->clickHoldDelayMs = ui->clickHoldDelaySpinBox->value();
  m_settings->clickHoldRepeatIntervalMs = ui->clickHoldRepeatIntervalSpinBox->value();
  m_settings->listClickHoldRepeatIntervalMs = ui->listClickHoldRepeatSpinBox->value();
  m_settings->keyboardRepeatIntervalMs = ui->keyboardSpeedSpinBox->value();
  m_settings->keyboardRepeatDelayMs = ui->keyboardRepeatDelaySpinBox->value();
  m_settings->listKeyboardRepeatIntervalMs = ui->listKeyboardRepeatSpinBox->value();

  // Performance & History
  m_settings->pixmapCacheSizeMB = ui->pixmapCacheSpinBox->value();
  m_settings->runtimeDetectionEnabled = ui->runtimeDetectionCheckBox->isChecked();
  m_settings->historyEnabled = ui->historyEnabledCheckBox->isChecked();
  m_settings->historyMaxEntries = ui->historyMaxEntriesSpinBox->value();
  m_settings->videoThumbnailExtractionTimeoutMs = ui->videoThumbnailTimeoutSpinBox->value();

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
