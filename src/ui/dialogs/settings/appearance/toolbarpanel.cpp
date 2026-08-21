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
        ui->toolbarSearchBarVisibleCheckBox, ui->showToolbarBreadcrumbsCheckBox}) {
    connect(box, &QCheckBox::toggled, this, [this](bool) { save(); });
  }
  for (auto *edit : {ui->toolbarGridViewTextEdit, ui->toolbarListViewTextEdit,
                     ui->toolbarCoverFlowViewTextEdit, ui->toolbarHorizontalViewTextEdit,
                     ui->toolbarHideSubcollectionsTextEdit, ui->toolbarTitleFilterTextEdit}) {
    connect(edit, &QLineEdit::textChanged, this, [this](const QString &) { save(); });
  }
}

ToolbarPanel::~ToolbarPanel() {
  delete ui;
}

void ToolbarPanel::setModel(SettingsModel *model) {
  m_model = model;
  load();
}

void ToolbarPanel::load() {
  if (!m_model || !m_model->generalSettings) {
    return;
  }
  GeneralSettings *s = m_model->generalSettings;
  SettingsFormBinding::loadInto(ui->toolbarGridViewVisibleCheckBox,
                                s->toolbar.toolbarShowGridViewButton);
  SettingsFormBinding::loadInto(ui->toolbarListViewVisibleCheckBox,
                                s->toolbar.toolbarShowListViewButton);
  SettingsFormBinding::loadInto(ui->toolbarCoverFlowViewVisibleCheckBox,
                                s->toolbar.toolbarShowCoverFlowViewButton);
  SettingsFormBinding::loadInto(ui->toolbarHorizontalViewVisibleCheckBox,
                                s->toolbar.toolbarShowHorizontalViewButton);
  SettingsFormBinding::loadInto(ui->toolbarHideSubcollectionsVisibleCheckBox,
                                s->toolbar.toolbarShowHideSubcollectionsButton);
  SettingsFormBinding::loadInto(ui->toolbarTypeFilterVisibleCheckBox,
                                s->toolbar.toolbarShowTypeFilter);
  SettingsFormBinding::loadInto(ui->toolbarTitleFilterVisibleCheckBox,
                                s->toolbar.toolbarShowTitleFilter);
  SettingsFormBinding::loadInto(ui->toolbarSearchModeVisibleCheckBox,
                                s->toolbar.toolbarShowSearchModeButton);
  SettingsFormBinding::loadInto(ui->toolbarSearchBarVisibleCheckBox,
                                s->toolbar.toolbarShowSearchBar);
  // Breadcrumb visibility lives on this page rather than General — it is a
  // toolbar widget like every other row here (user request 2026-08-19).
  SettingsFormBinding::loadInto(ui->showToolbarBreadcrumbsCheckBox, s->view.showToolbarBreadcrumbs);
  SettingsFormBinding::loadInto(ui->toolbarGridViewTextEdit, s->toolbar.toolbarGridViewButtonText);
  SettingsFormBinding::loadInto(ui->toolbarListViewTextEdit, s->toolbar.toolbarListViewButtonText);
  SettingsFormBinding::loadInto(ui->toolbarCoverFlowViewTextEdit,
                                s->toolbar.toolbarCoverFlowViewButtonText);
  SettingsFormBinding::loadInto(ui->toolbarHorizontalViewTextEdit,
                                s->toolbar.toolbarHorizontalViewButtonText);
  SettingsFormBinding::loadInto(ui->toolbarHideSubcollectionsTextEdit,
                                s->toolbar.toolbarHideSubcollectionsButtonText);
  SettingsFormBinding::loadInto(ui->toolbarTitleFilterTextEdit, s->toolbar.toolbarTitleFilterText);
}

void ToolbarPanel::save() {
  if (!m_model || !m_model->generalSettings) {
    return;
  }
  GeneralSettings *s = m_model->generalSettings;
  s->view.showToolbarBreadcrumbs = ui->showToolbarBreadcrumbsCheckBox->isChecked();
  s->toolbar.toolbarShowGridViewButton = ui->toolbarGridViewVisibleCheckBox->isChecked();
  s->toolbar.toolbarShowListViewButton = ui->toolbarListViewVisibleCheckBox->isChecked();
  s->toolbar.toolbarShowCoverFlowViewButton = ui->toolbarCoverFlowViewVisibleCheckBox->isChecked();
  s->toolbar.toolbarShowHorizontalViewButton =
      ui->toolbarHorizontalViewVisibleCheckBox->isChecked();
  s->toolbar.toolbarShowHideSubcollectionsButton =
      ui->toolbarHideSubcollectionsVisibleCheckBox->isChecked();
  s->toolbar.toolbarShowTypeFilter = ui->toolbarTypeFilterVisibleCheckBox->isChecked();
  s->toolbar.toolbarShowTitleFilter = ui->toolbarTitleFilterVisibleCheckBox->isChecked();
  s->toolbar.toolbarShowSearchModeButton = ui->toolbarSearchModeVisibleCheckBox->isChecked();
  s->toolbar.toolbarShowSearchBar = ui->toolbarSearchBarVisibleCheckBox->isChecked();
  s->toolbar.toolbarGridViewButtonText = ui->toolbarGridViewTextEdit->text();
  s->toolbar.toolbarListViewButtonText = ui->toolbarListViewTextEdit->text();
  s->toolbar.toolbarCoverFlowViewButtonText = ui->toolbarCoverFlowViewTextEdit->text();
  s->toolbar.toolbarHorizontalViewButtonText = ui->toolbarHorizontalViewTextEdit->text();
  s->toolbar.toolbarHideSubcollectionsButtonText = ui->toolbarHideSubcollectionsTextEdit->text();
  s->toolbar.toolbarTitleFilterText = ui->toolbarTitleFilterTextEdit->text();
  emit changed();
}
