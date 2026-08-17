#include "collectiontreecontroller.h"

#include <algorithm>

#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QStyle>
#include <QStyleOption>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "collection/collectiontreemodel.h"
#include "collection/typehelpers.h"
#include "inavigationmanager.h"
#include "isessionmanager.h"
#include "pathutils.h"

namespace {

/// Fixed panel width, matching the proportions of the details pane's fixed
/// dock without sharing its per-collection width machinery — the tree's
/// content is names, not artwork, so one width serves.
constexpr int kPanelWidth = 240;

/// Item data roles.
constexpr int kRoleCollectionIndex = Qt::UserRole;  // int; -1 for the group header
constexpr int kRoleExpansionKey = Qt::UserRole + 1; // QString; UUID or reserved key
constexpr int kRoleParentCollection = Qt::UserRole + 2; // int; -1 for roots/playlists
constexpr int kRoleName = Qt::UserRole + 3; // QString; cfg.name (text may be blank in icons-only)
constexpr int kRoleIsCategory = Qt::UserRole + 4; // bool; row has children (incl. group header)

/// Expansion-memory key for the synthetic Playlists group row. UUIDs are
/// derived from name+mediaDirectory, so a literal that can't collide.
const QString kPlaylistsGroupKey = QStringLiteral("::playlists-group::");

/// QTreeWidget whose branch column can hide the connector lines while
/// keeping the expand chevrons (user request 2026-08-17: tree lines off by
/// default, optional). Styles draw lines from State_Sibling/State_Item in
/// PE_IndicatorBranch; painting the primitive ourselves with ONLY the
/// children/open states yields just the arrow. Toggled via the
/// "kartendShowLines" dynamic property so the controller's member type can
/// stay QTreeWidget*.
class TreeBranchView : public QTreeWidget {
public:
  using QTreeWidget::QTreeWidget;

protected:
  void drawBranches(QPainter *painter, const QRect &rect,
                    const QModelIndex &index) const override {
    if (property("kartendShowLines").toBool()) {
      QTreeWidget::drawBranches(painter, rect, index);
      return;
    }
    if (!model() || !model()->hasChildren(index)) {
      return;
    }
    QStyleOption opt;
    opt.initFrom(this);
    const int unit = indentation();
    opt.rect = QRect(rect.right() - unit + 1, rect.top(), unit, rect.height());
    opt.state |= QStyle::State_Children;
    if (isExpanded(index)) {
      opt.state |= QStyle::State_Open;
    }
    style()->drawPrimitive(QStyle::PE_IndicatorBranch, &opt, painter, this);
  }
};

/// Crop fully-transparent borders (field report 2026-08-17, round 6):
/// ScreenScraper's company/logo canvases pad the actual mark with large
/// transparent margins (a 600x300 canvas can carry a 150px-wide mark), so
/// scaling the CANVAS to the configured height rendered some logos tiny and
/// visually mis-aligned while true full-bleed logos towered next to them.
/// Trimming to the opaque bounding box first makes every logo's VISIBLE art
/// scale to the same height.
QPixmap trimTransparentBorders(const QPixmap &pm) {
  if (pm.isNull()) return pm;
  const QImage img = pm.toImage().convertToFormat(QImage::Format_ARGB32);
  int top = img.height(), bottom = -1, left = img.width(), right = -1;
  for (int y = 0; y < img.height(); ++y) {
    const auto *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
    for (int x = 0; x < img.width(); ++x) {
      if (qAlpha(line[x]) > 16) {
        top = qMin(top, y);
        bottom = qMax(bottom, y);
        left = qMin(left, x);
        right = qMax(right, x);
      }
    }
  }
  if (bottom < 0) return pm; // fully transparent — leave as-is
  const QRect box(left, top, right - left + 1, bottom - top + 1);
  if (box == img.rect()) return pm;
  return QPixmap::fromImage(img.copy(box));
}

/// Readability halo (field report 2026-08-17, round 6: "some icons are
/// still difficult to read"): a logo whose opaque pixels average close to
/// the panel background's luminance — navy wordmarks and black box art on a
/// dark theme — gets a faint 1px outline in the opposing shade, drawn from
/// its own alpha silhouette. Logos with healthy contrast pass through
/// untouched, so bright marks keep their clean edges.
QPixmap ensureContrastAgainst(const QPixmap &pm, const QColor &background, qreal dpr) {
  if (pm.isNull()) return pm;
  const QImage img = pm.toImage().convertToFormat(QImage::Format_ARGB32);
  // Fraction-based contrast check (round 7): an AVERAGE hides mixed logos —
  // a navy wordmark on a gold diamond averages "bright" while the text is
  // invisible on a dark theme. Count the opaque pixels sitting within the
  // low-contrast band of the background instead; when a TENTH of the mark
  // would blend in, it earns the halo (measured: the Sony mark's unreadable
  // wordmark is 14% of its pixels; the fully-readable Nintendo pill is 5%).
  // Over-application is cheap — the halo is a subtle 1px outline.
  const int bgLum = qGray(background.rgb());
  qint64 lowContrast = 0;
  qint64 opaque = 0;
  for (int y = 0; y < img.height(); ++y) {
    const auto *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
    for (int x = 0; x < img.width(); ++x) {
      if (qAlpha(line[x]) > 128) {
        ++opaque;
        if (qAbs(qGray(line[x]) - bgLum) < 56) ++lowContrast;
      }
    }
  }
  if (opaque == 0) return pm;
  if (lowContrast * 100 < opaque * 10) return pm; // readable as-is
  const QColor halo = bgLum < 128 ? QColor(255, 255, 255, 210) : QColor(0, 0, 0, 210);
  QPixmap silhouette(pm.width(), pm.height());
  silhouette.fill(Qt::transparent);
  {
    QPainter painter(&silhouette);
    painter.drawPixmap(0, 0, pm);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(silhouette.rect(), halo);
  }
  Q_UNUSED(dpr);
  // ONE device pixel, orthogonal only (field report 2026-08-17: the
  // 8-direction logical-pixel stack rendered as a soft glow — "a little too
  // blurry"). A hairline cardinal outline reads crisp at any DPR.
  const int o = 1;
  QPixmap result(pm.width() + 2 * o, pm.height() + 2 * o);
  result.fill(Qt::transparent);
  {
    QPainter painter(&result);
    painter.drawPixmap(o - o, o, silhouette);
    painter.drawPixmap(o + o, o, silhouette);
    painter.drawPixmap(o, o - o, silhouette);
    painter.drawPixmap(o, o + o, silhouette);
    painter.drawPixmap(o, o, pm);
  }
  return result;
}

/// The Normal style's inverse of silhouetteSiblingFor (field report
/// 2026-08-17: "some icons are all-black"): when the config slot holds the
/// MONOCHROME fallback — the colour wheel 500'd during that scrape, so the
/// black-ink logo won applyEntityArtToConfig's priority walk — probe for a
/// colour sibling (raster wheel, then the colour SVG) that has since landed
/// so the row doesn't render as a dark silhouette on a dark theme. Empty
/// when nothing colour exists yet — the caller keeps the monochrome.
QString colourSiblingFor(const QString &resolvedPath) {
  static const QRegularExpression shared(
      QRegularExpression::anchoredPattern(QStringLiteral("(.*/_shared/)([^/]+)/([^/]+)")));
  const QRegularExpressionMatch m = shared.match(resolvedPath);
  if (!m.hasMatch()) return {};
  const QString base = m.captured(1);
  const QString fileBase = QFileInfo(m.captured(3)).completeBaseName();
  for (const char *dir : {"wheel", "logo-svg"}) {
    for (const char *ext : {"svg", "png", "jpg", "webp"}) {
      const QString candidate = base + QLatin1String(dir) + QLatin1Char('/') + fileBase +
                                QLatin1Char('.') + QLatin1String(ext);
      if (QFileInfo::exists(candidate)) return candidate;
    }
  }
  return {};
}

/// For monochrome/tinted styles, prefer a REAL silhouette source over
/// recolouring the colour wheel (field report 2026-08-17: "the monochrome
/// versions do not actually use monochrome svgs"): given the resolved icon at
/// `.../_shared/<type>/<file>.<ext>`, probe the sibling type directories the
/// platform scrape fills — the monochrome SVG, the monochrome raster
/// ("logo"), then the colour SVG (its alpha still yields a crisp silhouette,
/// and SVG re-rasterises losslessly at any height). Empty when the icon is
/// not shared-scope art or no sibling exists — caller recolours the original.
QString silhouetteSiblingFor(const QString &resolvedPath) {
  static const QRegularExpression shared(
      QRegularExpression::anchoredPattern(QStringLiteral("(.*/_shared/)([^/]+)/([^/]+)")));
  const QRegularExpressionMatch m = shared.match(resolvedPath);
  if (!m.hasMatch()) return {};
  const QString base = m.captured(1);
  const QString fileBase = QFileInfo(m.captured(3)).completeBaseName();
  for (const char *dir : {"logo-monochrome-svg", "logo", "logo-svg"}) {
    for (const char *ext : {"svg", "png", "jpg", "webp"}) {
      const QString candidate = base + QLatin1String(dir) + QLatin1Char('/') + fileBase +
                                QLatin1Char('.') + QLatin1String(ext);
      if (QFileInfo::exists(candidate)) return candidate;
    }
  }
  return {};
}

} // namespace

