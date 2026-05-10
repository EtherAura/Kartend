#include "collectionstreeshell.h"

#include "collectiontreewidget.h"
#include "ui_collectionstreeshell.h"

#include <QComboBox>
#include <QPushButton>

CollectionsTreeShell::CollectionsTreeShell(QWidget *parent)
    : QWidget(parent), ui(new Ui::CollectionsTreeShell) {
  ui->setupUi(this);
}

CollectionsTreeShell::~CollectionsTreeShell() {
  delete ui;
}

CollectionTreeWidget *CollectionsTreeShell::collectionTreeWidget() const {
  return ui->collectionTreeWidget;
}

QPushButton *CollectionsTreeShell::addCollectionButton() const {
  return ui->addCollectionButton;
}

QPushButton *CollectionsTreeShell::removeCollectionButton() const {
  return ui->removeCollectionButton;
}

QPushButton *CollectionsTreeShell::duplicateCollectionButton() const {
  return ui->duplicateCollectionButton;
}

QPushButton *CollectionsTreeShell::saveCollectionButton() const {
  return ui->saveCollectionButton;
}

QComboBox *CollectionsTreeShell::settingsScopeComboBox() const {
  return ui->settingsScopeComboBox;
}
