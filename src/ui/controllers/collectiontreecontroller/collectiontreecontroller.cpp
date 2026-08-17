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
#include <QApplication>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QStyleOption>
#include <QTimer>
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
constexpr int kRoleBakedPixmap = Qt::UserRole + 5; // QPixmap; painted by TreeIconDelegate
/// Symmetric horizontal margin the icons keep from the panel edges.
constexpr int kPanelChrome = 8;

/// Expansion-memory key for the synthetic Playlists group row. UUIDs are
/// derived from name+mediaDirectory, so a literal that can't collide.
const QString kPlaylistsGroupKey = QStringLiteral("::playlists-group::");

/// Thin-logo height boost (user direction 2026-08-17: "thinner than usual
/// icons should be made taller to compensate"): a wordmark whose aspect
/// exceeds the reference may exceed the configured icon height, up to the
/// boost cap — rows are non-uniform, so only those rows grow.
constexpr qreal kThinAspectRef = 3.0;
constexpr qreal kThinHeightBoost = 2.2;

/// Paints the baked row pixmap directly in viewport coordinates — TRUE
/// panel centring at any depth. Qt's decoration mechanism cannot do this:
/// the decoration never paints left of the row's indent, so a
/// panel-centred logo on an indented row is unreachable through QIcon
/// (chased through three geometry rounds on 2026-08-17 before this
/// delegate ended it). Rows without a baked pixmap (text fallback) use the
/// default paint, which honours the category font/band roles.
class TreeIconDelegate : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;

protected:
  void paint(QPainter *painter, const QStyleOptionViewItem &option,
             const QModelIndex &index) const override {
    const QPixmap pm = index.data(kRoleBakedPixmap).value<QPixmap>();
    if (pm.isNull()) {
      QStyledItemDelegate::paint(painter, option, index);
      return;
    }
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    opt.icon = QIcon();
    opt.text.clear();
    const QWidget *widget = option.widget;
    QStyle *style = widget ? widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);

    const qreal pmDpr = pm.devicePixelRatio() > 0 ? pm.devicePixelRatio() : 1.0;
    const int w = qMax(1, qRound(pm.width() / pmDpr));
    const int h = qMax(1, qRound(pm.height() / pmDpr));
    // The single column stretches to the viewport, so the item rect's right
    // edge IS the panel's inner width.
    const int panelRight = option.rect.right() + 1;
    int x = (panelRight - w) / 2; // panel-centred
    if (index.data(kRoleIsCategory).toBool()) {
      // Category rows own a chevron in their branch cell — never under it.
      x = qMax(option.rect.left(), x);
    }
    x = qMin(x, panelRight - kPanelChrome - w);
    x = qMax(x, kPanelChrome);
    const int y = option.rect.top() + (option.rect.height() - h) / 2;
    painter->drawPixmap(QRect(x, y, w, h), pm);
  }
};

/// QTreeWidget whose branch column can hide the connector lines while
/// keeping the expand chevrons (user request 2026-08-17: tree lines off by
/// default, optional). Styles draw lines from State_Sibling/State_Item in
/// PE_IndicatorBranch; painting the primitive ourselves with ONLY the
/// children/open states yields just the arrow. Toggled via the
/// "kartendShowLines" dynamic property so the controller's member type can
/// stay QTreeWidget*.
/// The fold marker: a slim vertical tab painted with a rotated label and
/// chevron. A bare 14px autoRaise arrow proved invisible in practice on
/// dark themes (field report 2026-08-17, twice) — a labelled tab is the
/// smallest thing that is genuinely discoverable.
class FoldMarkerButton : public QToolButton {
public:
  using QToolButton::QToolButton;

protected:
  void paintEvent(QPaintEvent * /*event*/) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const bool hover = underMouse();
    painter.setPen(palette().color(QPalette::Mid));
    painter.setBrush(hover ? palette().color(QPalette::Highlight)
                           : palette().color(QPalette::Button));
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    painter.setPen(hover ? palette().color(QPalette::HighlightedText)
                         : palette().color(QPalette::ButtonText));
    const QChar chevron = arrowType() == Qt::LeftArrow ? QChar(0x25C2) : QChar(0x25B8);
    const QString label = QString(chevron) + QLatin1Char(' ') +
                          CollectionTreeController::tr("Collections") + QLatin1Char(' ') +
                          QString(chevron);
    painter.translate(width() / 2.0, height() / 2.0);
    painter.rotate(-90);
    painter.drawText(QRectF(-height() / 2.0, -width() / 2.0, height(), width()), Qt::AlignCenter,
                     label);
  }
};

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
    // Paint the chevron OURSELVES: delegating to PE_IndicatorBranch with
    // only the children/open states draws nothing at all on some styles
    // (field report 2026-08-17 — Breeze), leaving branches with no fold
    // indicator. A small solid triangle in the palette text colour is
    // style-independent and always visible.
    const int unit = indentation();
    const QRectF cell(rect.right() - unit + 1, rect.top(), unit, rect.height());
    const QPointF c = cell.center();
    const qreal r = qMin(cell.width(), cell.height()) * 0.22;
    QPolygonF triangle;
    if (isExpanded(index)) {
      triangle << QPointF(c.x() - r, c.y() - r * 0.6) << QPointF(c.x() + r, c.y() - r * 0.6)
               << QPointF(c.x(), c.y() + r);
    } else {
      triangle << QPointF(c.x() - r * 0.6, c.y() - r) << QPointF(c.x() - r * 0.6, c.y() + r)
               << QPointF(c.x() + r, c.y());
    }
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    QColor ink = palette().color(QPalette::Text);
    ink.setAlpha(190);
    painter->setPen(Qt::NoPen);
    painter->setBrush(ink);
    painter->drawPolygon(triangle);
    painter->restore();
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