CollectionTreeController::CollectionTreeController(QObject *parent) : QObject(parent) {}

CollectionTreeController::~CollectionTreeController() = default;

void CollectionTreeController::setupReferences(const CollectionTreeControllerSetup &setup) {
  m_ctx = setup.ctx;
  m_mainLayout = setup.mainLayout;
  m_panelParent = setup.panelParent;
  m_persistCollections = setup.persistCollections;
  m_fullHeightLayout = setup.fullHeightLayout;
  m_toolbarColumnWidget = setup.toolbarColumnWidget;
}

void CollectionTreeController::setupPanel() {
  if (!m_mainLayout || m_panel) {
    return;
  }
  m_panel = new QWidget(m_panelParent);
  m_panel->setObjectName(QStringLiteral("collectionTreePanel"));
  m_panel->setFixedWidth(kPanelWidth);

  // Panel row: [content][grip] (grip order swaps with the dock side in
  // applyPanelWidth). The grip drags the panel's inner edge to resize; width
  // is per-collection state (cfg.collectionTree.treeWidth).
  auto *panelRow = new QHBoxLayout(m_panel);
  panelRow->setContentsMargins(0, 0, 0, 0);
  panelRow->setSpacing(0);
  auto *content = new QWidget(m_panel);
  auto *layout = new QVBoxLayout(content);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  panelRow->addWidget(content, /*stretch=*/1);
  m_grip = new QWidget(m_panel);
  m_grip->setObjectName(QStringLiteral("collectionTreeGrip"));
  m_grip->setFixedWidth(5);
  m_grip->setCursor(Qt::SplitHCursor);
  m_grip->installEventFilter(this);
  panelRow->addWidget(m_grip);

  // The fold marker lives OUTSIDE the panel (sibling in the dock layout):
  // it must stay visible precisely when the panel is not.
  m_foldMarker = new QToolButton(m_panelParent);
  m_foldMarker->setObjectName(QStringLiteral("collectionTreeFoldMarker"));
  m_foldMarker->setAutoRaise(true);
  m_foldMarker->setFixedWidth(14);
  m_foldMarker->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
  m_foldMarker->setCursor(Qt::PointingHandCursor);
  m_foldMarker->setToolTip(tr("Show collection tree"));
  m_foldMarker->setVisible(false);
  connect(m_foldMarker, &QToolButton::clicked, this, [this]() { toggleVisible(); });

  m_header = new QLabel(tr("Collections"), content);
  m_header->setObjectName(QStringLiteral("collectionTreeHeader"));
  m_header->setMargin(8);
  layout->addWidget(m_header);

  m_tree = new TreeBranchView(content);
  m_tree->setProperty("kartendShowLines", false);
  m_tree->setObjectName(QStringLiteral("collectionTreeWidget"));
  m_tree->setHeaderHidden(true);
  m_tree->setRootIsDecorated(true);
  // NOT uniformRowHeights: the icon size is user-configurable (up to 64px)
  // and the no-placeholder rule leaves icon and text-only rows mixed, so row
  // heights genuinely vary. The uniform-height optimisation is for views
  // with thousands of rows; this tree holds a collection list.
  m_tree->setUniformRowHeights(false);
  // iconSize is owned by refreshIcons: it must exactly match the baked
  // canvas width or Qt centres pixmaps in the wider decoration rect and the
  // alignment jitter returns (round 8). No other call site may set it.
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

  // Restore the expansion memory persisted by the previous session (user
  // request 2026-08-17: the tree's shape is remembered). Loaded BEFORE the
  // first rebuild so the initial build already opens the right branches.
  if (m_ctx) {
    if (ISessionManager *session = m_ctx->sessionManager()) {
      const QStringList keys = session->collectionTreeCollapsedKeys();
      m_collapsedUuids = QSet<QString>(keys.begin(), keys.end());
    }
  }

  applyPrimaryColor(QString());
  rebuildTree();
  // Default insert first: on the root view applyStateForCollection returns
  // early (no collection to read), and without this the panel was never in
  // any layout until the first collection switch (Kartend-auh7u fix). Struct
  // defaults, so the root view matches what a fresh collection would show
  // (left + full-height since the 2026-08-17 defaults decision).
  insertPanelAt(CollectionTreeSettings{}.treePosition, CollectionTreeSettings{}.treeJustification);
  applyStateForCollection(activeCollectionIndex());
  syncFoldMarker();
}

