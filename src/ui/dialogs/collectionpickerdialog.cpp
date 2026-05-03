#include "collectionpickerdialog.h"

#include <algorithm>
#include <functional>

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

CollectionPickerDialog::CollectionPickerDialog(const QList<CollectionConfig> &collections,
                                               int excludeIndex, const QList<int> &initialChecked,
                                               QWidget *parent)
    : QDialog(parent), m_collections(collections), m_excludeIndex(excludeIndex) {
  setupUi();
  populateTree(initialChecked);
}

void CollectionPickerDialog::setupUi() {
  setWindowTitle(tr("Select Target Collections"));
  resize(420, 480);

  auto *layout = new QVBoxLayout(this);

  m_tree = new QTreeWidget(this);
  m_tree->setHeaderLabel(tr("Collections"));
  m_tree->setSelectionMode(QAbstractItemView::NoSelection);
  m_tree->setAnimated(true);
  layout->addWidget(m_tree, 1);

  auto *buttonRow = new QHBoxLayout;
  auto *selectAllBtn = new QPushButton(tr("Select All"), this);
  auto *selectNoneBtn = new QPushButton(tr("Select None"), this);
  buttonRow->addWidget(selectAllBtn);
  buttonRow->addWidget(selectNoneBtn);
  buttonRow->addStretch(1);
  layout->addLayout(buttonRow);

  auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  layout->addWidget(buttonBox);

  connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(selectAllBtn, &QPushButton::clicked, this, &CollectionPickerDialog::onSelectAllClicked);
  connect(selectNoneBtn, &QPushButton::clicked, this, &CollectionPickerDialog::onSelectNoneClicked);
  connect(m_tree, &QTreeWidget::itemChanged, this, &CollectionPickerDialog::onItemChanged);
}

int CollectionPickerDialog::displayedParent(int idx, int excluded,
                                            const QList<CollectionConfig> &cols) {
  if (idx < 0 || idx >= cols.size()) {
    return -1;
  }
  int p = cols[idx].parentCollectionIndex;
  // Bound the walk by the collection count so a malformed parent chain can't
  // spin forever.
  for (int steps = 0; steps < cols.size() && p == excluded && p >= 0 && p < cols.size(); ++steps) {
    p = cols[p].parentCollectionIndex;
  }
  return p;
}

QTreeWidgetItem *CollectionPickerDialog::createItem(int collectionIndex,
                                                    QTreeWidgetItem *parentItem) {
  auto *item = parentItem ? new QTreeWidgetItem(parentItem) : new QTreeWidgetItem(m_tree);
  item->setText(0, m_collections[collectionIndex].name);
  item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
  item->setCheckState(0, Qt::Unchecked);
  m_indexToItem.insert(collectionIndex, item);
  m_itemToIndex.insert(item, collectionIndex);
  return item;
}

void CollectionPickerDialog::populateTree(const QList<int> &initialChecked) {
  m_blockItemChanged = true;

  for (int i = 0; i < m_collections.size(); ++i) {
    if (i == m_excludeIndex) {
      continue;
    }
    if (displayedParent(i, m_excludeIndex, m_collections) == -1) {
      createItem(i, nullptr);
    }
  }

  // Bounded fixed-point insertion so each iteration only adds children whose
  // parents are already in the tree.
  for (int pass = 0; pass < m_collections.size(); ++pass) {
    bool inserted = false;
    for (int i = 0; i < m_collections.size(); ++i) {
      if (i == m_excludeIndex || m_indexToItem.contains(i)) {
        continue;
      }
      int parentIdx = displayedParent(i, m_excludeIndex, m_collections);
      if (parentIdx >= 0 && m_indexToItem.contains(parentIdx)) {
        createItem(i, m_indexToItem.value(parentIdx));
        inserted = true;
      }
    }
    if (!inserted) {
      break;
    }
  }

  // Surface any orphans (broken parent links) at the root so the user can
  // still target them.
  for (int i = 0; i < m_collections.size(); ++i) {
    if (i == m_excludeIndex || m_indexToItem.contains(i)) {
      continue;
    }
    createItem(i, nullptr);
  }

  for (int idx : initialChecked) {
    if (auto *item = m_indexToItem.value(idx)) {
      item->setCheckState(0, Qt::Checked);
    }
  }

  m_tree->expandAll();
  m_blockItemChanged = false;
}

void CollectionPickerDialog::onItemChanged(QTreeWidgetItem *item, int column) {
  if (m_blockItemChanged || column != 0 || !item) {
    return;
  }
  // Auto-tristate handles ancestor recomputation; we only own the descendant
  // direction. A partial state arises from disagreeing children, so leave it
  // alone — propagating it would erase the user's intent.
  Qt::CheckState newState = item->checkState(0);
  if (newState == Qt::PartiallyChecked) {
    return;
  }
  m_blockItemChanged = true;
  std::function<void(QTreeWidgetItem *)> propagate = [&](QTreeWidgetItem *parent) {
    for (int i = 0; i < parent->childCount(); ++i) {
      auto *child = parent->child(i);
      if (child->checkState(0) != newState) {
        child->setCheckState(0, newState);
      }
      propagate(child);
    }
  };
  propagate(item);
  m_blockItemChanged = false;
}

void CollectionPickerDialog::onSelectAllClicked() {
  m_blockItemChanged = true;
  for (auto it = m_itemToIndex.begin(); it != m_itemToIndex.end(); ++it) {
    it.key()->setCheckState(0, Qt::Checked);
  }
  m_blockItemChanged = false;
}

void CollectionPickerDialog::onSelectNoneClicked() {
  m_blockItemChanged = true;
  for (auto it = m_itemToIndex.begin(); it != m_itemToIndex.end(); ++it) {
    it.key()->setCheckState(0, Qt::Unchecked);
  }
  m_blockItemChanged = false;
}

QList<int> CollectionPickerDialog::selectedIndices() const {
  QList<int> result;
  for (auto it = m_itemToIndex.constBegin(); it != m_itemToIndex.constEnd(); ++it) {
    if (it.key()->checkState(0) == Qt::Checked) {
      result.append(it.value());
    }
  }
  std::sort(result.begin(), result.end());
  return result;
}
