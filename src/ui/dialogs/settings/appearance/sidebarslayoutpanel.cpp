#include "sidebarslayoutpanel.h"
#include "ui_sidebarslayoutpanel.h"

#include "collection/collectionconfig.h"
#include "settingsmodel.h"

namespace {

/// The pane-side combo lists positions in enum order (Right=0, Left=1,
/// Top=2, Bottom=3), so index<->enum casts stay honest — the same contract
/// the Details Pane page's own position combo uses.
constexpr int kTreeSideLeftIndex = 0;
constexpr int kTreeSideRightIndex = 1;

} // namespace

SidebarsLayoutPanel::SidebarsLayoutPanel(QWidget *parent)
    : QWidget(parent), ui(new Ui::SidebarsLayoutPanel) {
  ui->setupUi(this);
  for (QComboBox *combo :
       {ui->paneSideComboBox, ui->paneJustificationComboBox, ui->paneScrollbarModeComboBox,
        ui->treeSideComboBox, ui->treeJustificationComboBox, ui->treeScrollbarModeComboBox,
        ui->treeModeComboBox}) {
    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { emit changed(); });
  }
  connect(ui->treeIconDisplayComboBox, &QComboBox::currentIndexChanged, this,
          [this](int) { emit changed(); });
  connect(ui->treeShowLinesCheckBox, &QCheckBox::toggled, this, [this](bool) { emit changed(); });
  connect(ui->treeColorizeSelectedCheckBox, &QCheckBox::toggled, this,
          [this](bool) { emit changed(); });
  connect(ui->treeIconSizeSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
          [this](int) { emit changed(); });
  connect(ui->treeWidthSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
          [this](int) { emit changed(); });
  connect(ui->treeIconStyleComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](int) { emit changed(); });
  connect(ui->treeIconTintEdit, &QLineEdit::textEdited, this,
          [this](const QString &) { emit changed(); });
}

SidebarsLayoutPanel::~SidebarsLayoutPanel() {
  delete ui;
}

void SidebarsLayoutPanel::setModel(SettingsModel *model) {
  m_model = model;
}

void SidebarsLayoutPanel::load() {
  const CollectionConfig *current = m_model ? m_model->currentWorkingCollection() : nullptr;
  if (!current) return;
  ui->paneSideComboBox->setCurrentIndex(static_cast<int>(current->sidebar.sidebarPosition));
  m_loadedPaneSideIndex = ui->paneSideComboBox->currentIndex();
  ui->paneJustificationComboBox->setCurrentIndex(
      static_cast<int>(current->sidebar.sidebarJustification));
  ui->paneScrollbarModeComboBox->setCurrentIndex(
      static_cast<int>(current->sidebar.sidebarScrollbarMode));
  ui->treeScrollbarModeComboBox->setCurrentIndex(
      static_cast<int>(current->collectionTree.treeScrollbarMode));
  ui->treeSideComboBox->setCurrentIndex(
      current->collectionTree.treePosition == DetailsPanePosition::Right ? kTreeSideRightIndex
                                                                         : kTreeSideLeftIndex);
  ui->treeJustificationComboBox->setCurrentIndex(
      static_cast<int>(current->collectionTree.treeJustification));
  ui->treeModeComboBox->setCurrentIndex(static_cast<int>(current->collectionTree.treeMode));
  ui->treeIconDisplayComboBox->setCurrentIndex(
      static_cast<int>(current->collectionTree.treeIconDisplay));
  ui->treeShowLinesCheckBox->setChecked(current->collectionTree.treeShowLines);
  ui->treeColorizeSelectedCheckBox->setChecked(current->collectionTree.treeColorizeSelected);
  ui->treeIconSizeSpinBox->setValue(current->collectionTree.treeIconSize);
  ui->treeWidthSpinBox->setValue(current->collectionTree.treeWidth);
  ui->treeIconStyleComboBox->setCurrentIndex(
      static_cast<int>(current->collectionTree.treeIconStyle));
  ui->treeIconTintEdit->setText(current->collectionTree.treeIconTintColor);
}