void CollectionTreeController::insertPanelAt(DetailsPanePosition position,
                                             SidebarJustification justification) {
  if (!m_mainLayout || !m_panel) {
    return;
  }
  // Kartend-auh7u: FullHeight docks into the outermost sidebar row (window
  // height, toolbar stopping at the panel's edge); BelowToolbar keeps the
  // classic under-toolbar dock. Without the row wired up, FullHeight
  // degrades to BelowToolbar.
  const bool fullHeight =
      justification == SidebarJustification::FullHeight && m_fullHeightLayout != nullptr;
  const SidebarJustification effective =
      fullHeight ? SidebarJustification::FullHeight : SidebarJustification::BelowToolbar;
  if (m_panelInserted && position == m_insertedPosition && effective == m_insertedJustification) {
    return;
  }
  if (m_panelInserted) {
    if (m_mainLayout->indexOf(m_panel) != -1) {
      m_mainLayout->removeWidget(m_panel);
    }
    if (m_fullHeightLayout && m_fullHeightLayout->indexOf(m_panel) != -1) {
      m_fullHeightLayout->removeWidget(m_panel);
    }
  }
  QHBoxLayout *target = fullHeight ? m_fullHeightLayout : m_mainLayout;
  // The tree claims the row's extremes, so it always sits OUTSIDE a
  // full-height details pane (which inserts adjacent to the toolbar column).
  // The fold marker docks at the very same extreme, OUTSIDE the panel, so
  // when the panel hides the marker holds its edge. Its arrow points into
  // the view — the direction the panel would unfold.
  if (m_foldMarker) {
    if (m_mainLayout->indexOf(m_foldMarker) != -1) {
      m_mainLayout->removeWidget(m_foldMarker);
    }
    if (m_fullHeightLayout && m_fullHeightLayout->indexOf(m_foldMarker) != -1) {
      m_fullHeightLayout->removeWidget(m_foldMarker);
    }
  }
  if (position == DetailsPanePosition::Right) {
    target->addWidget(m_panel);
    if (m_foldMarker) {
      target->addWidget(m_foldMarker);
      m_foldMarker->setArrowType(Qt::LeftArrow);
    }
  } else {
    target->insertWidget(0, m_panel);
    if (m_foldMarker) {
      target->insertWidget(0, m_foldMarker);
      m_foldMarker->setArrowType(Qt::RightArrow);
    }
  }
  m_insertedPosition = position;
  m_insertedJustification = effective;
  m_panelInserted = true;
}

