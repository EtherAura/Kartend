#include "toolbarpanel.h"

#include "collectionutils.h"
#include "settingsformbinding.h"
#include "ui_toolbarpanel.h"

#include <QCheckBox>
#include <QLineEdit>

ToolbarPanel::ToolbarPanel(QWidget *parent) : QWidget(parent), ui(new Ui::ToolbarPanel) {
  ui->setupUi(this);

  for (auto *box :
       {ui->toolbarGridViewVisibleCheckBox, ui->toolbarListViewVisibleCheckBox,
        ui->toolbarCoverFlowViewVisibleCheckBox, ui->toolbarHorizontalViewVisibleCheckBox,
        ui->toolbarHideSubcollectionsVisibleCheckBox, ui->toolbarTypeFilterVisibleCheckBox,
        ui->toolbarTitleFilterVisibleCheckBox, ui->toolbarSearchModeVisibleCheckBox,
        ui->toolbarSearchBarVisibleCheckBox}) {
    connect(box, &QCheckBox::toggled, this, [this](bool) { writeBack(); });
  }
  for (auto *edit : {ui->toolbarGridViewTextEdit, ui->toolbarListViewTextEdit,
                     ui->toolbarCoverFlowViewTextEdit, ui->toolbarHorizontalViewTextEdit,
                     ui->toolbarHideSubcollectionsTextEdit, ui->toolbarTitleFilterTextEdit}) {
    connect(edit, &QLineEdit::textChanged, this, [this](const QString &) { writeBack(); });
  }
}

ToolbarPanel::~ToolbarPanel() {
  delete ui;
}

void ToolbarPanel::setSettings(GeneralSettings *settings) {
  m_settings = settings;
  refresh();
}

void ToolbarPanel::refresh() {
  if (!m_settings) {
    return;
  }
  SettingsFormBinding::loadInto(ui->toolbarGridViewVisibleCheckBox,
                                m_settings->toolbarShowGridViewButton);
  SettingsFormBinding::loadInto(ui->toolbarListViewVisibleCheckBox,
                                m_settings->toolbarShowListViewButton);
  SettingsFormBinding::loadInto(ui->toolbarCoverFlowViewVisibleCheckBox,
                                m_settings->toolbarShowCoverFlowViewButton);
  SettingsFormBinding::loadInto(ui->toolbarHorizontalViewVisibleCheckBox,
                                m_settings->toolbarShowHorizontalViewButton);
  SettingsFormBinding::loadInto(ui->toolbarHideSubcollectionsVisibleCheckBox,
                                m_settings->toolbarShowHideSubcollectionsButton);
  SettingsFormBinding::loadInto(ui->toolbarTypeFilterVisibleCheckBox,
                                m_settings->toolbarShowTypeFilter);
  SettingsFormBinding::loadInto(ui->toolbarTitleFilterVisibleCheckBox,
                                m_settings->toolbarShowTitleFilter);
  SettingsFormBinding::loadInto(ui->toolbarSearchModeVisibleCheckBox,
                                m_settings->toolbarShowSearchModeButton);
  SettingsFormBinding::loadInto(ui->toolbarSearchBarVisibleCheckBox,
                                m_settings->toolbarShowSearchBar);
  SettingsFormBinding::loadInto(ui->toolbarGridViewTextEdit, m_settings->toolbarGridViewButtonText);
  SettingsFormBinding::loadInto(ui->toolbarListViewTextEdit, m_settings->toolbarListViewButtonText);
  SettingsFormBinding::loadInto(ui->toolbarCoverFlowViewTextEdit,
                                m_settings->toolbarCoverFlowViewButtonText);
  SettingsFormBinding::loadInto(ui->toolbarHorizontalViewTextEdit,
                                m_settings->toolbarHorizontalViewButtonText);
  SettingsFormBinding::loadInto(ui->toolbarHideSubcollectionsTextEdit,
                                m_settings->toolbarHideSubcollectionsButtonText);
  SettingsFormBinding::loadInto(ui->toolbarTitleFilterTextEdit, m_settings->toolbarTitleFilterText);
}

void ToolbarPanel::writeBack() {
  if (!m_settings) {
    return;
  }
  m_settings->toolbarShowGridViewButton = ui->toolbarGridViewVisibleCheckBox->isChecked();
  m_settings->toolbarShowListViewButton = ui->toolbarListViewVisibleCheckBox->isChecked();
  m_settings->toolbarShowCoverFlowViewButton = ui->toolbarCoverFlowViewVisibleCheckBox->isChecked();
  m_settings->toolbarShowHorizontalViewButton =
      ui->toolbarHorizontalViewVisibleCheckBox->isChecked();
  m_settings->toolbarShowHideSubcollectionsButton =
      ui->toolbarHideSubcollectionsVisibleCheckBox->isChecked();
  m_settings->toolbarShowTypeFilter = ui->toolbarTypeFilterVisibleCheckBox->isChecked();
  m_settings->toolbarShowTitleFilter = ui->toolbarTitleFilterVisibleCheckBox->isChecked();
  m_settings->toolbarShowSearchModeButton = ui->toolbarSearchModeVisibleCheckBox->isChecked();
  m_settings->toolbarShowSearchBar = ui->toolbarSearchBarVisibleCheckBox->isChecked();
  m_settings->toolbarGridViewButtonText = ui->toolbarGridViewTextEdit->text();
  m_settings->toolbarListViewButtonText = ui->toolbarListViewTextEdit->text();
  m_settings->toolbarCoverFlowViewButtonText = ui->toolbarCoverFlowViewTextEdit->text();
  m_settings->toolbarHorizontalViewButtonText = ui->toolbarHorizontalViewTextEdit->text();
  m_settings->toolbarHideSubcollectionsButtonText = ui->toolbarHideSubcollectionsTextEdit->text();
  m_settings->toolbarTitleFilterText = ui->toolbarTitleFilterTextEdit->text();
  emit changed();
}