/// Colour-source repair for every style (field report 2026-08-17: "some
/// icons are all-black"): when the config slot holds the
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

// (The former silhouetteSiblingFor probe — swapping mono/tint styles to the
// dedicated monochrome sources — was retired 2026-08-17: those sources have
// different aspect ratios from the colour wheels, so switching styles
// visibly resized rows. The luminance mapping recolours the colour art with
// its detail intact, so every style now shares one source geometry.)

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
  m_foldMarker = new FoldMarkerButton(m_panelParent);
  m_foldMarker->setObjectName(QStringLiteral("collectionTreeFoldMarker"));
  m_foldMarker->setFixedWidth(20);
  m_foldMarker->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
  m_foldMarker->setCursor(Qt::PointingHandCursor);
  m_foldMarker->setToolTip(tr("Show collection tree"));
  m_foldMarker->setVisible(false);
  connect(m_foldMarker, &QToolButton::clicked, this, [this]() { toggleVisible(); });

  // No "Collections" header label (user request 2026-08-17: "we all know
  // they are collections") — the tree starts at the panel's top edge.
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
  // Tight indent unit (user request 2026-08-17: the default unit tripled up
  // by depth ate a third of the panel — with less fixed margin the per-depth
  // steps read clearly).
  m_tree->setIndentation(16);
  // iconSize is owned by refreshIcons: it must exactly match the baked
  // canvas width or Qt centres pixmaps in the wider decoration rect and the
  // alignment jitter returns (round 8). No other call site may set it.
  m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
  m_tree->setFocusPolicy(Qt::ClickFocus);
  if (m_tree->viewport()) {
    m_tree->viewport()->installEventFilter(this);
  }
  m_tree->setItemDelegateForColumn(0, new TreeIconDelegate(m_tree));
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
    // Childless rows cannot testify: Qt reports a childless item as
    // not-expanded no matter what, and during startup the hierarchy
    // populates in waves — an early build's childless shell rows would be
    // captured as "user collapsed" here, poisoning the collapse memory on
    // every launch (field report 2026-08-17: "collapsed by default" with an
    // EMPTY session collapse list). Rows with no children keep whatever
    // state the memory already holds.
    if ((*liveIt)->childCount() == 0) continue;
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
    // Shells without a media directory have NO uuid (the hierarchy cache
    // only computes one when mediaDir is set), which silently excluded them
    // from ALL expansion memory — restore, capture, and persistence skip
    // empty keys — so exactly the grouping rows opened collapsed and forgot
    // everything (field report 2026-08-17). Fall back to a stable
    // name-derived key: parent name + own name survives restarts and index
    // shuffles for hand-made shells.
    QString expansionKey = hierarchy.collectionUuid(index);
    if (expansionKey.isEmpty()) {
      const QString parentName = (current.parentCollectionIndex >= 0 &&
                                  current.parentCollectionIndex < collections.size())
                                     ? collections.at(current.parentCollectionIndex).name
                                     : QString();
      expansionKey =
          QStringLiteral("::name::") + parentName + QLatin1Char('/') + cfg.name;
    }
    item->setData(0, kRoleExpansionKey, expansionKey);
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
      QString playlistKey = hierarchy.collectionUuid(index);
      if (playlistKey.isEmpty()) {
        playlistKey = QStringLiteral("::name::playlists/") + collections.at(index).name;
      }
      item->setData(0, kRoleExpansionKey, playlistKey);
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
  if (m_tree && watched == m_tree->viewport()) {
    if (event->type() == QEvent::Resize &&
        m_tree->viewport()->width() != m_bakedViewportWidth && m_bakedViewportWidth != 0) {
      // Deferred: icon swaps inside a resize re-enter layout. The width
      // check keeps the scrollbar-toggle feedback loop convergent.
      QTimer::singleShot(0, this, [this]() {
        if (m_tree && m_tree->viewport() &&
            m_tree->viewport()->width() != m_bakedViewportWidth) {
          refreshIcons();
        }
      });
    }
    return false;
  }
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
  // the sidebar. Leaf budgets are viewport minus the symmetric
  // kPanelChrome margins; category budgets additionally stop short of the
  // chevron cell plus this breathing margin. TreeIconDelegate clamps the
  // painted position to the same margins.
  const int breathing = 12;
  m_bakedViewportWidth = viewportWidth;
  struct BakedIcon {
    QPixmap pixmap; // painted by TreeIconDelegate in viewport coordinates
    int logicalHeight = 0;
  };
  QHash<QString, BakedIcon> cache; // path|maxW — style/size/tint are uniform per pass

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
    // Width budget: LEAF rows have no chevron, so the indent column is
    // dead space — they may span the whole panel minus the symmetric
    // chrome margins (TreeIconDelegate centres them on the panel).
    // Category rows stop short of their chevron cell.
    const bool categoryRow = item->data(0, kRoleIsCategory).toBool();
    const int maxWidth =
        categoryRow ? qMax(24, viewportWidth - kPanelChrome - indentation * depth - breathing)
                    : qMax(24, viewportWidth - 2 * kPanelChrome);

    QString parentArtworkDir;
    const int parentIndex = item->data(0, kRoleParentCollection).toInt();
    if (parentIndex >= 0 && parentIndex < collections.size()) {
      const CollectionConfig &parent = collections.at(parentIndex);
      parentArtworkDir = PathUtils::validateAndExpandPath(parent.artworkDirectory, parent.name);
    }
    QString path = CollectionUtils::resolveCollectionTileArtwork(&collections, index, name,
                                                                 parentArtworkDir);
    if (path.isEmpty()) {
      item->setData(0, kRoleBakedPixmap, QVariant());
      item->setIcon(0, QIcon());
      item->setText(0, name);
      item->setToolTip(0, QString());
      item->setSizeHint(0, QSize()); // text-only rows: default metrics
      continue;
    }
    // Every style renders the SAME source art (field report 2026-08-17:
    // the dedicated silhouette sources have different aspect ratios from
    // the colour wheels, so mono/tint rows visibly resized against Normal).
    // The luminance mapping preserves detail, so recolouring the colour art
    // beats swapping sources; the colour-sibling upgrade still repairs rows
    // whose config slot holds the black-ink fallback ("some icons are
    // all-black" — the colour wheel 500'd during that scrape).
    if (path.contains(QStringLiteral("/_shared/logo/"))) {
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
        // Render at 2x the target box, then TRIM: the scraped monochrome
        // SVGs park the art inside padded viewBoxes, so an untrimmed render
        // floated the logo wherever the viewBox put it (field report
        // 2026-08-17: "alignment is still off in mono/tinted mode" — the
        // mono styles are exactly the ones that swap to SVG sources). The
        // oversized render keeps the post-trim downscale sharp.
        pm = QIcon(path).pixmap(
            QSize(devWidth * 2, qRound(devHeight * kThinHeightBoost) * 2));
      }
      if (pm.isNull()) {
        pm = QPixmap(path);
      }
      pm = trimTransparentBorders(pm);
      if (!pm.isNull()) {
        // Box-fit with a thin-logo height boost (user directions
        // 2026-08-17): every logo fills the available box, and a wordmark
        // wider than the reference aspect earns a taller box —
        // aspect/kThinAspectRef, capped at kThinHeightBoost — so thin marks
        // stop reading smaller than square ones. Aspect is always
        // preserved and the box is a hard bound, so nothing crops.
        const qreal aspect =
            pm.height() > 0 ? static_cast<qreal>(pm.width()) / pm.height() : 1.0;
        const qreal boost = std::clamp(aspect / kThinAspectRef, 1.0, kThinHeightBoost);
        const int allowedDevH = qMax(1, qRound(devHeight * boost));
        pm = pm.scaled(devWidth, allowedDevH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
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
          // Luminance-PRESERVING conversion (field report 2026-08-17: the
          // flat SourceIn fill turned every logo into a solid blob — the
          // Nintendo pill's text, counters, and inner detail all vanished).
          // Map each pixel's luminance into a band anchored at the ink:
          // light ink -> [ink-115 .. ink], dark ink -> [ink .. ink+115];
          // Tinted keeps the tint's hue and varies lightness. Alpha is
          // untouched, so real monochrome sources pass through unchanged.
          QImage img = pm.toImage().convertToFormat(QImage::Format_ARGB32);
          const bool lightInk = qGray(ink.rgb()) >= 128;
          const float tintHue = ink.hslHueF();
          const float tintSat = ink.hslSaturationF();
          for (int y = 0; y < img.height(); ++y) {
            auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
            for (int x = 0; x < img.width(); ++x) {
              const int a = qAlpha(line[x]);
              if (a == 0) continue;
              const int g = qGray(line[x]);
              if (m_iconStyle == TreeIconStyle::Tinted) {
                const QColor c = QColor::fromHslF(
                    tintHue < 0 ? 0 : tintHue, tintSat,
                    0.30F + 0.55F * (static_cast<float>(g) / 255.0F));
                line[x] = qRgba(c.red(), c.green(), c.blue(), a);
              } else {
                const int v = lightInk ? 140 + g * 115 / 255 : g * 115 / 255;
                line[x] = qRgba(v, v, v, a);
              }
            }
          }
          pm = QPixmap::fromImage(img);
        }
      }
      if (!pm.isNull() && m_tree) {
        pm = ensureContrastAgainst(pm, m_tree->palette().color(QPalette::Base), dpr);
        // The halo pads by 2px; shrink back inside the budget rather than
        // letting the paint clip it (user: "i just dont want anything
        // cropped").
        const int maxDevH = qRound(m_iconSize * kThinHeightBoost * dpr);
        if (pm.height() > maxDevH) {
          pm = pm.scaledToHeight(maxDevH, Qt::SmoothTransformation);
        }
        if (pm.width() > devWidth) {
          pm = pm.scaledToWidth(devWidth, Qt::SmoothTransformation);
        }
      }
      BakedIcon baked;
      if (!pm.isNull()) {
        pm.setDevicePixelRatio(dpr);
        baked.pixmap = pm;
        baked.logicalHeight = qMax(1, qRound(pm.height() / dpr));
      }
      cached = cache.insert(cacheKey, baked);
    }

    const QPixmap &baked = cached.value().pixmap;
    item->setIcon(0, QIcon()); // TreeIconDelegate paints; no decoration
    item->setData(0, kRoleBakedPixmap, baked.isNull() ? QVariant() : QVariant(baked));
    // Per-row height hugs the baked pixmap (+4px breathing): boosted
    // wordmark rows are taller, square rows stay tight — the view-wide
    // decoration height must never set row heights again (2026-08-17,
    // "continued failure": every row ballooned to the boost headroom).
    if (!baked.isNull()) {
      item->setSizeHint(0, QSize(0, cached.value().logicalHeight + 4));
    } else {
      item->setSizeHint(0, QSize());
    }
    // Icons-only mode: the name moves to the tooltip. Rows whose icon did
    // NOT resolve keep their text — a blank row would be unusable.
    if (!baked.isNull() && m_iconsOnly) {
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
  // isHidden(), NOT !isVisible(): before the window maps, every widget's
  // EFFECTIVE visibility is false, so the startup sync would show the
  // marker beside a soon-visible panel (caught by TestCollectionTreePanel).
  // isHidden() tracks the intended state independent of mapping.
  if (m_foldMarker) {
    m_foldMarker->setVisible(m_panel && m_panel->isHidden());
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
  if (key.isEmpty() || item->childCount() == 0) {
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