void CollectionTreeController::rebuildTree() {
  if (!m_tree || !m_ctx || !m_ctx->collection.collections || !m_ctx->collection.hierarchyCache) {
    return;
  }
  const QList<CollectionConfig> &collections = *m_ctx->collection.collections;
  const CollectionHierarchyCache &hierarchy = *m_ctx->collection.hierarchyCache;

  const CollectionTreeModel::Model model = CollectionTreeModel::build(collections, hierarchy);

  // Structural fingerprint (field report 2026-08-17, round 5: "how can we
  // fix this for good?"): several unrelated paths funnel into
  // rebuildHierarchyCache — playlist resyncs among them — and most arrive
  // with a collection list whose TREE SHAPE is unchanged. A full
  // clear-and-rebuild for those repaints visibly, resets scroll, and churns
  // expansion no matter how carefully state is restored. So: describe the
  // tree this model WOULD build (parent key, expansion key, collection
  // index, name — one row per visual row, display order) and compare against
  // the rows on screen. Identical shape → restyle in place and stop; the
  // expensive path runs only for genuine structural change. This guards the
  // WIDGET, so every present and future redundant caller is covered.
  QStringList desired;
  {
    struct Walk {
      const CollectionTreeModel::Node *node;
      QString parentKey;
    };
    const QChar sep = QChar(0x1f);
    QList<Walk> walk;
    for (const CollectionTreeModel::Node &root : model.collectionRoots) {
      walk.append({&root, QString()});
    }
    while (!walk.isEmpty()) {
      const Walk current = walk.takeLast();
      const int index = current.node->collectionIndex;
      if (index < 0 || index >= collections.size()) continue;
      const QString uuid = hierarchy.collectionUuid(index);
      desired.append(current.parentKey + sep + uuid + sep + QString::number(index) + sep +
                     collections.at(index).name);
      for (auto it = current.node->children.crbegin(); it != current.node->children.crend(); ++it) {
        walk.append({&*it, uuid});
      }
    }
    if (!model.playlistIndices.isEmpty()) {
      desired.append(QString() + sep + kPlaylistsGroupKey + sep + QStringLiteral("-1") + sep +
                     kPlaylistsGroupKey);
      for (int index : model.playlistIndices) {
        if (index < 0 || index >= collections.size()) continue;
        desired.append(kPlaylistsGroupKey + sep + hierarchy.collectionUuid(index) + sep +
                       QString::number(index) + sep + collections.at(index).name);
      }
    }
    QStringList live;
    live.reserve(desired.size());
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
      QTreeWidgetItem *item = *it;
      const QString parentKey =
          item->parent() ? item->parent()->data(0, kRoleExpansionKey).toString() : QString();
      live.append(parentKey + sep + item->data(0, kRoleExpansionKey).toString() + sep +
                  QString::number(item->data(0, kRoleCollectionIndex).toInt()) + sep +
                  item->data(0, kRoleName).toString());
    }
    if (live == desired) {
      refreshIcons();
      return;
    }
  }

  m_suppressSignals = true;
  // Capture the LIVE expansion state before the teardown (field report
  // 2026-08-17: rebuilds triggered by unrelated UI actions visibly churned
  // branches). The on-screen truth wins over the memory set for every key
  // currently present; keys of rows not in this build (filtered playlists,
  // removed collections) keep their remembered state for when they return.
  for (QTreeWidgetItemIterator liveIt(m_tree); *liveIt; ++liveIt) {
    const QString key = (*liveIt)->data(0, kRoleExpansionKey).toString();
    if (key.isEmpty()) continue;
    if ((*liveIt)->isExpanded()) {
      m_collapsedUuids.remove(key);
    } else {
      m_collapsedUuids.insert(key);
    }
  }
  m_tree->clear();

  // Walk the pure model into items iteratively (parent item + node pairs) —
  // the model is already depth-capped, but symmetry with its guard beats a
  // recursive lambda here. The parent COLLECTION index rides along because
  // the icon fallback is per-occurrence: an alias-duplicated child under two
  // parents resolves against each parent's own artwork directory.
  struct Pending {
    QTreeWidgetItem *parent;
    const CollectionTreeModel::Node *node;
    int parentCollectionIndex;
  };
  QList<Pending> work;
  for (const CollectionTreeModel::Node &root : model.collectionRoots) {
    work.append({nullptr, &root, -1});
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
    item->setData(0, kRoleParentCollection, current.parentCollectionIndex);
    item->setData(0, kRoleName, cfg.name);
    const QString uuid = hierarchy.collectionUuid(index);
    item->setData(0, kRoleExpansionKey, uuid);
    item->setData(0, kRoleIsCategory, !current.node->children.isEmpty());
    // Expansion applied in the single post-pass below, once children exist.
    // Reverse-append so takeLast() preserves the model's child order.
    for (auto it = current.node->children.crbegin(); it != current.node->children.crend(); ++it) {
      work.append({item, &*it, index});
    }
  }

  if (!model.playlistIndices.isEmpty()) {
    auto *group = new QTreeWidgetItem(m_tree);
    group->setText(0, tr("Playlists"));
    group->setData(0, kRoleCollectionIndex, -1);
    group->setData(0, kRoleExpansionKey, kPlaylistsGroupKey);
    group->setData(0, kRoleName, kPlaylistsGroupKey);
    group->setData(0, kRoleIsCategory, true);
    group->setFlags(group->flags() & ~Qt::ItemIsSelectable);
    for (int index : model.playlistIndices) {
      if (index < 0 || index >= collections.size()) {
        continue;
      }
      auto *item = new QTreeWidgetItem(group);
      item->setText(0, collections.at(index).name);
      item->setData(0, kRoleCollectionIndex, index);
      item->setData(0, kRoleParentCollection, -1);
      item->setData(0, kRoleName, collections.at(index).name);
      item->setData(0, kRoleExpansionKey, hierarchy.collectionUuid(index));
    }
  }

  // Single post-pass expansion restore: every row exists WITH its children
  // now, so setExpanded is order-safe (mid-build it ran on still-childless
  // items, which Qt does not reliably honour).
  for (QTreeWidgetItemIterator restoreIt(m_tree); *restoreIt; ++restoreIt) {
    const QString key = (*restoreIt)->data(0, kRoleExpansionKey).toString();
    if (key.isEmpty()) continue;
    (*restoreIt)->setExpanded(!m_collapsedUuids.contains(key));
  }

  refreshIcons();

  m_suppressSignals = false;
  highlightCollection(activeCollectionIndex());
  // Kartend-auh7u: rebuilds fire from the collection-list mutation
  // chokepoint, which includes settings-dialog saves — re-apply the active
  // collection's dock state so side/justification edits take effect without
  // a collection switch. Idempotent when nothing changed.
  applyStateForCollection(activeCollectionIndex());
}

