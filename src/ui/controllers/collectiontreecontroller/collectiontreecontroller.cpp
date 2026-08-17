#include "collectiontreecontroller.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "collection/collectiontreemodel.h"
#include "inavigationmanager.h"

namespace {

/// Fixed panel width, matching the proportions of the details pane's fixed
/// dock without sharing its per-collection width machinery — the tree's
/// content is names, not artwork, so one width serves.
constexpr int kPanelWidth = 240;

/// Item data roles.
constexpr int kRoleCollectionIndex = Qt::UserRole;  // int; -1 for the group header
constexpr int kRoleExpansionKey = Qt::UserRole + 1; // QString; UUID or reserved key

/// Expansion-memory key for the synthetic Playlists group row. UUIDs are
/// derived from name+mediaDirectory, so a literal that can't collide.
const QString kPlaylistsGroupKey = QStringLiteral("::playlists-group::");

} // namespace

CollectionTreeController::CollectionTreeController(QObject *parent) : QObject(parent) {}

CollectionTreeController::~CollectionTreeController() = default;

void CollectionTreeController::setupReferences(const CollectionTreeControllerSetup &setup) {
  m_ctx = setup.ctx;
  m_mainLayout = setup.mainLayout;
  m_panelParent = setup.panelParent;
  m_persistCollections = setup.persistCollections;
}

void CollectionTreeController::setupPanel() {
  if (!m_mainLayout || m_panel) {
    return;
  }
  m_panel = new QWidget(m_panelParent);
  m_panel->setObjectName(QStringLiteral("collectionTreePanel"));
  m_panel->setFixedWidth(kPanelWidth);

  auto *layout = new QVBoxLayout(m_panel);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  m_header = new QLabel(tr("Collections"), m_panel);
  m_header->setObjectName(QStringLiteral("collectionTreeHeader"));
  m_header->setMargin(8);
  layout->addWidget(m_header);

  m_tree = new QTreeWidget(m_panel);
  m_tree->setObjectName(QStringLiteral("collectionTreeWidget"));
  m_tree->setHeaderHidden(true);
  m_tree->setRootIsDecorated(true);
  m_tree->setUniformRowHeights(true);
  m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
  m_tree->setFocusPolicy(Qt::ClickFocus);
  layout->addWidget(m_tree, /*stretch=*/1);

  connect(m_tree, &QTreeWidget::itemActivated, this,
          [this](QTreeWidgetItem *item, int) { onItemActivated(item); });
  connect(m_tree, &QTreeWidget::itemClicked, this,
          [this](QTreeWidgetItem *item, int) { onItemActivated(item); });
  connect(m_tree, &QTreeWidget::itemExpanded, this,
          [this](QTreeWidgetItem *item) { onItemExpandedCollapsed(item, true); });
  connect(m_tree, &QTreeWidget::itemCollapsed, this,
          [this](QTreeWidgetItem *item) { onItemExpandedCollapsed(item, false); });

  applyPrimaryColor(QString());
  rebuildTree();
  applyStateForCollection(activeCollectionIndex());
}

void CollectionTreeController::insertPanelAt(DetailsPanePosition position) {
  if (!m_mainLayout || !m_panel) {
    return;
  }
  if (m_panelInserted && position == m_insertedPosition) {
    return;
  }
  if (m_panelInserted) {
    m_mainLayout->removeWidget(m_panel);
  }
  if (position == DetailsPanePosition::Right) {
    m_mainLayout->addWidget(m_panel);
  } else {
    m_mainLayout->insertWidget(0, m_panel);
  }
  m_insertedPosition = position;
  m_panelInserted = true;
}

