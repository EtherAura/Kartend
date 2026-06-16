#include "generalsettingspanel.h"

#include "collection/generalsettings.h"
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
  connect(ui->browseQuarantineDefaultButton, &QPushButton::clicked, this,
          &GeneralSettingsPanel::onBrowseQuarantineDefault);

  connectChangeSignals();
}

GeneralSettingsPanel::~GeneralSettingsPanel() {
  delete ui;
}

void GeneralSettingsPanel::setModel(SettingsModel *model) {
  m_model = model;
  load();
}

void GeneralSettingsPanel::load() {
  if (!m_model || !m_model->generalSettings) {
    return;
  }
  GeneralSettings *s = m_model->generalSettings;
  // Startup
  SettingsFormBinding::loadInto(ui->useHomeViewCheckBox, s->startup.useHomeView);
  SettingsFormBinding::loadInto(ui->homeViewLabelLineEdit, s->startup.homeViewLabel);
  SettingsFormBinding::loadInto(ui->homeViewIconLineEdit, s->startup.homeViewIcon);
  SettingsFormBinding::loadInto(ui->startupVideoEnabledCheckBox, s->startup.startupVideoEnabled);
  SettingsFormBinding::loadInto(ui->startupVideoPathLineEdit, s->startup.startupVideoPath);
  SettingsFormBinding::loadInto(ui->retroarchConfigLineEdit, s->launchers.retroarchConfigPath);
  SettingsFormBinding::loadInto(ui->quarantineDefaultDirLineEdit,
                                s->scraper.options.quarantineDefaultDir);

  // Selection & Display
  SettingsFormBinding::loadInto(ui->rememberSelectionCheckBox, s->input.rememberSelection);
  SettingsFormBinding::loadInto(ui->wrapNavigationCheckBox, s->input.wrapNavigation);
  SettingsFormBinding::loadInto(ui->selectItemOnHoverCheckBox, s->input.selectItemOnHover);
  SettingsFormBinding::loadInto(ui->showTitleInPlaceholderCheckBox, s->view.showTitleInPlaceholder);

  // Input & Scroll Timing
  SettingsFormBinding::loadInto(ui->mouseWheelSpeedSpinBox, s->input.mouseWheelRows);
  SettingsFormBinding::loadInto(ui->scrollVelocityMultiplierSpinBox,
                                s->input.scrollVelocityMultiplier);
  SettingsFormBinding::loadInto(ui->scrollAnimationSpeedSpinBox,
                                s->input.scrollAnimationDurationMs);
  SettingsFormBinding::loadInto(ui->clickHoldDelaySpinBox, s->input.clickHoldDelayMs);
  SettingsFormBinding::loadInto(ui->clickHoldRepeatIntervalSpinBox,
                                s->input.clickHoldRepeatIntervalMs);
  SettingsFormBinding::loadInto(ui->listClickHoldRepeatSpinBox,
                                s->input.listClickHoldRepeatIntervalMs);
  SettingsFormBinding::loadInto(ui->keyboardSpeedSpinBox, s->input.keyboardRepeatIntervalMs);
  SettingsFormBinding::loadInto(ui->keyboardRepeatDelaySpinBox, s->input.keyboardRepeatDelayMs);
  SettingsFormBinding::loadInto(ui->listKeyboardRepeatSpinBox,
                                s->input.listKeyboardRepeatIntervalMs);

  // Performance & History
  SettingsFormBinding::loadInto(ui->pixmapCacheSpinBox, s->media.pixmapCacheSizeMB);
  SettingsFormBinding::loadInto(ui->runtimeDetectionCheckBox,
                                s->runtimeDetection.runtimeDetectionEnabled);
  SettingsFormBinding::loadInto(ui->historyEnabledCheckBox, s->history.historyEnabled);
  SettingsFormBinding::loadInto(ui->historyMaxEntriesSpinBox, s->history.historyMaxEntries);
  SettingsFormBinding::loadInto(ui->videoThumbnailTimeoutSpinBox,
                                s->media.videoThumbnailExtractionTimeoutMs);
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

void GeneralSettingsPanel::onBrowseQuarantineDefault() {
  const QString dir = QFileDialog::getExistingDirectory(
      this, tr("Select default quarantine folder"), QDir::homePath());
  if (!dir.isEmpty()) {
    ui->quarantineDefaultDirLineEdit->setText(dir);
  }
}

void GeneralSettingsPanel::save() {
  if (!m_model || !m_model->generalSettings) {
    return;
  }
  GeneralSettings *s = m_model->generalSettings;
  // Startup
  s->startup.startupCollection = ui->startupCollectionComboBox->currentData().toString();
  s->startup.useHomeView = ui->useHomeViewCheckBox->isChecked();
  s->startup.homeViewLabel = ui->homeViewLabelLineEdit->text();
  s->startup.homeViewIcon = ui->homeViewIconLineEdit->text();
  s->startup.startupVideoEnabled = ui->startupVideoEnabledCheckBox->isChecked();
  s->startup.startupVideoPath = ui->startupVideoPathLineEdit->text();
  s->launchers.retroarchConfigPath = ui->retroarchConfigLineEdit->text();
  s->scraper.options.quarantineDefaultDir = ui->quarantineDefaultDirLineEdit->text();

  // Selection & Display
  s->input.rememberSelection = ui->rememberSelectionCheckBox->isChecked();
  s->input.wrapNavigation = ui->wrapNavigationCheckBox->isChecked();
  s->input.selectItemOnHover = ui->selectItemOnHoverCheckBox->isChecked();
  s->view.showTitleInPlaceholder = ui->showTitleInPlaceholderCheckBox->isChecked();

  // Input & Scroll Timing
  s->input.mouseWheelRows = ui->mouseWheelSpeedSpinBox->value();
  s->input.scrollVelocityMultiplier = ui->scrollVelocityMultiplierSpinBox->value();
  s->input.scrollAnimationDurationMs = ui->scrollAnimationSpeedSpinBox->value();
  s->input.clickHoldDelayMs = ui->clickHoldDelaySpinBox->value();
  s->input.clickHoldRepeatIntervalMs = ui->clickHoldRepeatIntervalSpinBox->value();
  s->input.listClickHoldRepeatIntervalMs = ui->listClickHoldRepeatSpinBox->value();
  s->input.keyboardRepeatIntervalMs = ui->keyboardSpeedSpinBox->value();
  s->input.keyboardRepeatDelayMs = ui->keyboardRepeatDelaySpinBox->value();
  s->input.listKeyboardRepeatIntervalMs = ui->listKeyboardRepeatSpinBox->value();

  // Performance & History
  s->media.pixmapCacheSizeMB = ui->pixmapCacheSpinBox->value();
  s->runtimeDetection.runtimeDetectionEnabled = ui->runtimeDetectionCheckBox->isChecked();
  s->history.historyEnabled = ui->historyEnabledCheckBox->isChecked();
  s->history.historyMaxEntries = ui->historyMaxEntriesSpinBox->value();
  s->media.videoThumbnailExtractionTimeoutMs = ui->videoThumbnailTimeoutSpinBox->value();

  emit changed();
}

void GeneralSettingsPanel::connectChangeSignals() {
  const auto onCombo = QOverload<int>::of(&QComboBox::currentIndexChanged);
  const auto onSpin = QOverload<int>::of(&QSpinBox::valueChanged);
  const auto onDoubleSpin = QOverload<double>::of(&QDoubleSpinBox::valueChanged);

  // Startup
  connect(ui->startupCollectionComboBox, onCombo, this, [this](int) { save(); });
  connect(ui->useHomeViewCheckBox, &QCheckBox::toggled, this, [this](bool) { save(); });
  connect(ui->homeViewLabelLineEdit, &QLineEdit::textChanged, this,
          [this](const QString &) { save(); });
  connect(ui->homeViewIconLineEdit, &QLineEdit::textChanged, this,
          [this](const QString &) { save(); });
  connect(ui->startupVideoEnabledCheckBox, &QCheckBox::toggled, this, [this](bool) { save(); });
  connect(ui->startupVideoPathLineEdit, &QLineEdit::textChanged, this,
          [this](const QString &) { save(); });
  connect(ui->retroarchConfigLineEdit, &QLineEdit::textChanged, this,
          [this](const QString &) { save(); });
  connect(ui->quarantineDefaultDirLineEdit, &QLineEdit::textChanged, this,
          [this](const QString &) { save(); });

  // Selection & Display
  for (auto *box : {ui->rememberSelectionCheckBox, ui->wrapNavigationCheckBox,
                    ui->selectItemOnHoverCheckBox, ui->showTitleInPlaceholderCheckBox}) {
    connect(box, &QCheckBox::toggled, this, [this](bool) { save(); });
  }

  // Input & Scroll Timing — every QSpinBox plus the lone QDoubleSpinBox.
  for (auto *spin :
       {ui->mouseWheelSpeedSpinBox, ui->scrollAnimationSpeedSpinBox, ui->clickHoldDelaySpinBox,
        ui->clickHoldRepeatIntervalSpinBox, ui->listClickHoldRepeatSpinBox,
        ui->keyboardSpeedSpinBox, ui->keyboardRepeatDelaySpinBox, ui->listKeyboardRepeatSpinBox}) {
    connect(spin, onSpin, this, [this](int) { save(); });
  }
  connect(ui->scrollVelocityMultiplierSpinBox, onDoubleSpin, this, [this](double) { save(); });

  // Performance & History
  connect(ui->pixmapCacheSpinBox, onSpin, this, [this](int) { save(); });
  connect(ui->runtimeDetectionCheckBox, &QCheckBox::toggled, this, [this](bool) { save(); });
  connect(ui->historyEnabledCheckBox, &QCheckBox::toggled, this, [this](bool) { save(); });
  connect(ui->historyMaxEntriesSpinBox, onSpin, this, [this](int) { save(); });
  connect(ui->videoThumbnailTimeoutSpinBox, onSpin, this, [this](int) { save(); });
}
