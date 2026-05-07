#include "applysettingsdialog.h"

#include <array>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

struct CategoryEntry {
  ApplySettingsDialog::FieldCategory cat;
  const char *label;
  const char *tooltip;
};

// Order here is the visual order. Tooltips list the underlying fields so a
// user hovering each row understands exactly what gets copied.
constexpr std::array kCategoryEntries = std::to_array<CategoryEntry>({
    {ApplySettingsDialog::GridLayout, QT_TRANSLATE_NOOP("ApplySettingsDialog", "Grid && Layout"),
     QT_TRANSLATE_NOOP("ApplySettingsDialog", "Grid width, horizontal/vertical spacing, item "
                                              "width and height, corner radius, alignment, view "
                                              "type (grid vs list).")},
    {ApplySettingsDialog::ItemText, QT_TRANSLATE_NOOP("ApplySettingsDialog", "Item Text"),
     QT_TRANSLATE_NOOP("ApplySettingsDialog",
                       "Grid item font size and the custom font family override.")},
    {ApplySettingsDialog::Visibility,
     QT_TRANSLATE_NOOP("ApplySettingsDialog", "Visibility && Chrome"),
     QT_TRANSLATE_NOOP("ApplySettingsDialog",
                       "Hide titles, hide subcollection titles, scrollbar visibility, sidebar "
                       "mode (overlay vs expand).")},
    {ApplySettingsDialog::Colors, QT_TRANSLATE_NOOP("ApplySettingsDialog", "Colors && Background"),
     QT_TRANSLATE_NOOP("ApplySettingsDialog",
                       "Background type (color or image), background color/image, primary UI "
                       "color, tile color, selection color.")},
    {ApplySettingsDialog::ListView, QT_TRANSLATE_NOOP("ApplySettingsDialog", "List View"),
     QT_TRANSLATE_NOOP("ApplySettingsDialog",
                       "List-mode font size, row height, primary and alternate row colors. "
                       "Only meaningful when the target is in list view.")},
});

} // namespace

ApplySettingsDialog::ApplySettingsDialog(Mode mode, const QList<CollectionConfig> &collections,
                                         int currentIndex, FieldCategories initialCategories,
                                         QWidget *parent)
    : QDialog(parent), m_mode(mode), m_collections(collections), m_currentIndex(currentIndex),
      m_initialCategories(initialCategories) {
  setupUi();
  if (m_mode == Mode::Pull) {
    populateSourceCombo();
  }
}

void ApplySettingsDialog::setupUi() {
  setWindowTitle(m_mode == Mode::Pull ? tr("Copy Settings From...") : tr("Apply Settings"));
  resize(560, 420);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(12, 12, 12, 12);
  layout->setSpacing(10);

  m_headerLabel = new QLabel(this);
  m_headerLabel->setWordWrap(true);
  layout->addWidget(m_headerLabel);

  if (m_mode == Mode::Pull) {
    auto *sourceRow = new QFormLayout;
    sourceRow->setHorizontalSpacing(8);
    sourceRow->setVerticalSpacing(6);
    sourceRow->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    m_sourceCombo = new QComboBox(this);
    sourceRow->addRow(tr("Copy from:"), m_sourceCombo);
    layout->addLayout(sourceRow);
  }

  auto *box = new QGroupBox(tr("Categories to copy"), this);
  auto *boxLayout = new QVBoxLayout(box);
  // Tighten the group's spacing so the category list doesn't dominate the
  // dialog — the assignment is also what convinces clang-analyzer that
  // boxLayout is used unconditionally; without it the analyzer treats the
  // (compile-time non-empty) loop body as potentially unreachable.
  boxLayout->setContentsMargins(10, 10, 10, 10);
  boxLayout->setSpacing(6);
  for (const auto &entry : kCategoryEntries) {
    auto *cb = new QCheckBox(tr(entry.label), box);
    cb->setToolTip(tr(entry.tooltip));
    cb->setChecked(m_initialCategories.testFlag(entry.cat));
    m_categoryBoxes.insert(entry.cat, cb);
    boxLayout->addWidget(cb);
  }
  layout->addWidget(box, 1);

  auto *btnRow = new QHBoxLayout;
  btnRow->setSpacing(8);
  auto *selectAll = new QPushButton(tr("Select All"), this);
  selectAll->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::EditSelectAll));
  auto *selectNone = new QPushButton(tr("Select None"), this);
  selectNone->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::EditClear));
  btnRow->addWidget(selectAll);
  btnRow->addWidget(selectNone);
  btnRow->addStretch(1);
  layout->addLayout(btnRow);

  auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  layout->addWidget(buttonBox);

  connect(selectAll, &QPushButton::clicked, this, &ApplySettingsDialog::onSelectAllClicked);
  connect(selectNone, &QPushButton::clicked, this, &ApplySettingsDialog::onSelectNoneClicked);
  connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void ApplySettingsDialog::populateSourceCombo() {
  if (!m_sourceCombo) {
    return;
  }
  m_sourceComboMapping.clear();
  for (int i = 0; i < m_collections.size(); ++i) {
    if (i == m_currentIndex) {
      continue;
    }
    // Exclude playlists — their settings live in the playlists table, not in
    // the per-collection appearance fields, so copying from them is a no-op
    // at best and surprising at worst.
    if (m_collections[i].isPlaylist) {
      continue;
    }
    m_sourceCombo->addItem(m_collections[i].name);
    m_sourceComboMapping.append(i);
  }
}

void ApplySettingsDialog::setHeaderText(const QString &text) {
  if (m_headerLabel) {
    m_headerLabel->setText(text);
  }
}

ApplySettingsDialog::FieldCategories ApplySettingsDialog::selectedCategories() const {
  FieldCategories result = FieldCategory::None;
  for (auto it = m_categoryBoxes.cbegin(); it != m_categoryBoxes.cend(); ++it) {
    if (it.value() && it.value()->isChecked()) {
      result |= it.key();
    }
  }
  return result;
}

int ApplySettingsDialog::selectedSourceIndex() const {
  if (m_mode != Mode::Pull || !m_sourceCombo) {
    return -1;
  }
  const int row = m_sourceCombo->currentIndex();
  if (row < 0 || row >= m_sourceComboMapping.size()) {
    return -1;
  }
  return m_sourceComboMapping[row];
}

void ApplySettingsDialog::onSelectAllClicked() {
  for (auto *cb : std::as_const(m_categoryBoxes)) {
    if (cb) {
      cb->setChecked(true);
    }
  }
}

void ApplySettingsDialog::onSelectNoneClicked() {
  for (auto *cb : std::as_const(m_categoryBoxes)) {
    if (cb) {
      cb->setChecked(false);
    }
  }
}
