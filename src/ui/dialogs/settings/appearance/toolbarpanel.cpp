#include "toolbarpanel.h"

#include "collection/generalsettings.h"
#include "settingsformbinding.h"
#include "settingsmodel.h"
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

void ToolbarPanel::setModel(SettingsModel *model) {
  m_model = model;
  refresh();
}

void ToolbarPanel::refresh() {
  if (!m_model || !m_model->generalSettings) {
    return;
  }
  GeneralSettings *s = m_model->generalSettings;
  SettingsFormBinding::loadInto(ui->toolbarGridViewVisibleCheckBox, s->toolbarShowGridViewButton);
  SettingsFormBinding::loadInto(ui->toolbarListViewVisibleCheckBox, s->toolbarShowListViewButton);
  SettingsFormBinding::loadInto(ui->toolbarCoverFlowViewVisibleCheckBox,
                                s->toolbarShowCoverFlowViewButton);
  SettingsFormBinding::loadInto(ui->toolbarHorizontalViewVisibleCheckBox,
                                s->toolbarShowHorizontalViewButton);
  SettingsFormBinding::loadInto(ui->toolbarHideSubcollectionsVisibleCheckBox,
                                s->toolbarShowHideSubcollectionsButton);
  SettingsFormBinding::loadInto(ui->toolbarTypeFilterVisibleCheckBox, s->toolbarShowTypeFilter);
  SettingsFormBinding::loadInto(ui->toolbarTitleFilterVisibleCheckBox, s->toolbarShowTitleFilter);
  SettingsFormBinding::loadInto(ui->toolbarSearchModeVisibleCheckBox,
                                s->toolbarShowSearchModeButton);
  SettingsFormBinding::loadInto(ui->toolbarSearchBarVisibleCheckBox, s->toolbarShowSearchBar);
  SettingsFormBinding::loadInto(ui->toolbarGridViewTextEdit, s->toolbarGridViewButtonText);
  SettingsFormBinding::loadInto(ui->toolbarListViewTextEdit, s->toolbarListViewButtonText);
  SettingsFormBinding::loadInto(ui->toolbarCoverFlowViewTextEdit,
                                s->toolbarCoverFlowViewButtonText);
  SettingsFormBinding::loadInto(ui->toolbarHorizontalViewTextEdit,
                                s->toolbarHorizontalViewButtonText);
  SettingsFormBinding::loadInto(ui->toolbarHideSubcollectionsTextEdit,
                                s->toolbarHideSubcollectionsButtonText);
  SettingsFormBinding::loadInto(ui->toolbarTitleFilterTextEdit, s->toolbarTitleFilterText);
}

void ToolbarPanel::writeBack() {
  if (!m_model || !m_model->generalSettings) {
    return;
  }
  GeneralSettings *s = m_model->generalSettings;
  s->toolbarShowGridViewButton = ui->toolbarGridViewVisibleCheckBox->isChecked();
  s->toolbarShowListViewButton = ui->toolbarListViewVisibleCheckBox->isChecked();
  s->toolbarShowCoverFlowViewButton = ui->toolbarCoverFlowViewVisibleCheckBox->isChecked();
  s->toolbarShowHorizontalViewButton = ui->toolbarHorizontalViewVisibleCheckBox->isChecked();
  s->toolbarShowHideSubcollectionsButton =
      ui->toolbarHideSubcollectionsVisibleCheckBox->isChecked();
  s->toolbarShowTypeFilter = ui->toolbarTypeFilterVisibleCheckBox->isChecked();
  s->toolbarShowTitleFilter = ui->toolbarTitleFilterVisibleCheckBox->isChecked();
  s->toolbarShowSearchModeButton = ui->toolbarSearchModeVisibleCheckBox->isChecked();
  s->toolbarShowSearchBar = ui->toolbarSearchBarVisibleCheckBox->isChecked();
  s->toolbarGridViewButtonText = ui->toolbarGridViewTextEdit->text();
  s->toolbarListViewButtonText = ui->toolbarListViewTextEdit->text();
  s->toolbarCoverFlowViewButtonText = ui->toolbarCoverFlowViewTextEdit->text();
  s->toolbarHorizontalViewButtonText = ui->toolbarHorizontalViewTextEdit->text();
  s->toolbarHideSubcollectionsButtonText = ui->toolbarHideSubcollectionsTextEdit->text();
  s->toolbarTitleFilterText = ui->toolbarTitleFilterTextEdit->text();
  emit changed();
}