void CollectionTreeController::rebuildTree() {
  if (!m_tree || !m_ctx || !m_ctx->collection.collections || !m_ctx->collection.hierarchyCache) {
    return;
  }
  const QList<CollectionConfig> &collections = *m_ctx->collection.collections;
  const CollectionHierarchyCache &hierarchy = *m_ctx->collection.hierarchyCache;

  m_suppressSignals = true;
  m_tree->clear();

  const CollectionTreeModel::Model model = CollectionTreeModel::build(collections, hierarchy);

  // Walk the pure model into items iteratively (parent item + node pairs) —
  // the model is already depth-capped, but symmetry with its guard beats a
  // recursive lambda here.
  struct Pending {
    QTreeWidgetItem *parent;
    const CollectionTreeModel::Node *node;
  };
  QList<Pending> work;
  for (const CollectionTreeModel::Node &root : model.collectionRoots) {
    work.append({nullptr, &root});
  }
  while (!work.isEmpty()) {
    const Pending current = work.takeLast();
    const int index = current.node->collectionIndex;
    if (index < 0 || index >= collections.size()) {
      continue;
    }
    auto *item = current.parent ? new QTreeWidgetItem(current.parent) : new QTreeWidgetItem(m_tree);
    const CollectionConfig &cfg = collections.at(index);
    item->setText(0, cfg.name);
    item->setData(0, kRoleCollectionIndex, index);
    const QString uuid = hierarchy.collectionUuid(index);
    item->setData(0, kRoleExpansionKey, uuid);
    item->setExpanded(m_expandedUuids.contains(uuid));
    // Reverse-append so takeLast() preserves the model's child order.
    for (auto it = current.node->children.crbegin(); it != current.node->children.crend(); ++it) {
      work.append({item, &*it});
    }
  }

  if (!model.playlistIndices.isEmpty()) {
    auto *group = new QTreeWidgetItem(m_tree);
    group->setText(0, tr("Playlists"));
    group->setData(0, kRoleCollectionIndex, -1);
    group->setData(0, kRoleExpansionKey, kPlaylistsGroupKey);
    group->setFlags(group->flags() & ~Qt::ItemIsSelectable);
    for (int index : model.playlistIndices) {
      if (index < 0 || index >= collections.size()) {
        continue;
      }
      auto *item = new QTreeWidgetItem(group);
      item->setText(0, collections.at(index).name);
      item->setData(0, kRoleCollectionIndex, index);
      item->setData(0, kRoleExpansionKey, hierarchy.collectionUuid(index));
    }
    // The group defaults open — a collapsed mystery section helps nobody —
    // but remembered collapse wins.
    group->setExpanded(!m_expandedUuids.isEmpty() ? m_expandedUuids.contains(kPlaylistsGroupKey)
                                                  : true);
    if (m_expandedUuids.isEmpty()) {
      m_expandedUuids.insert(kPlaylistsGroupKey);
    }
  }

  m_suppressSignals = false;
  highlightCollection(activeCollectionIndex());
}

void CollectionTreeController::onCollectionSwitched(int collectionIndex) {
  applyStateForCollection(collectionIndex);
  highlightCollection(collectionIndex);
}

void CollectionTreeController::applyStateForCollection(int collectionIndex) {
  if (!m_panel || !m_ctx || !m_ctx->collection.collections) {
    return;
  }
  const QList<CollectionConfig> &collections = *m_ctx->collection.collections;
  if (collectionIndex < 0 || collectionIndex >= collections.size()) {
    // Root view: the panel is the navigator, keep whatever state it had.
    return;
  }
  const CollectionTreeSettings &tree = collections.at(collectionIndex).collectionTree;
  insertPanelAt(tree.treePosition);
  const bool wasVisible = m_panel->isVisible();
  m_panel->setVisible(tree.treeVisible);
  if (wasVisible != tree.treeVisible) {
    emit visibilityChanged(tree.treeVisible);
  }
}

void CollectionTreeController::highlightCollection(int collectionIndex) {
  if (!m_tree) {
    return;
  }
  m_suppressSignals = true;
  if (collectionIndex < 0) {
    m_tree->clearSelection();
    m_tree->setCurrentItem(nullptr);
  } else {
    // First row carrying the index wins — an alias-duplicated subtree shows
    // the same collection more than once, and the primary occurrence is
    // built first.
    QTreeWidgetItemIterator it(m_tree);
    bool found = false;
    while (*it) {
      if ((*it)->data(0, kRoleCollectionIndex).toInt() == collectionIndex) {
        m_tree->setCurrentItem(*it);
        (*it)->setSelected(true);
        m_tree->scrollToItem(*it);
        found = true;
        break;
      }
      ++it;
    }
    if (!found) {
      m_tree->clearSelection();
    }
  }
  m_suppressSignals = false;
}