void CollectionTreeController::applyPanelWidth(int width, DetailsPanePosition position) {
  if (!m_panel || !m_grip) {
    return;
  }
  m_panel->setFixedWidth(
      std::clamp(width, CollectionTreeSettings::kMinWidth, CollectionTreeSettings::kMaxWidth));
  // The grip belongs on the INNER edge — the one facing the content view.
  // Docked Left that's the row's end; docked Right it's the row's start.
  auto *row = qobject_cast<QHBoxLayout *>(m_panel->layout());
  if (!row) {
    return;
  }
  const int wantIndex = position == DetailsPanePosition::Right ? 0 : row->count() - 1;
  if (row->indexOf(m_grip) != wantIndex) {
    row->removeWidget(m_grip);
    row->insertWidget(wantIndex, m_grip);
  }
}

bool CollectionTreeController::eventFilter(QObject *watched, QEvent *event) {
  if (watched != m_grip || !m_panel) {
    return QObject::eventFilter(watched, event);
  }
  switch (event->type()) {
  case QEvent::MouseButtonPress: {
    auto *me = static_cast<QMouseEvent *>(event);
    if (me->button() != Qt::LeftButton) break;
    m_dragStartX = me->globalPosition().toPoint().x();
    m_dragStartWidth = m_panel->width();
    return true;
  }
  case QEvent::MouseMove: {
    auto *me = static_cast<QMouseEvent *>(event);
    if (!(me->buttons() & Qt::LeftButton)) break;
    // Docked Left the inner edge is the panel's right side, so dragging right
    // grows it; docked Right the sense inverts.
    const int delta = me->globalPosition().toPoint().x() - m_dragStartX;
    const int grown = m_insertedPosition == DetailsPanePosition::Right ? m_dragStartWidth - delta
                                                                       : m_dragStartWidth + delta;
    applyPanelWidth(grown, m_insertedPosition);
    return true;
  }
  case QEvent::MouseButtonRelease: {
    // Persist the settled width on the ACTIVE collection — same ownership
    // rule as toggleVisible: on the root view the resize is live-only,
    // because there is no collection to remember it on.
    if (CollectionConfig *cfg = activeCollectionMutable()) {
      const int settled = m_panel->width();
      if (cfg->collectionTree.treeWidth != settled) {
        cfg->collectionTree.treeWidth = settled;
        if (m_persistCollections) {
          m_persistCollections();
        }
      }
    }
    // The icon width cap derives from the panel width — re-bake at the
    // settled size (not per move event; decode cost belongs on release).
    refreshIcons();
    return true;
  }
  default:
    break;
  }
  return QObject::eventFilter(watched, event);
}