void SidebarsLayoutPanel::clear() {
  ui->paneSideComboBox->setCurrentIndex(static_cast<int>(DetailsPanePosition::Right));
  m_loadedPaneSideIndex = ui->paneSideComboBox->currentIndex();
  ui->paneJustificationComboBox->setCurrentIndex(
      static_cast<int>(SidebarJustification::BelowToolbar));
  ui->paneScrollbarModeComboBox->setCurrentIndex(
      static_cast<int>(SidebarAppearance{}.sidebarScrollbarMode));
  ui->treeScrollbarModeComboBox->setCurrentIndex(
      static_cast<int>(CollectionTreeSettings{}.treeScrollbarMode));
  ui->treeSideComboBox->setCurrentIndex(kTreeSideLeftIndex);
  // Struct defaults — the tree's default justification is FULL-HEIGHT since
  // the 2026-08-17 defaults decision, unlike the pane's above.
  ui->treeJustificationComboBox->setCurrentIndex(
      static_cast<int>(CollectionTreeSettings{}.treeJustification));
  ui->treeModeComboBox->setCurrentIndex(static_cast<int>(CollectionTreeSettings{}.treeMode));
  ui->treeIconDisplayComboBox->setCurrentIndex(
      static_cast<int>(CollectionTreeSettings{}.treeIconDisplay));
  ui->treeShowLinesCheckBox->setChecked(CollectionTreeSettings{}.treeShowLines);
  ui->treeColorizeSelectedCheckBox->setChecked(CollectionTreeSettings{}.treeColorizeSelected);
  ui->treeIconSizeSpinBox->setValue(CollectionTreeSettings{}.treeIconSize);
  ui->treeWidthSpinBox->setValue(CollectionTreeSettings{}.treeWidth);
  ui->treeIconStyleComboBox->setCurrentIndex(
      static_cast<int>(CollectionTreeSettings{}.treeIconStyle));
  ui->treeIconTintEdit->clear();
}

void SidebarsLayoutPanel::save() {
  CollectionConfig *current = m_model ? m_model->currentWorkingCollection() : nullptr;
  if (!current) return;
  // Aliased with the Details Pane page's Position: write only when edited
  // HERE, so an untouched copy never overwrites the other page's edit.
  if (ui->paneSideComboBox->currentIndex() != m_loadedPaneSideIndex) {
    current->sidebar.sidebarPosition =
        static_cast<DetailsPanePosition>(ui->paneSideComboBox->currentIndex());
  }
  current->sidebar.sidebarJustification =
      static_cast<SidebarJustification>(ui->paneJustificationComboBox->currentIndex());
  current->sidebar.sidebarScrollbarMode =
      static_cast<ScrollbarMode>(ui->paneScrollbarModeComboBox->currentIndex());
  current->collectionTree.treeScrollbarMode =
      static_cast<ScrollbarMode>(ui->treeScrollbarModeComboBox->currentIndex());
  current->collectionTree.treePosition = ui->treeSideComboBox->currentIndex() == kTreeSideRightIndex
                                             ? DetailsPanePosition::Right
                                             : DetailsPanePosition::Left;
  current->collectionTree.treeJustification =
      static_cast<SidebarJustification>(ui->treeJustificationComboBox->currentIndex());
  // Combo order matches DetailsPaneMode (Overlay = 0, Expand = 1).
  current->collectionTree.treeMode =
      static_cast<DetailsPaneMode>(ui->treeModeComboBox->currentIndex());
  current->collectionTree.treeIconDisplay =
      static_cast<TreeIconDisplay>(ui->treeIconDisplayComboBox->currentIndex());
  current->collectionTree.treeShowLines = ui->treeShowLinesCheckBox->isChecked();
  current->collectionTree.treeColorizeSelected = ui->treeColorizeSelectedCheckBox->isChecked();
  current->collectionTree.treeIconSize = ui->treeIconSizeSpinBox->value();
  current->collectionTree.treeWidth = ui->treeWidthSpinBox->value();
  current->collectionTree.treeIconStyle =
      static_cast<TreeIconStyle>(ui->treeIconStyleComboBox->currentIndex());
  current->collectionTree.treeIconTintColor = ui->treeIconTintEdit->text().trimmed();
}