void CollectionTreeController::toggleVisible() {
  if (!m_panel) {
    return;
  }
  if (CollectionConfig *cfg = activeCollectionMutable()) {
    cfg->collectionTree.treeVisible = !cfg->collectionTree.treeVisible;
    m_panel->setVisible(cfg->collectionTree.treeVisible);
    emit visibilityChanged(cfg->collectionTree.treeVisible);
    if (m_persistCollections) {
      m_persistCollections();
    }
    return;
  }
  // Root view: live toggle only, nothing to remember it on.
  const bool next = !m_panel->isVisible();
  m_panel->setVisible(next);
  emit visibilityChanged(next);
}

void CollectionTreeController::setDockPosition(DetailsPanePosition position) {
  if (position != DetailsPanePosition::Left && position != DetailsPanePosition::Right) {
    return;
  }
  insertPanelAt(position);
  if (CollectionConfig *cfg = activeCollectionMutable()) {
    cfg->collectionTree.treePosition = position;
    if (m_persistCollections) {
      m_persistCollections();
    }
  }
}

DetailsPanePosition CollectionTreeController::activeDockPosition() const {
  const CollectionConfig *cfg = activeCollection();
  return cfg ? cfg->collectionTree.treePosition : m_insertedPosition;
}

void CollectionTreeController::applyPrimaryColor(const QString &hexColor) {
  if (!m_panel) {
    return;
  }
  // Palette roles keep the panel matching the system theme; the collection's
  // primary color lands on the header text and the selection, the same
  // accents the toolbar takes in applyPrimaryColorForCollection.
  const QString accent = hexColor.isEmpty() ? QStringLiteral("palette(highlight)") : hexColor;
  m_panel->setStyleSheet(QStringLiteral("QWidget#collectionTreePanel {"
                                        " background-color: palette(window); }"
                                        "QLabel#collectionTreeHeader {"
                                        " color: %1; font-weight: bold; }"
                                        "QTreeWidget#collectionTreeWidget {"
                                        " background-color: palette(base);"
                                        " color: palette(text); border: none; }"
                                        "QTreeWidget#collectionTreeWidget::item:selected {"
                                        " background-color: %1;"
                                        " color: palette(highlighted-text); }")
                             .arg(accent));
}

bool CollectionTreeController::isPanelVisible() const {
  return m_panel && m_panel->isVisible();
}

void CollectionTreeController::onItemActivated(QTreeWidgetItem *item) {
  if (m_suppressSignals || !item || !m_ctx) {
    return;
  }
  const int index = item->data(0, kRoleCollectionIndex).toInt();
  if (index < 0) {
    return; // the Playlists group header — expansion only
  }
  if (INavigationManager *nav = m_ctx->navigationManager()) {
    nav->showCollectionItems(index);
  }
}

void CollectionTreeController::onItemExpandedCollapsed(QTreeWidgetItem *item, bool expanded) {
  if (m_suppressSignals || !item) {
    return;
  }
  const QString key = item->data(0, kRoleExpansionKey).toString();
  if (key.isEmpty()) {
    return;
  }
  if (expanded) {
    m_expandedUuids.insert(key);
  } else {
    m_expandedUuids.remove(key);
  }
}

int CollectionTreeController::activeCollectionIndex() const {
  if (!m_ctx || !m_ctx->collection.currentCollectionIndex) {
    return -1;
  }
  return *m_ctx->collection.currentCollectionIndex;
}

const CollectionConfig *CollectionTreeController::activeCollection() const {
  if (!m_ctx || !m_ctx->collection.collections) {
    return nullptr;
  }
  const int index = activeCollectionIndex();
  if (index < 0 || index >= m_ctx->collection.collections->size()) {
    return nullptr;
  }
  return &m_ctx->collection.collections->at(index);
}

CollectionConfig *CollectionTreeController::activeCollectionMutable() {
  if (!m_ctx || !m_ctx->collection.collections) {
    return nullptr;
  }
  const int index = activeCollectionIndex();
  if (index < 0 || index >= m_ctx->collection.collections->size()) {
    return nullptr;
  }
  return &(*m_ctx->collection.collections)[index];
}