void CollectionTreeController::refreshIcons() {
  if (!m_tree || !m_ctx || !m_ctx->collection.collections) {
    return;
  }
  const QList<CollectionConfig> &collections = *m_ctx->collection.collections;
  const qreal dpr = m_tree->devicePixelRatioF();
  m_bakedPanelWidth = m_panel ? m_panel->width() : kPanelWidth;
  const int indentation = m_tree->indentation();
  // The width budget comes from the tree's ACTUAL viewport (field report
  // 2026-08-17, round 6: an estimated chrome constant left max-aspect logos
  // a few pixels over the edge — the viewport already accounts for the
  // grip, scrollbar, frame, and theme margins, so measure instead of
  // estimating). Depth indentation is subtracted PER ROW below; 8px covers
  // the item's own decoration margin.
  const int viewportWidth =
      m_tree->viewport() && m_tree->viewport()->width() > 0 ? m_tree->viewport()->width()
                                                            : m_bakedPanelWidth - 29;
  // HARD INVARIANT (user directive 2026-08-17): no icon may be wider than
  // the sidebar. The per-row CANVAS is clamped to the space that physically
  // exists right of the row's indentation, so by construction its right
  // edge lands 8px inside the viewport at any depth; the logo gets a
  // further 12px breathing margin inside it. The view's iconSize carries
  // the LARGEST (depth-1) canvas so Qt never scales a canvas to fit.
  const int chrome = 8;
  const int breathing = 12;
  m_tree->setIconSize(QSize(qMax(24, viewportWidth - chrome - indentation), m_iconSize));
  QHash<QString, QIcon> cache; // path|maxW — style/size/tint are uniform per pass

  for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
    QTreeWidgetItem *item = *it;
    const int index = item->data(0, kRoleCollectionIndex).toInt();
    // Category rows — anything with children, including the Playlists group
    // header — read differently from leaves beyond their icon size (user
    // request 2026-08-17): bold label plus a faint full-row band. Both work
    // in icons-only mode and with every icon style.
    const bool isCategory = item->data(0, kRoleIsCategory).toBool();
    QFont rowFont = item->font(0);
    rowFont.setBold(isCategory);
    item->setFont(0, rowFont);
    if (isCategory) {
      QColor band = m_tree->palette().color(QPalette::Text);
      band.setAlpha(14);
      item->setBackground(0, band);
    } else {
      item->setBackground(0, QBrush());
    }
    if (index < 0 || index >= collections.size()) {
      continue; // the Playlists group header keeps its text-only look
    }
    const QString name = collections.at(index).name;
    int depth = 1; // rootIsDecorated indents even top-level rows one unit
    for (QTreeWidgetItem *p = item->parent(); p; p = p->parent()) ++depth;
    const int canvasWidth = qMax(24, viewportWidth - chrome - indentation * depth);
    const int canvasDevW = qMax(1, qRound(canvasWidth * dpr));
    const int maxWidth = qMax(24, canvasWidth - breathing);

    QString parentArtworkDir;
    const int parentIndex = item->data(0, kRoleParentCollection).toInt();
    if (parentIndex >= 0 && parentIndex < collections.size()) {
      const CollectionConfig &parent = collections.at(parentIndex);
      parentArtworkDir = PathUtils::validateAndExpandPath(parent.artworkDirectory, parent.name);
    }
    QString path = CollectionUtils::resolveCollectionTileArtwork(&collections, index, name,
                                                                 parentArtworkDir);
    if (path.isEmpty()) {
      item->setIcon(0, QIcon());
      item->setText(0, name);
      item->setToolTip(0, QString());
      continue;
    }
    // Silhouette styles swap to a genuine monochrome/SVG source when the
    // scrape delivered one; the colour wheel is only the fallback. Normal
    // style runs the INVERSE swap (field report 2026-08-17: "some icons are
    // all-black"): when the wired icon is the monochrome fallback — the
    // colour wheel 500'd during the scrape, so the black-ink logo won the
    // config slot — prefer a colour sibling that has since landed.
    if (m_iconStyle != TreeIconStyle::Normal) {
      const QString sibling = silhouetteSiblingFor(path);
      if (!sibling.isEmpty()) path = sibling;
    } else if (path.contains(QStringLiteral("/_shared/logo/"))) {
      const QString sibling = colourSiblingFor(path);
      if (!sibling.isEmpty()) path = sibling;
    }

    const QString cacheKey = path + QLatin1Char('|') + QString::number(maxWidth);
    auto cached = cache.find(cacheKey);
    if (cached == cache.end()) {
      QPixmap pm;
      const int devHeight = qMax(1, qRound(m_iconSize * dpr));
      const int devWidth = qMax(1, qRound(maxWidth * dpr));
      if (path.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)) {
        // SVG renders at exactly the box we need — no raster upscaling.
        pm = QIcon(path).pixmap(QSize(devWidth, devHeight));
      }
      if (pm.isNull()) {
        pm = trimTransparentBorders(QPixmap(path));
        if (!pm.isNull()) {
          pm = pm.scaledToHeight(devHeight, Qt::SmoothTransformation);
          if (pm.width() > devWidth) {
            pm = pm.scaledToWidth(devWidth, Qt::SmoothTransformation);
          }
        }
      }
      if (!pm.isNull() && m_iconStyle != TreeIconStyle::Normal) {
        QColor ink;
        switch (m_iconStyle) {
        case TreeIconStyle::MonochromeDark:
          ink = QColor(QStringLiteral("#2e2e2e"));
          break;
        case TreeIconStyle::MonochromeLight:
          ink = QColor(QStringLiteral("#e8e8e8"));
          break;
        case TreeIconStyle::Tinted:
          ink = QColor(m_iconTint);
          if (!ink.isValid()) ink = QColor(m_accentColor);
          if (!ink.isValid()) ink = m_tree->palette().color(QPalette::Highlight);
          break;
        case TreeIconStyle::Normal:
          break;
        }
        if (ink.isValid()) {
          QPixmap silhouette(pm.width(), pm.height());
          silhouette.fill(Qt::transparent);
          {
            QPainter painter(&silhouette);
            painter.drawPixmap(0, 0, pm);
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(silhouette.rect(), ink);
          }
          pm = silhouette;
        }
      }
      if (!pm.isNull() && m_tree) {
        pm = ensureContrastAgainst(pm, m_tree->palette().color(QPalette::Base), dpr);
      }
      if (!pm.isNull()) {
        // Uniform-width, LEFT-ALIGNED canvas (field report 2026-08-17: Qt
        // centres each pixmap inside the view's decoration rect, so
        // per-row pixmap widths made narrow logos drift right and wide
        // ones overflow the panel edge — "horribly cut off"). Every icon
        // ships at canvasDevW wide with the logo at x=0; rows at any depth
        // then share one decoration geometry, the transparent overhang on
        // deep rows clips invisibly, and the visible logo always fits its
        // per-row cap.
        QPixmap canvas(canvasDevW, qRound(m_iconSize * dpr));
        canvas.fill(Qt::transparent);
        {
          QPainter painter(&canvas);
          painter.drawPixmap(0, (canvas.height() - pm.height()) / 2, pm);
        }
        canvas.setDevicePixelRatio(dpr);
        pm = canvas;
      }
      cached = cache.insert(cacheKey, pm.isNull() ? QIcon() : QIcon(pm));
    }

    const QIcon &icon = cached.value();
    item->setIcon(0, icon);
    // Icons-only mode: the name moves to the tooltip. Rows whose icon did
    // NOT resolve keep their text — a blank row would be unusable.
    if (!icon.isNull() && m_iconsOnly) {
      item->setText(0, QString());
      item->setToolTip(0, name);
    } else {
      item->setText(0, name);
      item->setToolTip(0, QString());
    }
  }
}

void CollectionTreeController::onCollectionSwitched(int collectionIndex) {
  applyStateForCollection(collectionIndex);
  highlightCollection(collectionIndex);
}

void CollectionTreeController::syncFoldMarker() {
  if (m_foldMarker) {
    m_foldMarker->setVisible(m_panel && !m_panel->isVisible());
  }
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
  insertPanelAt(tree.treePosition, tree.treeJustification);
  applyPanelWidth(tree.treeWidth, tree.treePosition);
  // Icon display options are baked into the rows by rebuildTree, so a
  // change (settings edit, collection switch between differently-configured
  // collections) needs a rebuild — cheap, and the expansion memory keeps the
  // tree's shape across it.
  // Width participates in the rebake decision (field report 2026-08-17,
  // round 4): treeWidth is per-collection, so a switch can resize the panel
  // under icons baked for the previous width — wide logos then clip at the
  // new edge. m_bakedPanelWidth is stamped by refreshIcons itself.
  const bool displayChanged =
      m_iconsOnly != tree.treeIconsOnly || m_iconSize != tree.treeIconSize ||
      m_iconStyle != tree.treeIconStyle || m_iconTint != tree.treeIconTintColor ||
      (m_panel && m_panel->width() != m_bakedPanelWidth);
  m_iconsOnly = tree.treeIconsOnly;
  m_iconSize = tree.treeIconSize;
  m_iconStyle = tree.treeIconStyle;
  m_iconTint = tree.treeIconTintColor;
  if (displayChanged) {
    refreshIcons();
  }
  if (m_tree && m_tree->property("kartendShowLines").toBool() != tree.treeShowLines) {
    m_tree->setProperty("kartendShowLines", tree.treeShowLines);
    if (m_tree->viewport()) m_tree->viewport()->update();
  }
  const bool wasVisible = m_panel->isVisible();
  m_panel->setVisible(tree.treeVisible);
  if (wasVisible != tree.treeVisible) {
    emit visibilityChanged(tree.treeVisible);
  }
  syncFoldMarker();
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
    // The highlight must never CHANGE the tree's shape (field report
    // 2026-08-17: traversing collections made the tree auto-expand — Qt's
    // scrollTo expands collapsed ancestors to reveal the target — and the
    // next rebuild collapsed them again, so navigation visibly churned
    // branches the user had deliberately closed). Prefer a row that is
    // already on an expanded path (alias-duplicated collections appear more
    // than once); when every occurrence is inside a collapsed branch, mark
    // it current WITHOUT scrolling — the branch stays closed, and the
    // highlight appears when the user opens it.
    const auto onExpandedPath = [](QTreeWidgetItem *item) {
      for (QTreeWidgetItem *p = item->parent(); p; p = p->parent()) {
        if (!p->isExpanded()) return false;
      }
      return true;
    };
    QTreeWidgetItem *firstMatch = nullptr;
    QTreeWidgetItem *visibleMatch = nullptr;
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
      if ((*it)->data(0, kRoleCollectionIndex).toInt() != collectionIndex) continue;
      if (!firstMatch) firstMatch = *it;
      if (onExpandedPath(*it)) {
        visibleMatch = *it;
        break;
      }
    }
    if (QTreeWidgetItem *target = visibleMatch ? visibleMatch : firstMatch) {
      m_tree->setCurrentItem(target);
      target->setSelected(true);
      if (visibleMatch) {
        m_tree->scrollToItem(visibleMatch);
      }
    } else {
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
    syncFoldMarker();
    if (m_persistCollections) {
      m_persistCollections();
    }
    return;
  }
  // Root view: live toggle only, nothing to remember it on.
  const bool next = !m_panel->isVisible();
  m_panel->setVisible(next);
  emit visibilityChanged(next);
  syncFoldMarker();
}

void CollectionTreeController::setDockPosition(DetailsPanePosition position) {
  if (position != DetailsPanePosition::Left && position != DetailsPanePosition::Right) {
    return;
  }
  insertPanelAt(position, m_insertedJustification);
  // The grip must follow the panel to its new inner edge.
  applyPanelWidth(m_panel ? m_panel->width() : CollectionTreeSettings{}.treeWidth, position);
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
  // The Tinted icon style defaults to the accent — when the accent changes
  // under it (per-collection theming), the baked pixmaps are stale.
  const bool accentChanged = m_accentColor != hexColor;
  m_accentColor = hexColor;
  if (accentChanged && m_iconStyle == TreeIconStyle::Tinted && m_iconTint.isEmpty()) {
    refreshIcons();
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
  // Collapsed-set semantics (user decision 2026-08-17): only deliberate
  // collapses are remembered; expansion is the default state.
  if (expanded) {
    m_collapsedUuids.remove(key);
  } else {
    m_collapsedUuids.insert(key);
  }
  if (m_ctx) {
    if (ISessionManager *session = m_ctx->sessionManager()) {
      session->setCollectionTreeCollapsedKeys(
          QStringList(m_collapsedUuids.begin(), m_collapsedUuids.end()));
      session->saveToDisk();
    }
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
