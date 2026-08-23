#include "collectiontreecontroller.h"

#include <algorithm>

#include <QAbstractScrollArea>
#include <QApplication>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QStyleOption>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "collection/collectiontreemodel.h"
#include "collection/typehelpers.h"
#include "colorcontrast.h"
#include "inavigationmanager.h"
#include "isessionmanager.h"
#include "kdecolorscheme.h"
#include "overlayscrollbars.h"
#include "pathutils.h"
#include "retroarchicons.h"
#include "retroarchutils.h"
#include "uiconstants/detailspaneconstants.h"

namespace {

/// Fixed panel width, matching the proportions of the details pane's fixed
/// dock without sharing its per-collection width machinery — the tree's
/// content is names, not artwork, so one width serves.
constexpr int kPanelWidth = 240;

/// Item data roles.
constexpr int kRoleCollectionIndex = Qt::UserRole;      // int; -1 for the group header
constexpr int kRoleExpansionKey = Qt::UserRole + 1;     // QString; UUID or reserved key
constexpr int kRoleParentCollection = Qt::UserRole + 2; // int; -1 for roots/playlists
constexpr int kRoleName = Qt::UserRole + 3; // QString; cfg.name (text may be blank in icons-only)
constexpr int kRoleIsCategory = Qt::UserRole + 4;   // bool; row has children (incl. group header)
constexpr int kRoleBakedPixmap = Qt::UserRole + 5;  // QPixmap; painted by TreeIconDelegate
constexpr int kRoleRootFill = Qt::UserRole + 6;     // QColor; edge-to-edge fill for root rows
constexpr int kRoleCategoryBand = Qt::UserRole + 7; // QColor; rounded band for category rows
/// bool; true only for a genuine ROOT collection (one with no parent in the
/// model). Not derivable from item->parent() any more: the chrome row's
/// children are promoted to top-level items so they align with the Playlists
/// heading (see the build walk), so "has no parent item" is now true for rows
/// that are not roots at all. Playlist rows carry a parentCollection of -1 too,
/// so that field cannot stand in for it either — hence an explicit flag.
constexpr int kRoleIsRootCollection = Qt::UserRole + 8;
/// QPixmap; the small RetroArch system glyph drawn immediately left of the
/// row's NAME (Kartend-1kkk2). A separate role from kRoleBakedPixmap because
/// it is a separate mark from a separate source with its own size and its own
/// per-collection option set — the two can appear on the same row.
constexpr int kRoleSystemGlyph = Qt::UserRole + 9;
/// int (SystemIconPlacement); where that glyph sits relative to the name.
/// Per-ITEM rather than a view property like kartendIconDisplay, because the
/// glyph itself is per-collection — a row that has one carries its own choice
/// of where it goes, instead of inheriting whichever collection happens to be
/// active.
constexpr int kRoleSystemGlyphPlacement = Qt::UserRole + 10;
/// Corner radius of a SUBCOLLECTION's selection backdrop. Root rows are
/// deliberately square — see the delegate.
constexpr int kSelectionRadius = 20;
/// Extra clearance beyond the scrollbar lane so a row's backdrop reads as
/// inset rather than running to the edge (user request 2026-08-19: "a bit
/// less wide"). Applied to band, hover and selection alike so their corner
/// radii match instead of the right pair being clipped off.
constexpr int kRowRightInset = 8;
/// Trims the pill's height — a backdrop the full row height reads as a slab
/// rather than a pill (user request 2026-08-19: "the pills are also too
/// tall").
///
/// Reduced 5 -> 2 on 2026-08-22 ("not tall enough"). The original 5 was tuned
/// against icon rows, which are tall enough that trimming 10px still leaves a
/// generous pill; a text-only row is only as tall as the label, so the same
/// trim left a thin strip around the text.
constexpr int kRowVerticalInset = 2;
/// Horizontal padding either side of a LABEL inside its pill, used when the
/// pill is sized to its text rather than run out to the panel edge.
constexpr int kRowTextPadding = 14;
/// Vertical padding above and below a LABEL inside its pill. With the pill
/// sized from the font rather than the row, this is what sets its height.
constexpr int kPillVerticalPadding = 5;
/// Corner radius for a text-sized pill. Smaller than kSelectionRadius, which
/// was tuned against tall icon rows: at 20 against a pill only as tall as its
/// text, Qt clamps the arc to half the height and the result is a lozenge with
/// semicircular ends rather than a rounded rectangle (user request 2026-08-22:
/// "backdrop pill of selected category too pointy").
constexpr int kTextPillRadius = 9;
/// Breathing room between a logo and the edge of the pill behind it.
constexpr int kRowLogoPadding = 10;
/// Symmetric horizontal margin the icons keep from the panel edges
/// (widened from 8 on 2026-08-18 — logos ran too close to both edges).
constexpr int kPanelChrome = 18;
/// Icon-and-text mode only (Kartend-j1mtg): breathing room above/below the
/// inline icon so it cannot press against the row edge, and the gap between it
/// and the label. Small deliberately — the point of the mode is that the icon
/// accompanies the name rather than competing with it.
constexpr int kIconTextVMargin = 3;
constexpr int kIconTextGap = 8;

/// Gap between the RetroArch system glyph and the name it introduces
/// (Kartend-1kkk2). Tighter than kIconTextGap on purpose: this glyph is
/// scaled to about text height and reads as part of the label, the way an
/// icon in a menu entry does, rather than as artwork standing beside it.
constexpr int kSystemGlyphGap = 6;

/// Expansion-memory key for the synthetic Playlists group row. UUIDs are
/// derived from name+mediaDirectory, so a literal that can't collide.
const QString kPlaylistsGroupKey = QStringLiteral("::playlists-group::");

/// Thin-logo height boost (user direction 2026-08-17: "thinner than usual
/// icons should be made taller to compensate"): a wordmark whose aspect
/// exceeds the reference may exceed the configured icon height, up to the
/// boost cap — rows are non-uniform, so only those rows grow.
constexpr qreal kThinHeightBoost = 2.2;
/// Widest a logo may get, as a multiple of the configured icon size. Together
/// with kThinHeightBoost this defines a fixed BOX every logo is fitted into.
///
/// Height-only sizing (the previous scheme, and the two-sided boost curve
/// that replaced it) could not normalise anything, because it left width
/// completely unbounded: a wordmark at a given height still covered several
/// times the area of a square mark at that same height, so the two never read
/// as the same size (field reports 2026-08-20, twice: "wide ones are easy to
/// see, but others aren't", then "icons still not normalized enough").
///
/// Deriving the box from the CONFIGURED SIZE — never from the panel width —
/// is what keeps this stable: an earlier width clamp measured the viewport,
/// which made every icon resize when the sidebar was dragged.
constexpr qreal kIconMaxAspect = 5.5;

/// Draws the expand/collapse chevron for @p index inside the branch column
/// to the left of @p option.rect. Shared so the view and the delegate
/// cannot drift: the delegate re-draws it after painting a full-width row
/// band over the view's own chevron.
void drawChevron(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) {
  const auto *view = qobject_cast<const QTreeView *>(option.widget);
  if (!view) {
    return;
  }
  const int unit = view->indentation();
  const QRectF cell(option.rect.left() - unit, option.rect.top(), unit, option.rect.height());
  const QPointF centre = cell.center();
  const qreal r = qMin(cell.width(), cell.height()) * 0.22;
  QPolygonF triangle;
  if (view->isExpanded(index)) {
    triangle << QPointF(centre.x() - r, centre.y() - r * 0.6)
             << QPointF(centre.x() + r, centre.y() - r * 0.6)
             << QPointF(centre.x(), centre.y() + r);
  } else {
    triangle << QPointF(centre.x() - r * 0.6, centre.y() - r)
             << QPointF(centre.x() - r * 0.6, centre.y() + r)
             << QPointF(centre.x() + r, centre.y());
  }
  painter->save();
  painter->setRenderHint(QPainter::Antialiasing);
  QColor ink = view->palette().color(QPalette::Text);
  ink.setAlpha(190);
  painter->setPen(Qt::NoPen);
  painter->setBrush(ink);
  painter->drawPolygon(triangle);
  painter->restore();
}

/// Keeps the ROOT collection row parked at the top of the tree while the
/// rest of the list scrolls beneath it (user request 2026-08-19).
///
/// It is a PAINTED COPY, not a moved row: taking the item out of the model
/// would disturb selection, expansion and every index the controller holds.
/// The copy is rendered through the tree's own delegate, so the pinned row
/// and the real one can never drift apart in colour, shape or icon — the
/// delegate already paints in viewport coordinates, which is exactly what a
/// header at y=0 needs.
class StickyRootHeader : public QWidget {
public:
  explicit StickyRootHeader(QTreeWidget *tree) : QWidget(tree->viewport()), m_tree(tree) { hide(); }

  /// Show the pinned copy only once the real row has scrolled out of sight,
  /// so nothing is drawn twice.
  void sync() {
    QTreeWidgetItem *root = rootItem();
    if (!m_tree || !m_tree->viewport() || !root) {
      hide();
      return;
    }
    const QRect real = m_tree->visualItemRect(root);
    if (real.isNull()) {
      hide();
      return;
    }
    // ALWAYS on screen, not only once the real row scrolls away. Swapping a
    // copy in at the moment the row left the viewport meant the row appeared
    // to move and change as it pinned, however closely the copy matched
    // (field report 2026-08-19: "Games should stay static"). Kept permanently
    // at y=0 it simply covers the real row at scroll-top — identical pixels,
    // no transition at all — and stays put as the list scrolls beneath.
    setGeometry(0, 0, m_tree->viewport()->width(), real.height());
    show();
    raise();
    update();
  }

protected:
  void paintEvent(QPaintEvent * /*event*/) override {
    QTreeWidgetItem *root = rootItem();
    // The COLUMN-0 delegate — TreeIconDelegate — not the view default.
    // itemDelegate() returns the default QStyledItemDelegate, whose text
    // colour the panel stylesheet overrides, so the pinned copy rendered
    // white while every tint fix landed in the column delegate underneath —
    // pixels this permanent overlay covers (root cause of six rounds of
    // "still not tinted", 2026-08-19). Same delegate, same pixels.
    QAbstractItemDelegate *delegate = m_tree ? m_tree->itemDelegateForColumn(0) : nullptr;
    if (!delegate && m_tree) {
      delegate = m_tree->itemDelegate();
    }
    if (!root || !delegate || !m_tree->model()) {
      return;
    }
    const QModelIndex index = m_tree->model()->index(0, 0, QModelIndex());
    if (!index.isValid()) {
      return;
    }
    QPainter painter(this);
    // Opaque base first: the widget is transparent by default, so a row
    // scrolling underneath showed through the pinned copy.
    // TITLEBAR colour, matching the real root row's fill — the pinned copy is
    // always a root row, and the window colour showed through wherever the
    // delegate's own fill did not reach (user request 2026-08-19).
    const QColor titlebar = KdeColorScheme::activeTitlebarColor();
    painter.fillRect(rect(),
                     titlebar.isValid() ? titlebar : m_tree->palette().color(QPalette::Window));
    QStyleOptionViewItem opt;
    opt.initFrom(m_tree);
    // The REAL row's rect, moved to the top — not this widget's rect. The
    // row rect carries the tree's indentation and column width, so painting
    // into a rect starting at x=0 shifted the label and logo left the
    // instant the row pinned (field report 2026-08-19: "it should not change
    // position or appearance"). Only y moves.
    const QRect real = m_tree->visualItemRect(root);
    opt.rect = QRect(real.x(), 0, real.width(), real.height());
    opt.widget = m_tree;
    opt.state = QStyle::State_Enabled;
    if (root->isSelected()) {
      opt.state |= QStyle::State_Selected;
    }
    delegate->paint(&painter, opt, index);
  }

  void mousePressEvent(QMouseEvent * /*event*/) override {
    // The pinned copy behaves like the row it stands in for rather than
    // letting the click fall through to whatever happens to be underneath.
    if (QTreeWidgetItem *root = rootItem()) {
      m_tree->setCurrentItem(root);
    }
  }

private:
  [[nodiscard]] QTreeWidgetItem *rootItem() const {
    return (m_tree && m_tree->topLevelItemCount() > 0) ? m_tree->topLevelItem(0) : nullptr;
  }
  QTreeWidget *m_tree = nullptr;
};

/// Typed access to the controller's type-erased member (the class above is
/// file-local, so the header cannot name it).
void syncStickyRoot(QWidget *header) {
  if (auto *sticky = static_cast<StickyRootHeader *>(header)) {
    sticky->sync();
  }
}

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
    // ROOT-collection rows are filled edge to edge, in VIEWPORT coordinates.
    // A per-item background only covers the item's own rect, which the tree
    // indents — so setting it left an unfilled strip down the left and the
    // row read as a floating block instead of chrome (field report
    // 2026-08-19: "not spanning the entire area, leaving a gap").
    const int spanWidth =
        option.widget ? option.widget->property("kartendRowSpanWidth").toInt() : 0;
    const int fullWidth = spanWidth > 0 ? spanWidth : option.rect.right() + 1;
    // The synthetic Playlists group is a TOP-LEVEL item, so the plain
    // parent-validity test called it a root and gave it the square, edge-to-edge
    // chrome shape. It is a section header inside the list, not chrome
    // continuous with the toolbar, and the comment in fillBackdrop already said
    // it should wear the pill — the test simply did not agree with the comment
    // (user request 2026-08-22: "the playlists line item needs to have a
    // rounded pill backdrop too").
    const bool isPlaylistsGroup = index.data(kRoleName).toString() == kPlaylistsGroupKey;
    // Reads the explicit root flag rather than testing for a parent index. Two
    // kinds of row are top-level without being roots: the Playlists heading,
    // and the chrome row's promoted children — both would take the square,
    // edge-to-edge chrome shape under the old test.
    const bool isRootRow = index.data(kRoleIsRootCollection).toBool() && !isPlaylistsGroup;

    // Hoisted above bodyRect because the pill's WIDTH now depends on it: a
    // text-only row is sized to its label, not run out to the panel edge.
    const auto displayMode = static_cast<TreeIconDisplay>(
        option.widget ? option.widget->property("kartendIconDisplay").toInt()
                      : static_cast<int>(TreeIconDisplay::IconAndText));

    // The RetroArch system glyph (Kartend-1kkk2) introduces the NAME — the
    // user asked for "a small icon to the left of the title text" — so it
    // appears in exactly the modes that draw a name, and is skipped in
    // IconOnly, where the row deliberately IS the picture and there is no
    // title for it to sit beside.
    //
    // Hoisted this high because the pill's width depends on it: a text-sized
    // pill has to make room for the glyph as well as the label, or the glyph
    // would be drawn over its own backdrop's edge.
    const QPixmap systemGlyph = displayMode == TreeIconDisplay::IconOnly
                                    ? QPixmap()
                                    : index.data(kRoleSystemGlyph).value<QPixmap>();
    const QSize glyphSize = [&systemGlyph]() -> QSize {
      if (systemGlyph.isNull()) {
        return {};
      }
      const qreal dpr = systemGlyph.devicePixelRatio() > 0 ? systemGlyph.devicePixelRatio() : 1.0;
      return {qMax(1, qRound(systemGlyph.width() / dpr)),
              qMax(1, qRound(systemGlyph.height() / dpr))};
    }();
    // What the glyph costs the label horizontally: itself plus its gap, or
    // nothing at all when there is no glyph — so every measurement below can
    // add it unconditionally.
    const int glyphAdvance = systemGlyph.isNull() ? 0 : glyphSize.width() + kSystemGlyphGap;
    const auto glyphPlacement =
        static_cast<SystemIconPlacement>(index.data(kRoleSystemGlyphPlacement).toInt());
    // The two placements that HUG the name grow the pill and shift the text;
    // RowEnd instead pins the glyph to the panel's inner edge, outside the
    // pill entirely, so it takes its space off the row's right limit rather
    // than out of the backdrop. Splitting the advance in two here is what lets
    // every measurement below stay a plain addition.
    const bool glyphAtRowEnd = glyphPlacement == SystemIconPlacement::RowEnd;
    const int inlineGlyphAdvance = glyphAtRowEnd ? 0 : glyphAdvance;
    const int edgeGlyphReserve = glyphAtRowEnd ? glyphAdvance : 0;

    // Hoisted out of bodyRect so the RowEnd placement can pin its glyph to the
    // same edge the pill stops at — the two have to agree, or the glyph lands
    // either on top of the backdrop or out past the scrollbar.
    const auto *scrollArea = qobject_cast<const QAbstractScrollArea *>(option.widget);
    const int viewportWidth =
        (scrollArea && scrollArea->viewport()) ? scrollArea->viewport()->width() : fullWidth;
    const int scrollLane = OverlayScrollbars::reservedGutter(option.widget);
    /// Where a row's content must stop: inside the scrollbar lane and the
    /// standard inset. The RowEnd glyph occupies the strip just before it.
    const int rowContentRight = viewportWidth - scrollLane - kRowRightInset;

    // ONE shape for every backdrop on the row — category band, hover and
    // selection. They used to be computed separately and disagreed: the band
    // ran to the panel edge (past the scrollbar handle) with its right-hand
    // corners clipped square while the left pair was rounded (field report
    // 2026-08-19). Root rows stay square and full width by design; they read
    // as chrome continuous with the toolbar.
    const auto bodyRect = [&]() {
      // Width comes from the VIEWPORT, not from the baked-span property: the
      // property is empty until the first icon bake, so the fallback (the
      // indented item rect) painted a narrower pill that visibly jumped wider
      // the first time a repaint ran — reported as backdrops resizing on the
      // first hover (2026-08-19).
      if (isRootRow) {
        // A root row's band spans the panel, but a RowEnd glyph still has to
        // sit inside it rather than under the scrollbar — the band is chrome,
        // the glyph is content.
        return QRect(0, option.rect.y(), viewportWidth, option.rect.height());
      }
      // UNCONDITIONAL, not "only when scrollable": the scroll range is still
      // zero on the first paint, so gating on it made every pill jump
      // narrower the moment the range was computed — which is what the user
      // saw as the width changing on first hover (2026-08-19). A constant
      // lane costs a few pixels when nothing scrolls and never moves.
      const int lane = scrollLane;
      // LEFT EDGE = where the item's own content starts, i.e. just right of
      // the branch cell that holds the fold chevron. Anchoring the pill to a
      // fixed inset instead reached back across that cell and swallowed the
      // chevron (user request 2026-08-19: folding icons belong outside the
      // pill). The indent doubles as the left padding.
      //
      // EVERY non-root row anchors at its own indented position, categories
      // and leaves alike. Leaves used to be pinned to a flat 8px inset
      // while their parent category yielded the branch cell, which put a
      // child's pill one indent LEFT of its parent's — the hierarchy read
      // backwards, so a category and the subcollections under it looked like
      // siblings (user request 2026-08-21: "shell collections/categories
      // should be more to the left, so it's clear the underlying collections
      // are subcollections"). Anchoring both to option.rect.left() restores
      // the nesting: each depth steps one indentation unit right.
      //
      // The flat inset originally existed because anchoring leaves here cost
      // them the width their pill needs to cover a wide logo (field report
      // 2026-08-19: "the pill doesn't cover most of the logos anymore"). That
      // is no longer load-bearing — the logo-coverage guarantee a few lines
      // below now widens the pill to fit its logo whatever the left edge is.
      const int left = option.rect.left();
      // A RowEnd glyph owns the strip just inside the panel edge, so the
      // backdrop — and the label inside it — has to stop before that strip
      // rather than run under it.
      const int rightLimit = viewportWidth - lane - kRowRightInset - edgeGlyphReserve;
      int width = qMax(0, rightLimit - left);
      // SIZE A TEXT ROW TO ITS TEXT. Running every pill out to the panel edge
      // makes a short name like "Sega" wear a backdrop several times its own
      // width, which reads as a filled lane rather than a pill (user request
      // 2026-08-22: "the rounded pills are too wide"). Only in a mode that
      // draws no icon — an icon row still needs the logo-coverage rule below,
      // and its pill is sized from the artwork instead.
      //
      // Clamped to the edge-run width, never beyond it, so a label longer than
      // the panel still stops at the scrollbar lane and is elided (or scrolled)
      // rather than overflowing.
      if (displayMode == TreeIconDisplay::TextOnly) {
        const QString label = index.data(Qt::DisplayRole).toString();
        if (!label.isEmpty()) {
          QFont font = option.font;
          if (const QVariant fnt = index.data(Qt::FontRole); fnt.canConvert<QFont>()) {
            font = fnt.value<QFont>();
          }
          const QFontMetrics fm(font);
          // A HUGGING glyph is inside the pill, so it is part of what the pill
          // has to be wide enough to hold — otherwise sizing to the label alone
          // leaves the glyph hanging off the edge of its own backdrop. A RowEnd
          // glyph is outside it and contributes nothing here; its strip was
          // already taken off rightLimit above.
          width =
              qMin(width, fm.horizontalAdvance(label) + kRowTextPadding * 2 + inlineGlyphAdvance);
          // HEIGHT FROM THE TEXT, not from the row. Rows in this tree are not a
          // uniform height — a collection row is sized for the artwork it could
          // carry even when nothing is drawn, while the synthetic Playlists
          // heading is only as tall as its label. Insetting a fixed amount from
          // each therefore produced pills of visibly different heights, and the
          // collection ones came out nearly as tall as they were wide (user
          // request 2026-08-22: "subcollection pills are too tall", "playlists
          // label background should display identically").
          //
          // Sizing from the font makes every pill in this mode the same height
          // and proportionate to what it contains, whatever its row does.
          // Sized to whichever of the two is taller. A glyph configured larger
          // than the row font would otherwise overhang a pill measured from
          // the text alone.
          const int pillHeight = qMax(fm.height(), glyphSize.height()) + kPillVerticalPadding * 2;
          const int y = option.rect.y() + (option.rect.height() - pillHeight) / 2;
          return QRect(left, y, width, pillHeight);
        }
      }
      // The pill must COVER its logo. Logos are baked to the configured
      // height and keep whatever width that aspect implies, so a wide
      // wordmark could overhang a pill sized purely from the viewport.
      // Widening the pill is the fix — the icon size stays as configured.
      // Skipped in TextOnly: the pixmap is still baked there but never drawn,
      // so honouring it would re-widen the pill we just sized to its text.
      const QPixmap logo = index.data(kRoleBakedPixmap).value<QPixmap>();
      if (!logo.isNull() && displayMode != TreeIconDisplay::TextOnly) {
        const int logoWidth = static_cast<int>(logo.deviceIndependentSize().width());
        width = qMax(width, logoWidth + kRowLogoPadding * 2);
      }
      width = qMin(width, qMax(0, viewportWidth - lane - left));
      return QRect(left, option.rect.y() + kRowVerticalInset, width,
                   qMax(0, option.rect.height() - kRowVerticalInset * 2));
    };
    const auto fillBackdrop = [&](const QColor &colour) {
      painter->save();
      painter->setRenderHint(QPainter::Antialiasing, true);
      painter->setPen(Qt::NoPen);
      painter->setBrush(colour);
      // Only true ROOT rows are square (they read as chrome continuous with
      // the toolbar). Everything else, the Playlists section header included,
      // wears the rounded pill — it was briefly squared on 2026-08-20 on a
      // misreading of "playlist backdrop not rounded", which was a report
      // that it lacked the pill, not a request to remove one.
      if (isRootRow) {
        painter->drawRect(bodyRect());
      } else {
        // QRectF inset by half a pixel: an integer QRect puts the antialiased
        // edge exactly on the pixel boundary, so the curve is dithered across
        // two rows of pixels and reads as jagged (user request 2026-08-20:
        // "the rounding appears jagged"). Landing the geometric edge on a
        // pixel CENTRE lets the same antialiasing resolve a clean arc.
        const QRectF pill = QRectF(bodyRect()).adjusted(0.5, 0.5, -0.5, -0.5);
        // A text-sized pill needs the smaller radius — see kTextPillRadius.
        const int radius =
            displayMode == TreeIconDisplay::TextOnly ? kTextPillRadius : kSelectionRadius;
        painter->drawRoundedRect(pill, radius, radius);
      }
      painter->restore();
    };

    if (const QColor rootFill = index.data(kRoleRootFill).value<QColor>(); rootFill.isValid()) {
      fillBackdrop(rootFill);
    }
    if (const QColor band = index.data(kRoleCategoryBand).value<QColor>(); band.isValid()) {
      fillBackdrop(band);
    }

    const QColor rowTint =
        option.widget ? option.widget->property("kartendSelectionColor").value<QColor>() : QColor();
    if ((option.state & QStyle::State_Selected) && rowTint.isValid()) {
      fillBackdrop(rowTint);
    } else if ((option.state & QStyle::State_MouseOver) && rowTint.isValid()) {
      QColor hover = rowTint;
      hover.setAlpha(90); // a hint of the selection, not the selection itself
      fillBackdrop(hover);
    }

    // The style would otherwise repaint its own square, full-row highlight
    // over the shapes above — applied to BOTH paint paths, since a text-only
    // row takes the early return below.
    QStyleOptionViewItem base = option;
    // An item's ForegroundRole loses to the panel stylesheet's `color:`
    // rules, so the tinted root label kept rendering in the sheet's colour
    // (field report 2026-08-19: "still not seeing Games text tinted"). Push
    // it into the option's palette, which is what the item view actually
    // reads when it draws the label.
    if (const QVariant fg = index.data(Qt::ForegroundRole); fg.canConvert<QBrush>()) {
      if (const QColor ink = fg.value<QBrush>().color(); ink.isValid()) {
        base.palette.setColor(QPalette::Text, ink);
        base.palette.setColor(QPalette::HighlightedText, ink);
        base.palette.setColor(QPalette::WindowText, ink);
      }
    }
    if (base.state & QStyle::State_Selected) {
      base.palette.setColor(QPalette::Text, base.palette.color(QPalette::HighlightedText));
    }
    base.state &= ~(QStyle::State_Selected | QStyle::State_MouseOver);

    const QPixmap pm = index.data(kRoleBakedPixmap).value<QPixmap>();
    // Kartend-j1mtg. The mode rides on the view as a property, the same way
    // kartendShowLines does — a delegate has no route to the collection config.
    // Read once, near the top, because the pill geometry needs it too.
    const TreeIconDisplay display = displayMode;
    // TextOnly takes the no-pixmap path even though a pixmap exists. The art
    // is still BAKED — switching back to a showing mode must not need a
    // re-bake — it is simply not drawn.
    if (pm.isNull() || display == TreeIconDisplay::TextOnly) {
      // Text-only row with an explicit colour: draw the label HERE rather
      // than handing it to the style. QStyleSheetStyle resolves `color:`
      // from the panel stylesheet and ignores both the item's ForegroundRole
      // and the option's palette, so every attempt to tint this label
      // through those channels was silently discarded (four rounds of it,
      // 2026-08-19). Painting it directly is the only route the sheet
      // cannot override.
      const QVariant fg = index.data(Qt::ForegroundRole);
      const QString label = index.data(Qt::DisplayRole).toString();
      QColor ink = fg.canConvert<QBrush>() ? fg.value<QBrush>().color() : QColor();

      // MARQUEE OFFSET, computed for BOTH draw paths below. It lived inside the
      // explicit-colour branch at first, which meant it never ran: only tinted
      // rows take that path, and an ordinary row falls through to the style —
      // which elides. The symptom was a row still reading "Open Source Ga…"
      // and never moving, with the ellipsis itself the clue, since the manual
      // drawText path does not elide at all.
      QFont labelFont = option.font;
      if (const QVariant fnt = index.data(Qt::FontRole); fnt.canConvert<QFont>()) {
        labelFont = fnt.value<QFont>();
      }
      const int labelWidth = QFontMetrics(labelFont).horizontalAdvance(label);
      // Measured against the space the text ACTUALLY gets — the pill minus its
      // padding — not the row rect. The two differ now that the pill is sized
      // to its text and inset from the row, and using the row's width would let
      // a label overflow its own backdrop while the marquee reported it as
      // fitting.
      // The glyph's advance comes off the label's budget: it sits inside the
      // pill, so the text has that much less room and a name can be clipped by
      // the glyph as readily as by the panel edge.
      // inlineGlyphAdvance, NOT glyphAdvance — it has to be the same term
      // bodyRect sized the pill with. A row-end glyph sits OUTSIDE the pill
      // and costs the label nothing here, so subtracting its width made every
      // row report exactly that much overflow and scroll, however short its
      // name (field report 2026-08-23: "now all titles scroll, even if they
      // fit").
      const int overflow =
          labelWidth - qMax(0, bodyRect().width() - kRowTextPadding * 2 - inlineGlyphAdvance);

      // TWO independent opt-ins. "always" moves every clipped row; "on hover"
      // moves only the row under the pointer and is the default, because
      // pointing at a row is a deliberate "what is this?" — the movement is
      // asked for, is confined to one row, and stops when the pointer leaves.
      //
      // option.state, not base.state: base has MouseOver cleared a few lines
      // above so the style cannot paint its own square hover highlight over
      // our pill.
      const bool alwaysScroll =
          option.widget && option.widget->property("kartendScrollClippedLabels").toBool();
      const bool hoverScroll =
          option.widget && option.widget->property("kartendScrollClippedLabelsOnHover").toBool() &&
          (option.state & QStyle::State_MouseOver);
      const bool scrolling = !label.isEmpty() && overflow > 0 && (alwaysScroll || hoverScroll);
      int shift = 0;
      if (scrolling) {
        // Report in so the controller keeps the animation timer alive; it idles
        // itself out when no row answers. const_cast because paint() only ever
        // sees a const widget, and the delegate is the only thing that knows a
        // row's true available width.
        if (auto *w = const_cast<QWidget *>(option.widget)) {
          w->setProperty("kartendSawClippedLabel", true);
        }
        const int phase = option.widget->property("kartendLabelScrollPhase").toInt();
        // DWELL AT BOTH ENDS rather than a constant crawl or a wrap-around. A
        // name that slides continuously is hardest to read at exactly the
        // moment you want to read it; the start and the end are the two states
        // worth holding. One pixel per tick at 20fps with ~1.5s pauses.
        constexpr int kDwellTicks = 30;
        const int travel = overflow + 1;
        const int cycle = (travel + kDwellTicks) * 2;
        const int t = phase % cycle;
        if (t < travel) {
          shift = t; // travelling out
        } else if (t < travel + kDwellTicks) {
          shift = overflow; // holding at the end
        } else if (t < travel * 2 + kDwellTicks) {
          shift = overflow - (t - travel - kDwellTicks); // travelling back
        }
        shift = std::clamp(shift, 0, overflow);
      }

      // ONE DRAW PATH for every text-only row, tinted or not. It used to fork:
      // rows with an explicit ForegroundRole were painted here and everything
      // else was handed to the style. That fork is what made the label sit hard
      // against the left edge of its pill — the style draws text at the rect's
      // own left edge, while the pill is sized to the text plus padding on BOTH
      // sides, so all the slack ended up on the right (user request 2026-08-22:
      // "the text should be centered").
      //
      // Positioning the text therefore has to be ours, which means the colour
      // has to be ours too. An explicit ForegroundRole still wins where one is
      // set — that is the tint the panel stylesheet would otherwise override,
      // and the reason this manual path existed at all — and everything else
      // takes the palette already resolved for this row.
      if (!label.isEmpty()) {
        const QRect pill = bodyRect();
        painter->save();
        painter->setFont(labelFont);
        if (ink.isValid()) {
          painter->setPen(ink);
        } else {
          painter->setPen((option.state & QStyle::State_Selected)
                              ? option.palette.color(QPalette::HighlightedText)
                              : option.palette.color(QPalette::Text));
        }
        int align = Qt::AlignVCenter | Qt::AlignLeft;
        if (const QVariant a = index.data(Qt::TextAlignmentRole); a.canConvert<int>()) {
          align = a.toInt();
        }
        // Inset by the same padding the pill was sized with, so the label sits
        // centred between its edges rather than flush against one of them.
        // A HUGGING glyph then takes its advance off whichever side it is on,
        // which is what puts the name beside it instead of underneath it.
        const bool glyphLeads = glyphPlacement == SystemIconPlacement::BeforeName;
        QRect textRect = pill.adjusted(kRowTextPadding + (glyphLeads ? inlineGlyphAdvance : 0), 0,
                                       -kRowTextPadding - (glyphLeads ? 0 : inlineGlyphAdvance), 0);
        int glyphX = glyphLeads ? pill.left() + kRowTextPadding
                                : pill.right() + 1 - kRowTextPadding - glyphSize.width();

        // A CENTRE-aligned row needs the glyph and the name centred AS A PAIR.
        // Anchoring the glyph to a pill edge works only where the pill is sized
        // to its contents; a ROOT row's "pill" is the full-width chrome band
        // (see bodyRect), so the glyph would strand itself against the panel
        // edge while the name floated in the middle of the row. Measuring the
        // block and centring that puts them together at any width, and
        // collapses to the old behaviour when there is no glyph.
        if ((align & Qt::AlignHCenter) && inlineGlyphAdvance > 0) {
          const int textWidth = qMin(labelWidth, textRect.width());
          const int blockLeft = pill.left() + (pill.width() - (inlineGlyphAdvance + textWidth)) / 2;
          glyphX = glyphLeads ? blockLeft : blockLeft + textWidth + kSystemGlyphGap;
          textRect = QRect(glyphLeads ? blockLeft + inlineGlyphAdvance : blockLeft, pill.top(),
                           textWidth, pill.height());
          // The block is already positioned, so the text draws from its own
          // left edge — centring it a second time inside its own rect would
          // be a no-op at best and re-offset it at worst.
          align = (align & ~Qt::AlignHorizontal_Mask) | Qt::AlignLeft;
        }

        // RowEnd ignores all of the above: it pins to the panel's inner edge so
        // a column of glyphs lines up regardless of how long the names are.
        if (glyphAtRowEnd) {
          glyphX = rowContentRight - glyphSize.width();
          // bodyRect already reserved this strip out of a normal row's pill,
          // but a ROOT row's pill is the full-width chrome band and took no
          // such reservation — so a long centred heading would otherwise be
          // drawn straight through the glyph. Clamping the text rect covers
          // both cases in one place.
          textRect.setRight(qMin(textRect.right(), rowContentRight - glyphAdvance));
        }

        if (!systemGlyph.isNull()) {
          // Vertically centred on the PILL, not the row: in this mode the pill
          // is sized to its contents and is shorter than the row, so centring
          // on the row would float the glyph off its own backdrop. A RowEnd
          // glyph sits outside the pill horizontally but still lines up with
          // it vertically, which is what keeps it reading as part of the row.
          const int glyphY = pill.top() + (pill.height() - glyphSize.height()) / 2;
          painter->drawPixmap(QRect(QPoint(glyphX, glyphY), glyphSize), systemGlyph);
        }
        if (scrolling) {
          // Clip to the pill so a travelling label cannot spill past its own
          // backdrop and across the panel — and, for a LEADING glyph, stop the
          // label at that glyph's right edge as well. The marquee slides the
          // text LEFT, straight into the glyph's lane; without this it would
          // ride over the top of it, since the glyph is painted first. A
          // trailing or row-end glyph needs no such guard: the text travels
          // away from both.
          QRect lane = pill;
          if (glyphLeads && inlineGlyphAdvance > 0) {
            lane.setLeft(glyphX + inlineGlyphAdvance);
          }
          painter->setClipRect(lane);
        }
        painter->drawText(textRect.adjusted(-shift, 0, 0, 0), align, label);
        painter->restore();
        return;
      }
      QStyledItemDelegate::paint(painter, base, index);
      return;
    }
    QStyleOptionViewItem opt = base;
    initStyleOption(&opt, index);
    opt.icon = QIcon();
    opt.text.clear();
    opt.state &= ~(QStyle::State_Selected | QStyle::State_MouseOver);
    const QWidget *widget = option.widget;
    QStyle *style = widget ? widget->style() : QApplication::style();
    // Full-width row band (field report 2026-08-18: the hover/selection
    // band stopped short of the panel's left edge, leaving the indent
    // column unpainted). Qt hands the delegate a rect that starts AFTER
    // the indent, so stretch it back to the viewport edge. Only when the
    // connector lines are hidden — with lines on, that column belongs to
    // them and painting over it would erase them.
    const bool linesHidden = widget && !widget->property("kartendShowLines").toBool();
    if (linesHidden) {
      opt.rect.setLeft(0);
    }
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);
    if (linesHidden && index.model() && index.model()->hasChildren(index)) {
      // The band just covered the chevron the view painted before us, so
      // re-draw it on top. Same geometry TreeBranchView uses.
      drawChevron(painter, option, index);
    }

    const qreal pmDpr = pm.devicePixelRatio() > 0 ? pm.devicePixelRatio() : 1.0;
    const int w = qMax(1, qRound(pm.width() / pmDpr));
    const int h = qMax(1, qRound(pm.height() / pmDpr));
    // The single column stretches to the viewport, so the item rect's right
    // edge IS the panel's inner width.
    const int panelRight = option.rect.right() + 1;
    // Centred on the PILL, not the panel (user request 2026-08-19). For a
    // root row the pill spans the whole panel, so this keeps the original
    // panel-centred result there; for an indented row it centres inside the
    // backdrop the user actually sees, instead of drifting toward the panel
    // centre and out of the pill.
    const QRect pill = bodyRect();
    int x = pill.left() + (pill.width() - w) / 2;
    if (w > panelRight - 2 * kPanelChrome) {
      // OVERSIZED: the logo is wider than the panel can show. Since icons are
      // baked to the configured size and no longer shrink to fit (user,
      // 2026-08-20: "the icon size should remain fixed"), no x keeps this one
      // whole — so centre it on the PANEL and let it clip evenly on both
      // sides. The clamps below would instead pin it to the left inset and
      // clip the entire overflow off the right, which reads as a logo that
      // has slid sideways rather than one that is simply too big.
      x = (panelRight - w) / 2;
    } else {
      x = qMax(x, pill.left());
      x = qMin(x, pill.right() - w + 1);
      // Staying fully visible WINS over pill-centring. A logo can be wider
      // than an indented row's pill, and centring it there pushed its right
      // edge onto the viewport edge — the clipping that
      // icons_onIndentedRows_renderCenteredAtConfiguredSize exists to catch,
      // and that took several rounds to eliminate on 2026-08-17. Where the
      // two rules disagree the icon stays fully visible and merely off-centre
      // in its pill.
      x = qMin(x, panelRight - kPanelChrome - w);
      x = qMax(x, kPanelChrome);
    }
    if (display == TreeIconDisplay::IconAndText) {
      // Small icon at the pill's left edge, name beside it (user request
      // 2026-08-22: "show the text, but make the icon small and next to the
      // text"). Deliberately NOT the centred full-size draw below: that one
      // fills the row, which is what left no room for a label in the first
      // place.
      //
      // Height is capped to the row so a tall poster cannot stretch it, and
      // the width follows the SOURCE aspect ratio rather than the baked box —
      // baking pads compact marks toward a common box (Kartend-ob1c9), and
      // reusing that padded width here would leave a wide gap between a round
      // logo and its name.
      const int rowH = option.rect.height();
      const int iconH = qMax(1, qMin(h, rowH - 2 * kIconTextVMargin));
      const int iconW = qMax(1, qRound(static_cast<qreal>(w) * iconH / qMax(1, h)));
      const int iconX = qMax(pill.left() + kPanelChrome, kPanelChrome);
      const int iconY = option.rect.top() + (rowH - iconH) / 2;
      painter->drawPixmap(QRect(iconX, iconY, iconW, iconH), pm);

      const QString label = index.data(Qt::DisplayRole).toString();
      if (!label.isEmpty()) {
        const int textLeft = iconX + iconW + kIconTextGap;
        // The glyph keeps its place relative to the NAME rather than to the
        // row, so its meaning is the same in every mode: it accompanies the
        // title, and the row artwork — a different mark from a different
        // source — stays where it was. So BeforeName reads [artwork][glyph]
        // [name] and AfterName reads [artwork][name][glyph].
        const bool glyphLeads = glyphPlacement == SystemIconPlacement::BeforeName;
        // Where the label has to stop — whichever of three constraints binds
        // first: the pill's inner edge, the row's content edge less any strip
        // a RowEnd glyph has reserved, and a trailing glyph's own advance.
        //
        // The middle one is not redundant. A non-root pill already stops short
        // of the reserved strip (bodyRect took it off rightLimit), but a ROOT
        // row's pill is the full-width chrome band, so without this the label
        // would run under a row-end glyph whenever the glyph is wider than
        // kPanelChrome — which it is for most of the size range.
        const int textRight =
            qMin(pill.right() - kPanelChrome, rowContentRight - edgeGlyphReserve) -
            (glyphLeads ? 0 : inlineGlyphAdvance);
        QRect textRect(textLeft + (glyphLeads ? inlineGlyphAdvance : 0), option.rect.top(),
                       textRight - (textLeft + (glyphLeads ? inlineGlyphAdvance : 0)), rowH);
        if (!systemGlyph.isNull()) {
          const int glyphY = option.rect.top() + (rowH - glyphSize.height()) / 2;
          // Three placements, spelled out rather than nested into one
          // expression — the trailing case is the fallthrough.
          int glyphX = textRight + kSystemGlyphGap;
          if (glyphAtRowEnd) {
            glyphX = rowContentRight - glyphSize.width();
          } else if (glyphLeads) {
            glyphX = textLeft;
          }
          painter->drawPixmap(QRect(QPoint(glyphX, glyphY), glyphSize), systemGlyph);
        }
        if (textRect.width() > 0) {
          painter->save();
          // Same ink resolution as the text-only path: the panel stylesheet's
          // `color:` wins over ForegroundRole and the option palette, so the
          // label has to be painted directly to be tinted at all.
          const QVariant fg = index.data(Qt::ForegroundRole);
          if (const QColor ink = fg.canConvert<QBrush>() ? fg.value<QBrush>().color() : QColor();
              ink.isValid()) {
            painter->setPen(ink);
          }
          if (const QVariant fnt = index.data(Qt::FontRole); fnt.canConvert<QFont>()) {
            painter->setFont(fnt.value<QFont>());
          } else {
            painter->setFont(option.font);
          }
          const QString elided =
              painter->fontMetrics().elidedText(label, Qt::ElideRight, textRect.width());
          painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, elided);
          painter->restore();
        }
      }
      return;
    }
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
class TreeBranchView : public QTreeWidget {
public:
  using QTreeWidget::QTreeWidget;

protected:
  void drawBranches(QPainter *painter, const QRect &rect, const QModelIndex &index) const override {
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

/// Recolour a SILHOUETTE glyph to @p ink, and leave anything else alone
/// (Kartend-1kkk2).
///
/// RetroArch's icon packs split cleanly in two. `monochrome` and `automatic`
/// are white-on-transparent: drawn as-is they are invisible on a light theme,
/// which is not a subtlety — it is the whole glyph gone. The colour packs
/// (`systematic`, `retrosystem`, `dot-art`, …) are picked BECAUSE they are
/// coloured, so flattening them to one ink would throw away the reason for
/// choosing them.
///
/// Rather than ask the user which kind they picked, look: a silhouette is art
/// whose opaque pixels are all essentially one colour. That test is a property
/// of the art itself, so it keeps working for packs nobody has classified.
QPixmap tintSilhouetteGlyph(const QPixmap &pm, const QColor &ink) {
  if (pm.isNull() || !ink.isValid()) return pm;
  const QImage img = pm.toImage().convertToFormat(QImage::Format_ARGB32);
  QRgb reference = 0;
  bool haveReference = false;
  qint64 opaque = 0;
  qint64 offReference = 0;
  for (int y = 0; y < img.height(); ++y) {
    const auto *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
    for (int x = 0; x < img.width(); ++x) {
      if (qAlpha(line[x]) <= 128) continue;
      ++opaque;
      if (!haveReference) {
        reference = line[x];
        haveReference = true;
        continue;
      }
      // Chebyshev distance in RGB — cheap, and a silhouette's antialiased
      // interior varies by a hair at most, so the tolerance can be tight
      // without counting edge pixels against it.
      if (qAbs(qRed(line[x]) - qRed(reference)) > 12 ||
          qAbs(qGreen(line[x]) - qGreen(reference)) > 12 ||
          qAbs(qBlue(line[x]) - qBlue(reference)) > 12) {
        ++offReference;
      }
    }
  }
  // A couple of percent of stragglers still counts as one ink; a genuinely
  // coloured mark blows straight past this.
  if (opaque == 0 || offReference * 100 > opaque * 3) return pm;
  QPixmap tinted(pm.size());
  tinted.setDevicePixelRatio(pm.devicePixelRatio());
  tinted.fill(Qt::transparent);
  {
    QPainter painter(&tinted);
    painter.drawPixmap(0, 0, pm);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(tinted.rect(), ink);
  }
  return tinted;
}

/// Flatten @p pm to @p ink while KEEPING its internal detail — the same
/// luminance-band mapping the row-artwork bake uses for its monochrome styles
/// (see the TreeIconStyle switch in refreshIcons), lifted out so the system
/// glyph's fallback art can share it.
///
/// A flat SourceIn fill is the obvious implementation and the wrong one: it
/// turns a logo into a solid blob, losing the counters and inner shapes that
/// make it recognisable (field report 2026-08-17). Mapping each pixel's
/// luminance into a band anchored at the ink keeps them. Alpha is untouched.
QPixmap flattenToInk(const QPixmap &pm, const QColor &ink, bool keepHue) {
  if (pm.isNull() || !ink.isValid()) return pm;
  QImage img = pm.toImage().convertToFormat(QImage::Format_ARGB32);
  const bool lightInk = qGray(ink.rgb()) >= 128;
  // Tinted keeps the ink's hue and varies lightness instead of collapsing to
  // grey, so a tinted glyph reads as the accent rather than as a washed logo.
  const float tintHue = ink.hslHueF();
  const float tintSat = ink.hslSaturationF();
  for (int y = 0; y < img.height(); ++y) {
    auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
    for (int x = 0; x < img.width(); ++x) {
      const int a = qAlpha(line[x]);
      if (a == 0) continue;
      const int g = qGray(line[x]);
      if (keepHue) {
        const QColor c = QColor::fromHslF(tintHue < 0 ? 0 : tintHue, tintSat,
                                          0.30F + 0.55F * (static_cast<float>(g) / 255.0F));
        line[x] = qRgba(c.red(), c.green(), c.blue(), a);
      } else {
        const int v = lightInk ? 140 + g * 115 / 255 : g * 115 / 255;
        line[x] = qRgba(v, v, v, a);
      }
    }
  }
  return QPixmap::fromImage(img);
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
  // A zero-width sentinel, never shown: the drag is handled as a hit zone
  // on the tree's inner edge instead (field report 2026-08-18 — a real
  // widget always paints SOMETHING between the sidebar and the toolbar,
  // whichever colour it takes, and that band is the "gap"). This mirrors
  // the details pane, whose grip is likewise an invisible zone.

  // No "Collections" header label (user request 2026-08-17: "we all know
  // they are collections") — the tree starts at the panel's top edge.
  m_tree = new TreeBranchView(content);
  // Right-click acts on the row under the cursor (Kartend-1kkk2) — the point
  // is to fix a row without navigating to it first.
  m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_tree, &QWidget::customContextMenuRequested, this,
          &CollectionTreeController::showRowContextMenu);
  m_tree->setProperty("kartendShowLines", false);
  m_tree->setProperty("kartendIconDisplay",
                      static_cast<int>(CollectionTreeSettings{}.treeIconDisplay));
  m_tree->setProperty("kartendScrollClippedLabels",
                      CollectionTreeSettings{}.treeScrollClippedLabels);
  m_tree->setProperty("kartendScrollClippedLabelsOnHover",
                      CollectionTreeSettings{}.treeScrollClippedLabelsOnHover);
  m_tree->setProperty("kartendLabelScrollPhase", 0);
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
    // Captured here, while the tree is unambiguously alive, because
    // eventFilter cannot safely ask for it later (Kartend-6a2ci — see the
    // m_treeViewport declaration).
    m_treeViewport = m_tree->viewport();
    m_tree->viewport()->installEventFilter(this);
    m_tree->viewport()->setMouseTracking(true);
  }
  m_tree->setMouseTracking(true);
  m_tree->installEventFilter(this);
  if (m_ctx && m_ctx->ui.itemsTopBar) {
    m_ctx->ui.itemsTopBar->installEventFilter(this); // height changes re-bake
  }
  m_tree->setItemDelegateForColumn(0, new TreeIconDelegate(m_tree));
  m_stickyRootHeader = new StickyRootHeader(m_tree);
  if (QScrollBar *vbar = m_tree->verticalScrollBar()) {
    connect(vbar, &QScrollBar::valueChanged, this,
            [this]() { syncStickyRoot(m_stickyRootHeader); });
  }
  layout->addWidget(m_tree, /*stretch=*/1);

  // HOVER RESTARTS THE MARQUEE. The timer idles itself out when nothing is
  // scrolling, which with hover-scrolling is the normal resting state — so
  // something has to wake it when the pointer arrives. entered() is emitted
  // because mouse tracking is on for both the view and its viewport.
  //
  // Landing on a DIFFERENT row also resets the shared phase, so its name
  // starts from the beginning rather than joining the cycle wherever it
  // happened to be.
  connect(m_tree, &QTreeWidget::entered, this, [this](const QModelIndex &index) {
    const void *item = index.internalPointer();
    if (item != m_lastHoveredItem) {
      m_lastHoveredItem = item;
      m_labelScrollPhase = 0;
      if (m_tree) {
        m_tree->setProperty("kartendLabelScrollPhase", 0);
      }
    }
    updateLabelScrollTimer();
  });
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
  insertPanelAt(CollectionTreeSettings{}.treePosition, CollectionTreeSettings{}.treeJustification,
                CollectionTreeSettings{}.treeMode);
  applyStateForCollection(activeCollectionIndex());
}

void CollectionTreeController::insertPanelAt(DetailsPanePosition position,
                                             SidebarJustification justification,
                                             DetailsPaneMode mode) {
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
  if (m_panelInserted && position == m_insertedPosition && effective == m_insertedJustification &&
      mode == m_insertedMode) {
    return;
  }
  // setParent() below hides the widget, and a mode switch must not silently
  // close a panel the collection wants open.
  const bool wasVisible = m_panel->isVisible();
  if (m_panelInserted) {
    if (m_mainLayout->indexOf(m_panel) != -1) {
      m_mainLayout->removeWidget(m_panel);
    }
    if (m_fullHeightLayout && m_fullHeightLayout->indexOf(m_panel) != -1) {
      m_fullHeightLayout->removeWidget(m_panel);
    }
  }

  if (mode == DetailsPaneMode::Overlay) {
    // FLOAT: the panel leaves the layout entirely and is positioned over the
    // host by hand. This is the whole point of the mode — a docked panel
    // takes width from the row, which moves the items viewport's ORIGIN, and
    // a grid clipped to a viewport that itself moved cannot be held still
    // from the grid side (user request 2026-08-20: the nav sidebar should
    // overlap "without moving the grid at all").
    QWidget *host =
        (fullHeight && m_fullHeightLayout) ? m_fullHeightLayout->parentWidget() : m_panelParent;
    if (host) {
      if (m_overlayHost && m_overlayHost != host) {
        m_overlayHost->removeEventFilter(this);
      }
      if (m_panel->parentWidget() != host) {
        m_panel->setParent(host);
      }
      m_overlayHost = host;
      // The host's resizes are what the float has to track — nothing lays
      // the panel out any more, so a window resize would otherwise leave it
      // spanning the old height.
      host->installEventFilter(this);
      positionOverlayPanel();
    }
  } else {
    if (m_overlayHost) {
      m_overlayHost->removeEventFilter(this);
      m_overlayHost = nullptr;
    }
    QHBoxLayout *target = fullHeight ? m_fullHeightLayout : m_mainLayout;
    // The tree claims the row's extremes, so it always sits OUTSIDE a
    // full-height details pane (which inserts adjacent to the toolbar column).
    // The fold marker docks at the very same extreme, OUTSIDE the panel, so
    // when the panel hides the marker holds its edge. Its arrow points into
    // the view — the direction the panel would unfold.
    if (position == DetailsPanePosition::Right) {
      target->addWidget(m_panel);
    } else {
      target->insertWidget(0, m_panel);
    }
  }
  m_insertedPosition = position;
  m_insertedJustification = effective;
  m_insertedMode = mode;
  m_panelInserted = true;
  if (wasVisible) {
    m_panel->show();
    if (mode == DetailsPaneMode::Overlay) {
      m_panel->raise();
    }
  }
}

void CollectionTreeController::positionOverlayPanel() {
  if (!m_panel || !m_overlayHost || m_insertedMode != DetailsPaneMode::Overlay) {
    return;
  }
  // Width comes from the panel itself (the grip drag sets it); the height is
  // always the host's, so the float spans the same extent the dock would have.
  const int width = qBound(CollectionTreeSettings::kMinWidth, m_panel->width(),
                           CollectionTreeSettings::kMaxWidth);
  const int x =
      (m_insertedPosition == DetailsPanePosition::Right) ? m_overlayHost->width() - width : 0;
  m_panel->setGeometry(x, 0, width, m_overlayHost->height());
  m_panel->raise();
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
  // REVERSE-SEED, for the same reason the child loop below reverse-appends:
  // `work` is a stack drained with takeLast(), so appending the roots in model
  // order made the LAST root the first item created and the sidebar listed
  // root collections back to front. Nobody caught it because a library with a
  // single root looks identical either way — but it also meant
  // indexOfTopLevelItem(item) == 0, which decides which row wears the toolbar
  // chrome, picked the last root rather than the first.
  for (auto it = model.collectionRoots.crbegin(); it != model.collectionRoots.crend(); ++it) {
    work.append({nullptr, &*it, -1});
  }
  // The chrome row (topmost root) reads as a header for the panel, so its
  // children are the top level of the list as far as the eye is concerned.
  // Promoting them to top-level items is what lets them align with the
  // Playlists heading instead of sitting one indentation unit right of it
  // (user request 2026-08-22: "unnecessary indent"). Only that root's direct
  // children are promoted; deeper rows keep nesting under them, and any other
  // root keeps its own subtree, because those rows are ordinary list entries
  // rather than a header.
  QTreeWidgetItem *chromeRoot = nullptr;
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
    // A ROOT is a node the model seeded with no parent collection — recorded
    // explicitly because a promoted child is also a top-level ITEM without
    // being a root, and root styling (centred, tinted, square, edge-to-edge)
    // must not follow it up there.
    const bool isRoot = current.parentCollectionIndex == -1;
    item->setData(0, kRoleIsRootCollection, isRoot);
    // The first root created is the topmost row, i.e. the one wearing the
    // toolbar chrome. Captured here rather than re-derived, so the promotion
    // below cannot disagree with the styling decision made further down.
    if (isRoot && chromeRoot == nullptr) {
      chromeRoot = item;
    }
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
      const QString parentName =
          (current.parentCollectionIndex >= 0 && current.parentCollectionIndex < collections.size())
              ? collections.at(current.parentCollectionIndex).name
              : QString();
      expansionKey = QStringLiteral("::name::") + parentName + QLatin1Char('/') + cfg.name;
    }
    item->setData(0, kRoleExpansionKey, expansionKey);
    item->setData(0, kRoleIsCategory, !current.node->children.isEmpty());
    // Expansion applied in the single post-pass below, once children exist.
    // Reverse-append so takeLast() preserves the model's child order.
    //
    // nullptr parent for the chrome row's children promotes them to top level.
    // Order still comes out right: they sit on top of the stack, so they and
    // their subtrees are created before any later root is popped, landing
    // immediately after the header row exactly where they were nested before.
    QTreeWidgetItem *childParent = (item == chromeRoot) ? nullptr : item;
    for (auto it = current.node->children.crbegin(); it != current.node->children.crend(); ++it) {
      work.append({childParent, &*it, index});
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

void CollectionTreeController::applyPanelWidth(int width, DetailsPanePosition /*position*/) {
  if (!m_panel) {
    return;
  }
  // Width only — the drag zone lives on the tree's inner edge now, so
  // there is no grip widget to re-seat when the dock side changes.
  m_panel->setFixedWidth(
      std::clamp(width, CollectionTreeSettings::kMinWidth, CollectionTreeSettings::kMaxWidth));
  // A floating panel is anchored by hand, and a Right-docked float anchors
  // its RIGHT edge — growing the width must move x, not just the extent.
  // No-op in Expand mode.
  positionOverlayPanel();
}

bool CollectionTreeController::eventFilter(QObject *watched, QEvent *event) {
  // A floating panel is laid out by nobody, so it has to follow its host by
  // hand — otherwise a window resize leaves it spanning the old height.
  if (m_overlayHost && watched == m_overlayHost && event->type() == QEvent::Resize) {
    positionOverlayPanel();
    return false;
  }
  if (m_ctx && watched == m_ctx->ui.itemsTopBar && event->type() == QEvent::Resize) {
    // Top-level rows track the toolbar's height; re-bake when it changes.
    QTimer::singleShot(0, this, [this]() { refreshIcons(); });
    return false;
  }
  // Viewport RESIZE only. Deliberately scoped to Resize and deliberately
  // NOT an early return for everything else: a QTreeWidget delivers mouse
  // events to its VIEWPORT, never to the tree itself, so swallowing every
  // viewport event here left the inner-edge drag zone below unreachable and
  // the panel width impossible to change by dragging (field report
  // 2026-08-19, "i am unable to edit nav sidebar width").
  if (m_treeViewport && watched == m_treeViewport && event->type() == QEvent::Resize) {
    if (m_treeViewport->width() != m_bakedViewportWidth && m_bakedViewportWidth != 0) {
      // Deferred: icon swaps inside a resize re-enter layout. The width
      // check keeps the scrollbar-toggle feedback loop convergent.
      QTimer::singleShot(0, this, [this]() {
        if (m_treeViewport && m_treeViewport->width() != m_bakedViewportWidth) {
          refreshIcons();
        }
      });
    }
    return false;
  }
  const bool fromTree =
      (m_tree && watched == m_tree) || (m_treeViewport && watched == m_treeViewport);
  if (!fromTree || !m_panel) {
    return QObject::eventFilter(watched, event);
  }
  // Inner edge = the one facing the content view: the panel's right when
  // docked Left, its left when docked Right.
  //
  // Measured against the widget that DELIVERED the event, not always the
  // tree: the viewport is inset from the tree by its frame and by the
  // scrollbar lane, so using the tree's width against a viewport-relative
  // position puts the zone in the wrong place by that inset.
  auto *source = qobject_cast<QWidget *>(watched);
  const int sourceWidth = source ? source->width() : 0;
  const auto inGripZone = [this, sourceWidth](const QPoint &pos) {
    const int zone = UIConstants::DetailsPane::RESIZE_GRIP_PX;
    return m_insertedPosition == DetailsPanePosition::Right ? pos.x() <= zone
                                                            : pos.x() >= sourceWidth - zone;
  };
  if (event->type() == QEvent::MouseMove && !m_resizingPanel) {
    auto *move = static_cast<QMouseEvent *>(event);
    // On the SOURCE widget: the viewport carries its own cursor, so setting
    // the tree's would be overridden by it and the split-cursor hint would
    // never appear where the pointer actually is.
    if (source) {
      source->setCursor(inGripZone(move->pos()) ? Qt::SplitHCursor : Qt::ArrowCursor);
    }
    return false; // never swallow plain hover
  }
  if (event->type() == QEvent::MouseButtonPress) {
    auto *press = static_cast<QMouseEvent *>(event);
    if (!inGripZone(press->pos())) {
      return false; // a normal click on a row
    }
    m_resizingPanel = true;
  } else if (!m_resizingPanel &&
             (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonRelease)) {
    return false;
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
    // Persist to EVERY collection, not just the active one (field report
    // 2026-08-18: "clicking a nav bar entry resized the navigation
    // sidebar"). The width is a property of the panel as the user sees
    // it; storing it per collection meant switching to one that still had
    // the default width visibly resized the sidebar mid-click.
    if (m_ctx && m_ctx->collection.collections) {
      auto *collections = const_cast<QList<CollectionConfig> *>(m_ctx->collection.collections);
      const int settled = m_panel->width();
      bool changed = false;
      for (CollectionConfig &cfg : *collections) {
        if (cfg.collectionTree.treeWidth != settled) {
          cfg.collectionTree.treeWidth = settled;
          changed = true;
        }
      }
      if (changed && m_persistCollections) {
        m_persistCollections();
      }
    }
    m_resizingPanel = false;
    // The icon width cap derives from the panel width — re-bake at the
    // settled size (not per move event; decode cost belongs on release).
    refreshIcons();
    // A narrower panel can newly clip a name the marquee timer had already
    // idled itself out over, so give it a chance to restart. Cheap: it stops
    // itself again within a few frames if nothing actually overflows.
    updateLabelScrollTimer();
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
  // NOTE: the tree indentation and the per-row width budget it fed are gone.
  // Icons are sized purely from the configured height now (see refreshIcons),
  // so nothing here depends on how wide the panel happens to be.
  const int viewportWidth = m_tree->viewport() && m_tree->viewport()->width() > 0
                                ? m_tree->viewport()->width()
                                : m_bakedPanelWidth - 29;
  m_bakedViewportWidth = viewportWidth;
  // The delegate fills root rows edge to edge; it needs the row's full span,
  // which is the viewport width (already excluding the scrollbar gutter).
  // FULL viewport width — no gutter subtracted. Stopping short of the
  // scrollbar lane left an unfilled strip at the row's right end, which
  // reads as a gap (field report 2026-08-19). A row fill is decoration; the
  // handle floating over its end is normal for an overlay scrollbar. The
  // lane matters for ITEM CONTENT (grid art), not for a background.
  m_tree->setProperty("kartendRowSpanWidth", viewportWidth);
  syncStickyRoot(m_stickyRootHeader);
  struct BakedIcon {
    QPixmap pixmap; // painted by TreeIconDelegate in viewport coordinates
    int logicalHeight = 0;
  };
  QHash<QString, BakedIcon> cache; // path|maxW — style/size/tint are uniform per pass
  // RetroArch's assets tree, resolved ONCE per pass rather than per row
  // (Kartend-1kkk2): resolution walks the standard config paths and reads a
  // key out of retroarch.cfg, which is far too much to repeat for every row of
  // a large library. Empty when RetroArch is not installed — every glyph
  // lookup below then misses and the rows simply carry no glyph, which is the
  // intended behaviour on a machine without it.
  //
  // Deliberately not cached across passes: a user who installs RetroArch, or
  // repoints the override in Settings, should see glyphs appear on the next
  // refresh rather than after a restart. A pass runs on collection switch and
  // on resize, not per frame.
  const QString assetsDir = RetroArchUtils::resolveAssetsDirectory(
      m_ctx->collection.generalSettings
          ? m_ctx->collection.generalSettings->launchers.retroarchConfigPath
          : QString());
  // Enumerated once for the same reason: choosing the pack that suits a
  // collection's subject is a lookup in this list, and rebuilding the list per
  // row would walk every pack directory once per row.
  const QList<RetroArchIcons::Pack> glyphPacks = assetsDir.isEmpty()
                                                     ? QList<RetroArchIcons::Pack>()
                                                     : RetroArchIcons::discoverPacks(assetsDir);
  // Silhouette packs are recoloured to the ordinary label ink so they read on
  // a light theme as well as a dark one — see tintSilhouetteGlyph.
  const QColor glyphInk = m_tree->palette().color(QPalette::Text);
  /// The ink a glyph STYLE asks for, or an invalid colour for Normal (which
  /// means "leave the art alone"). Mirrors the row artwork's own switch so the
  /// two vocabularies cannot mean different things, and resolves Tinted
  /// through the same tint -> accent -> highlight order the row logos use.
  const auto glyphStyleInk = [this](TreeIconStyle style) -> QColor {
    switch (style) {
    case TreeIconStyle::MonochromeDark:
      return QColor(QStringLiteral("#2e2e2e"));
    case TreeIconStyle::MonochromeLight:
      return QColor(QStringLiteral("#e8e8e8"));
    case TreeIconStyle::Tinted: {
      QColor ink(m_iconTint);
      if (!ink.isValid()) ink = QColor(m_accentColor);
      if (!ink.isValid()) ink = m_tree->palette().color(QPalette::Highlight);
      return ink;
    }
    case TreeIconStyle::Normal:
      break;
    }
    return {};
  };
  QHash<QString, QPixmap> glyphCache; // path|size — one bake per distinct glyph
  // Collected in the pass below, applied after it: every row in this set gets
  // ONE height, so the group reads as an even list. See the setSizeHint arm.
  QList<QTreeWidgetItem *> normalisedRows;
  int maxNormalisedIconH = 0;

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
    // ROOT-level collections carry the desktop TITLEBAR colour (user
    // request 2026-08-19), the same fill as the toolbar and the selection,
    // so the top of the sidebar reads as one continuous piece of chrome
    // rather than a faintly washed row. Deeper categories keep the faint
    // band that distinguishes them from leaves; the Playlists group header
    // (index < 0) keeps its text-only look.
    // NOT item->parent() == nullptr any more: the chrome row's children are
    // promoted to top-level items so they align with the Playlists heading, and
    // they would otherwise pick up root styling — centred, tinted and square —
    // the moment they were promoted.
    const bool isRootCollection = item->data(0, kRoleIsRootCollection).toBool() && index >= 0;
    // ONLY THE TOPMOST root row is chrome (user request 2026-08-22: "on the
    // navbar, only the top one should be the titlebar color"). The point of
    // the titlebar fill is to continue the toolbar across the top of the
    // panel, and only the first row touches the toolbar — painting every root
    // collection with it turned a library with several roots into a stack of
    // chrome bars with no visual hierarchy. Lower roots keep the rest of their
    // root styling (centred, tinted, square) and simply lose the fill.
    const bool isTopRootRow = isRootCollection && m_tree && m_tree->indexOfTopLevelItem(item) == 0;
    const QColor rootFill = isTopRootRow ? KdeColorScheme::activeTitlebarColor() : QColor();
    if (isTopRootRow && rootFill.isValid()) {
      // Handed to the delegate rather than set as the item's background:
      // an item background stops at the indented item rect and leaves a
      // gap down the left, so the delegate fills the row edge to edge.
      item->setData(0, kRoleRootFill, rootFill);
      item->setData(0, kRoleCategoryBand, QVariant());
      item->setBackground(0, QBrush());
      // CENTRED and TINTED (user request 2026-08-19). Set on the item rather
      // than on a paint-time option so the pinned copy inherits it for free —
      // the delegate re-reads the model, so anything set only on the passed
      // option is discarded by initStyleOption.
      item->setTextAlignment(0, Qt::AlignCenter);
      // SAME resolution order as the tinted logos below (m_iconTint, then
      // the accent): the label only consulted m_accentColor, which is empty
      // when the tint is configured explicitly, so it fell through to the
      // titlebar's white text — and saturating white is a no-op, which is
      // why "tint it more" kept changing nothing.
      QColor ink = QColor(m_iconTint);
      if (!ink.isValid()) {
        ink = QColor(m_accentColor);
      }
      if (!ink.isValid()) {
        // The LOGOS' fallback, not the titlebar's text colour. With no tint
        // and no accent configured — the default — the logos resolve to the
        // palette highlight while this label resolved to white, which is why
        // it never looked tinted no matter what else was fixed (2026-08-19).
        ink = m_tree->palette().color(QPalette::Highlight);
      }
      // MATCH the toolbar breadcrumb (user request 2026-08-19) via the shared
      // derivation rather than a second copy: the previous local version
      // saturated the highlight instead of softening it, so the label read
      // far stronger than the text it was meant to match.
      ink = ColorContrast::breadcrumbLinkColor(ink, m_tree->palette().color(QPalette::Window));
      if (ink.isValid()) {
        item->setForeground(0, ink);
      }
    } else if (isCategory) {
      item->setData(0, kRoleRootFill, QVariant());
      QColor band = m_tree->palette().color(QPalette::Text);
      band.setAlpha(14);
      // Handed to the delegate: an item background spans the row and cannot
      // be rounded or inset, which is what pushed it past the scrollbar.
      item->setData(0, kRoleCategoryBand, band);
      item->setBackground(0, QBrush());
      item->setForeground(0, QBrush());
    } else {
      item->setData(0, kRoleRootFill, QVariant());
      item->setData(0, kRoleCategoryBand, QVariant());
      item->setBackground(0, QBrush());
      item->setForeground(0, QBrush());
    }
    if (index < 0 || index >= collections.size()) {
      continue; // the Playlists group header keeps its text-only look
    }
    const QString name = collections.at(index).name;
    int depth = 1; // rootIsDecorated indents even top-level rows one unit
    for (QTreeWidgetItem *p = item->parent(); p; p = p->parent()) ++depth;

    // Hoisted above the glyph bake, which needs it for the manufacturer-logo
    // fallback below; the row-artwork bake further down uses the same value.
    QString parentArtworkDir;
    const int parentIndex = item->data(0, kRoleParentCollection).toInt();
    if (parentIndex >= 0 && parentIndex < collections.size()) {
      const CollectionConfig &parent = collections.at(parentIndex);
      parentArtworkDir = PathUtils::validateAndExpandPath(parent.artworkDirectory, parent.name);
    }

    // The RetroArch system glyph (Kartend-1kkk2). Baked here rather than in
    // the delegate for the same reason the row artwork is: paint() runs on
    // every repaint, and decoding a PNG there would put a file read on the
    // scroll path.
    //
    // Resolved from the collection's SYSTEM each pass, never from a stored
    // path — see SystemIconSettings — so re-theming or updating a RetroArch
    // install is picked up without touching any collection's config.
    item->setData(0, kRoleSystemGlyph, QVariant());
    if (const SystemIconSettings &glyphCfg = collections.at(index).systemIcon; glyphCfg.enabled) {
      QString glyphPath;
      if (!glyphCfg.systemName.isEmpty() && !assetsDir.isEmpty()) {
        // ONE rule for which set gets used — the subject wins over a set that
        // cannot draw it (RetroArchIcons::resolvePack). Shared with the settings
        // page and the bulk-apply path so the sidebar cannot disagree with the
        // control that configured it.
        const QString pack =
            RetroArchIcons::resolvePack(glyphCfg.subject, glyphCfg.packOverride, glyphPacks);
        glyphPath =
            RetroArchIcons::iconPath(assetsDir, pack, glyphCfg.systemName, glyphCfg.subject);
      }
      // Tracks WHICH source won, because the two want different treatment: a
      // RetroArch icon keeps the look of the set it came from, while fallback
      // artwork is standing in for a glyph and has to be made to read like
      // one (see the flatten below).
      bool glyphFromFallback = false;
      if (glyphPath.isEmpty() && glyphCfg.useCollectionArtwork) {
        glyphFromFallback = true;
        // No system — so this row is not a machine. Fall back to the
        // collection's OWN artwork at glyph size (user 2026-08-22:
        // "manufacturers should show their logos instead of a console icon").
        //
        // A shell like "Nintendo" or "Sega" groups systems rather than being
        // one, so autodetect deliberately leaves it without a system; what it
        // does have is a scraped COMPANY logo sitting in collectionIcon, and
        // that is exactly the mark the row wants. Rendering it here rather
        // than through the row-artwork path means it obeys the glyph's size
        // and placement, so a column of platforms and their manufacturers
        // lines up as one list instead of two different treatments.
        glyphPath = CollectionUtils::resolveCollectionTileArtwork(&collections, index, name,
                                                                  parentArtworkDir);
      }
      if (!glyphPath.isEmpty()) {
        // The fallback flag is part of the key: the same file could in
        // principle arrive down either route, and the two bake differently.
        // Style and source both change the baked pixels, so both are in the
        // key — otherwise two collections sharing a system but styled
        // differently would get whichever baked first.
        const QString glyphKey = glyphPath + QLatin1Char('|') + QString::number(glyphCfg.iconSize) +
                                 QLatin1Char('|') +
                                 QString::number(static_cast<int>(glyphCfg.style)) +
                                 (glyphFromFallback ? QLatin1String("|f") : QLatin1String("|s"));
        auto cachedGlyph = glyphCache.find(glyphKey);
        if (cachedGlyph == glyphCache.end()) {
          QPixmap glyph(glyphPath);
          if (!glyph.isNull()) {
            // Trim first, then scale: the packs park some marks inside padded
            // canvases, and scaling the canvas makes those render visibly
            // smaller than their neighbours — the same problem the row
            // artwork hit (field report 2026-08-17, round 6).
            glyph = trimTransparentBorders(glyph);
            const int devH = qMax(1, qRound(glyphCfg.iconSize * dpr));
            // Height-driven with the aspect kept: these are near-square marks,
            // and a fixed height is what makes a column of them line up with
            // the text beside them.
            glyph = glyph.scaledToHeight(devH, Qt::SmoothTransformation);
            // ONE inking rule for BOTH sources (user 2026-08-23: "the lv1
            // subcollection icons are also not tinted, but the others are").
            // A manufacturer logo and the console icons under it are the same
            // kind of mark in the same column, so they cannot be inked by
            // different hardcoded rules — which is exactly what they were.
            if (const QColor styleInk = glyphStyleInk(glyphCfg.style); styleInk.isValid()) {
              glyph = flattenToInk(glyph, styleInk, glyphCfg.style == TreeIconStyle::Tinted);
            } else {
              // Normal — art keeps its own colours. A flat SILHOUETTE is the
              // exception: RetroArch's monochrome and automatic sets are
              // white-on-transparent and would be invisible on a light theme,
              // so those are inked to the label colour regardless.
              glyph = tintSilhouetteGlyph(glyph, glyphInk);
            }
            glyph.setDevicePixelRatio(dpr);
          }
          cachedGlyph = glyphCache.insert(glyphKey, glyph);
        }
        if (!cachedGlyph->isNull()) {
          item->setData(0, kRoleSystemGlyph, *cachedGlyph);
          item->setData(0, kRoleSystemGlyphPlacement, static_cast<int>(glyphCfg.placement));
        }
      }
    }
    // No width budget any more: the bake is driven entirely by the
    // configured icon size, so nothing here depends on how wide the panel
    // happens to be (user, 2026-08-20 — "the icon size should remain fixed").

    // Top-level rows match the toolbar's height (user request
    // 2026-08-18) whether or not they have a logo — computed BEFORE the
    // no-artwork early-out below, which used to skip this entirely and
    // left a text-only top row at ~14px against a 50px toolbar.
    int toolbarHeight = 0;
    if (depth == 1 && m_ctx->ui.itemsTopBar) {
      // height() is 0 until the top bar is laid out and the first bake
      // runs before that, so fall back to its size hint.
      QWidget *bar = m_ctx->ui.itemsTopBar;
      toolbarHeight = qMax(bar->height(), bar->sizeHint().height());
    }

    QString path =
        CollectionUtils::resolveCollectionTileArtwork(&collections, index, name, parentArtworkDir);
    if (path.isEmpty()) {
      item->setData(0, kRoleBakedPixmap, QVariant());
      item->setIcon(0, QIcon());
      item->setText(0, name);
      item->setToolTip(0, QString());
      item->setSizeHint(0, toolbarHeight > 0 ? QSize(0, toolbarHeight) : QSize());
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

    // The row you are VIEWING can keep its colours while the rest stay
    // monochrome/tinted (user request 2026-08-18) — the cache key carries
    // the decision so the two renderings never share an entry.
    const bool colourThisRow = m_iconStyle == TreeIconStyle::Normal ||
                               (m_colorizeSelected && index == activeCollectionIndex());
    // Keyed on the CONFIGURED size, never the panel width: the bake is now
    // width-independent, so including maxWidth only threw the cache away on
    // every drag frame to re-produce a byte-identical pixmap.
    const QString cacheKey = path + QLatin1Char('|') + QString::number(m_iconSize) +
                             (colourThisRow ? QLatin1String("|c") : QLatin1String("|m"));
    auto cached = cache.find(cacheKey);
    if (cached == cache.end()) {
      QPixmap pm;
      const int devHeight = qMax(1, qRound(m_iconSize * dpr));
      if (path.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)) {
        // Render at 2x the target box, then TRIM: the scraped monochrome
        // SVGs park the art inside padded viewBoxes, so an untrimmed render
        // floated the logo wherever the viewBox put it (field report
        // 2026-08-17: "alignment is still off in mono/tinted mode" — the
        // mono styles are exactly the ones that swap to SVG sources). The
        // oversized render keeps the post-trim downscale sharp.
        // Render box derived from the HEIGHT budget alone. It used to be
        // devWidth*2, which made a narrow panel render the source small and
        // the later upscale soft — the size survived, the sharpness did not.
        // 8:1 is wider than any real logo, so the height is what binds.
        const int renderDevH = qRound(devHeight * kThinHeightBoost) * 2;
        pm = QIcon(path).pixmap(QSize(renderDevH * 8, renderDevH));
      }
      if (pm.isNull()) {
        pm = QPixmap(path);
      }
      pm = trimTransparentBorders(pm);
      if (!pm.isNull()) {
        // HEIGHT-driven, not box-fit (field report 2026-08-18: "the icon
        // size changes depending on the width of the sidebar — it should
        // be fixed according to the specified size"). Fitting inside a box
        // makes wide logos width-bound, so their rendered height tracked
        // the panel width. Scaling to the target height first pins every
        // icon to the configured size; the width clamp below only engages
        // for a logo too wide to fit at that height, which is the one case
        // where something has to give.
        // FIT TO A BOX, rather than scaling on height alone. Every logo is
        // scaled to fit inside (kIconMaxAspect x kThinHeightBoost) icon sizes,
        // so a square mark fills the box's height and a wordmark fills its
        // width. Both then occupy comparable area, which is what makes a
        // column of mixed logos read as one size — the thing height-only
        // scaling could never deliver, since it left width unbounded.
        const int boxH = qMax(1, qRound(devHeight * kThinHeightBoost));
        const int boxW = qMax(1, qRound(devHeight * kIconMaxAspect));
        const qreal scale = std::min(static_cast<qreal>(boxW) / qMax(1, pm.width()),
                                     static_cast<qreal>(boxH) / qMax(1, pm.height()));
        pm = pm.scaled(qMax(1, qRound(pm.width() * scale)), qMax(1, qRound(pm.height() * scale)),
                       Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        // NO width clamp. It was meant to engage only for a logo too wide to
        // fit, but platform logos are mostly wide, so in practice it engaged
        // on nearly every row and the rendered size tracked the panel width
        // again — the same complaint, twice (2026-08-18, and 2026-08-20:
        // "expanding nav pane also increases icon size ... the icon size
        // should remain fixed"). The configured size now wins outright; a
        // logo too wide for the panel overflows rather than shrinking, which
        // is the user's call to make with the width and size settings.
      }
      if (!pm.isNull() && !colourThisRow) {
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
                const QColor c = QColor::fromHslF(tintHue < 0 ? 0 : tintHue, tintSat,
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
        // Height-capped only — see the note on the width clamp above.
      }
      BakedIcon baked;
      if (!pm.isNull()) {
        pm.setDevicePixelRatio(dpr);
        baked.pixmap = pm;
        baked.logicalHeight = qMax(1, qRound(pm.height() / dpr));
      }
      cached = cache.insert(cacheKey, baked);
    }

    const QPixmap &bakedRaw = cached.value().pixmap;
    // Kartend-j1mtg: in TextOnly the pixmap is still CACHED (switching modes
    // must not force a re-bake) but it is invisible to everything downstream —
    // the delegate does not draw it, and, just as importantly, the row must not
    // be SIZED for it. Sizing ran off the baked height regardless of mode, so
    // text-only rows kept icon-tall gaps between one-line labels.
    const bool iconHidden = (m_iconDisplay == TreeIconDisplay::TextOnly);
    const QPixmap baked = iconHidden ? QPixmap() : bakedRaw;
    item->setIcon(0, QIcon()); // TreeIconDelegate paints; no decoration
    item->setData(0, kRoleBakedPixmap, baked.isNull() ? QVariant() : QVariant(baked));
    // Per-row height hugs the baked pixmap plus a breathing gap that
    // scales with the icon size (field reports 2026-08-17: the view-wide
    // decoration height ballooned every row; a flat 4px then read "too
    // cramped vertically"). Boosted wordmark rows stay taller, square
    // rows tighter, and the gap keeps big logos from touching.
    if (toolbarHeight > 0) {
      // Top-level rows match the toolbar height, logo or not (user request
      // 2026-08-18, restated 2026-08-19). Only the no-artwork branch above
      // honoured that, so a root collection WITH a logo — the normal case —
      // fell through to the pixmap-hugging height below and stood at a
      // different height from the toolbar it is supposed to line up with.
      // Floored, never truncated: an icon taller than the toolbar keeps its
      // own height rather than being clipped to match.
      item->setSizeHint(
          0, QSize(0, qMax(toolbarHeight, baked.isNull() ? 0 : cached.value().logicalHeight)));
    } else if (!baked.isNull()) {
      // Height is NORMALISED across these rows rather than hugging each
      // pixmap (user request 2026-08-20: "category height should be
      // normalized"). Per-logo heights made neighbouring rows visibly
      // uneven once compact marks gained their own boost — a square mark at
      // 1.8x stood taller than a wordmark at 1.17x. Deferred to a second
      // pass below, which uses the TALLEST baked icon actually present:
      // a fixed worst-case ceiling would balloon every row even when no
      // logo needs the room (the 2026-08-17 regression).
      normalisedRows.append(item);
      maxNormalisedIconH = qMax(maxNormalisedIconH, cached.value().logicalHeight);
    } else {
      item->setSizeHint(0, QSize());
    }
    // Icons-only mode: the name moves to the tooltip. Rows whose icon did
    // NOT resolve keep their text — a blank row would be unusable.
    if (!baked.isNull() && m_iconDisplay == TreeIconDisplay::IconOnly) {
      item->setText(0, QString());
      item->setToolTip(0, name);
    } else {
      item->setText(0, name);
      item->setToolTip(0, QString());
    }
  }

  // Second pass: one height for every row that carries a logo at this depth.
  // Driven by the tallest icon actually baked, so the rows are even without
  // reserving space no logo uses.
  if (!normalisedRows.isEmpty() && maxNormalisedIconH > 0) {
    const int rowPad = qMax(16, (m_iconSize * 3) / 4);
    const int uniformHeight = maxNormalisedIconH + rowPad;
    for (QTreeWidgetItem *row : normalisedRows) {
      row->setSizeHint(0, QSize(0, uniformHeight));
    }
  }
}

void CollectionTreeController::onCollectionSwitched(int collectionIndex) {
  applyStateForCollection(collectionIndex);
  highlightCollection(collectionIndex);
}

void CollectionTreeController::mutateCollectionIcon(
    int collectionIndex, const std::function<void(CollectionConfig &)> &mutate) {
  if (!m_ctx || !m_ctx->collection.collections) {
    return;
  }
  auto *collections = const_cast<QList<CollectionConfig> *>(m_ctx->collection.collections);
  if (collectionIndex < 0 || collectionIndex >= collections->size()) {
    return;
  }
  mutate((*collections)[collectionIndex]);
  if (m_persistCollections) {
    m_persistCollections();
  }
  // The active collection's settings are cached for the rebake test, and the
  // row just edited may not be the active one — so invalidate rather than
  // trusting the comparison, or an edit to another row would be a no-op on
  // screen (the stale-glyph bug, one more time).
  m_systemIcon = SystemIconSettings{};
  refreshIcons();
}

void CollectionTreeController::showRowContextMenu(const QPoint &pos) {
  if (!m_tree || !m_ctx || !m_ctx->collection.collections) {
    return;
  }
  QTreeWidgetItem *item = m_tree->itemAt(pos);
  if (!item) {
    return;
  }
  const int index = item->data(0, kRoleCollectionIndex).toInt();
  const QList<CollectionConfig> &collections = *m_ctx->collection.collections;
  if (index < 0 || index >= collections.size()) {
    return; // the synthetic Playlists heading owns no collection
  }
  const CollectionConfig &cfg = collections.at(index);

  QMenu menu(m_tree);
  // Named for the ROW, so a menu raised over one collection while another is
  // active cannot be misread as acting on the active one.
  menu.addSection(cfg.name);

  QAction *setCustom = menu.addAction(tr("Set Custom Icon…"));
  QAction *detect = menu.addAction(tr("Detect System Icon"));
  menu.addSeparator();
  QAction *remove = menu.addAction(tr("Remove Icon"));
  // Nothing to remove is a disabled entry rather than a missing one: a menu
  // whose shape changes per row is harder to learn than one with a greyed item.
  remove->setEnabled(cfg.systemIcon.enabled);

  QAction *chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
  if (!chosen) {
    return;
  }

  if (chosen == setCustom) {
    const QString picked =
        QFileDialog::getOpenFileName(m_tree, tr("Select Icon for \"%1\"").arg(cfg.name),
                                     PathUtils::expandPathWithoutExistenceCheck(cfg.collectionIcon),
                                     tr("Images (*.png *.jpg *.jpeg *.webp *.svg);;All files (*)"));
    if (picked.isEmpty()) {
      return;
    }
    // Reuses cfg.collectionIcon — the collection's existing icon slot — rather
    // than inventing a second per-collection image field. Pointing the glyph
    // at it (useCollectionArtwork) is what puts the chosen file in the sidebar
    // at the glyph's size and placement.
    mutateCollectionIcon(index, [&picked](CollectionConfig &c) {
      c.collectionIcon = picked;
      c.systemIcon.enabled = true;
      c.systemIcon.useCollectionArtwork = true;
      c.systemIcon.systemName.clear();
      c.systemIcon.systemAutoDetected = false;
    });
    return;
  }

  if (chosen == detect) {
    const QString assetsDir = RetroArchUtils::resolveAssetsDirectory(
        m_ctx->collection.generalSettings
            ? m_ctx->collection.generalSettings->launchers.retroarchConfigPath
            : QString());
    const QList<RetroArchIcons::Pack> packs = RetroArchIcons::discoverPacks(assetsDir);
    const QString pack =
        RetroArchIcons::resolvePack(cfg.systemIcon.subject, cfg.systemIcon.packOverride, packs);
    const QString detected = RetroArchIcons::autodetectSystem(
        cfg.name, RetroArchIcons::discoverSystems(assetsDir, pack));
    if (detected.isEmpty()) {
      // Say so rather than appearing to do nothing — a silent no-op on a menu
      // item reads as a bug.
      QMessageBox::information(m_tree, tr("Detect System Icon"),
                               tr("No system matched \"%1\". Pick one under Settings → "
                                  "Appearance → Sidebars.")
                                   .arg(cfg.name));
      return;
    }
    mutateCollectionIcon(index, [&detected](CollectionConfig &c) {
      c.systemIcon.enabled = true;
      c.systemIcon.systemName = detected;
      c.systemIcon.systemAutoDetected = true;
      c.systemIcon.useCollectionArtwork = false;
    });
    return;
  }

  if (chosen == remove) {
    // Switches the slot OFF rather than clearing the system, so the row's
    // system and artwork survive for when it is turned back on.
    mutateCollectionIcon(index, [](CollectionConfig &c) { c.systemIcon.enabled = false; });
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
  insertPanelAt(tree.treePosition, tree.treeJustification, tree.treeMode);
  applyPanelWidth(tree.treeWidth, tree.treePosition);
  // Before the icon rebake below. With no overlay attached this changes the
  // native policy, which resizes the viewport the icons are baked against —
  // and refreshIcons stamps m_bakedViewportWidth from that.
  OverlayScrollbars::setScrollbarMode(m_tree, tree.treeScrollbarMode);
  // Icon display options are baked into the rows by rebuildTree, so a
  // change (settings edit, collection switch between differently-configured
  // collections) needs a rebuild — cheap, and the expansion memory keeps the
  // tree's shape across it.
  // Width participates in the rebake decision (field report 2026-08-17,
  // round 4): treeWidth is per-collection, so a switch can resize the panel
  // under icons baked for the previous width — wide logos then clip at the
  // new edge. m_bakedPanelWidth is stamped by refreshIcons itself.
  // Kartend-1kkk2: the system glyph's settings are baked into the rows too, so
  // they belong in this test — every one of them (subject, set, system,
  // placement, size) changes what gets drawn. Missing them meant editing the
  // glyph's settings changed NOTHING on screen until something unrelated
  // forced a rebake, which is how "still shows controller icon after choosing
  // console" was reported: the setting had saved correctly and the row was
  // still showing the pixmap baked before the edit.
  //
  // Compared as a WHOLE STRUCT rather than field by field, so a field added to
  // SystemIconSettings later cannot silently fall out of the rebake test the
  // way these five did.
  const SystemIconSettings &glyphCfg = collections.at(collectionIndex).systemIcon;
  const bool displayChanged =
      m_iconDisplay != tree.treeIconDisplay || m_iconSize != tree.treeIconSize ||
      m_iconStyle != tree.treeIconStyle || m_iconTint != tree.treeIconTintColor ||
      m_colorizeSelected != tree.treeColorizeSelected || m_systemIcon != glyphCfg ||
      (m_panel && m_panel->width() != m_bakedPanelWidth);
  m_systemIcon = glyphCfg;
  m_iconDisplay = tree.treeIconDisplay;
  // The delegate reads the mode off the view; publish it before any repaint.
  if (m_tree) {
    m_tree->setProperty("kartendIconDisplay", static_cast<int>(tree.treeIconDisplay));
  }
  m_iconSize = tree.treeIconSize;
  m_iconStyle = tree.treeIconStyle;
  m_iconTint = tree.treeIconTintColor;
  m_colorizeSelected = tree.treeColorizeSelected;
  if (displayChanged) {
    refreshIcons();
  }
  if (m_tree && m_tree->property("kartendShowLines").toBool() != tree.treeShowLines) {
    m_tree->setProperty("kartendShowLines", tree.treeShowLines);
    if (m_tree->viewport()) m_tree->viewport()->update();
  }
  if (m_tree && m_tree->property("kartendScrollClippedLabelsOnHover").toBool() !=
                    tree.treeScrollClippedLabelsOnHover) {
    m_tree->setProperty("kartendScrollClippedLabelsOnHover", tree.treeScrollClippedLabelsOnHover);
    if (m_tree->viewport()) m_tree->viewport()->update();
  }
  if (m_tree &&
      m_tree->property("kartendScrollClippedLabels").toBool() != tree.treeScrollClippedLabels) {
    m_tree->setProperty("kartendScrollClippedLabels", tree.treeScrollClippedLabels);
    // Turning it OFF must repaint too, or the last frame of a scrolled label
    // stays on screen at whatever offset it had reached.
    m_labelScrollPhase = 0;
    m_tree->setProperty("kartendLabelScrollPhase", 0);
    if (m_tree->viewport()) m_tree->viewport()->update();
  }
  const bool wasVisible = m_panel->isVisible();
  m_panel->setVisible(tree.treeVisible);
  if (wasVisible != tree.treeVisible) {
    emit visibilityChanged(tree.treeVisible);
  }
  // AFTER setVisible, not before. Called first, it asked whether the panel was
  // shown while the panel was still hidden, declined to start, and was never
  // called again — a clipped name then sat frozen at phase 0 for good.
  updateLabelScrollTimer();
}

void CollectionTreeController::updateLabelScrollTimer() {
  if (!m_tree) {
    return;
  }
  // isHidden(), NOT isVisible(). isVisible() is false for a widget whose window
  // has not been shown yet, and this runs from applyStateForCollection during
  // startup — so the timer was never started, nothing called back, and a
  // clipped name sat frozen at phase 0 forever (measured: 10 identical frames
  // over 16s). isHidden() answers the question actually being asked, "has this
  // panel been switched off", and is false as soon as setVisible(true) has been
  // called regardless of whether the window is on screen yet.
  const bool wanted = (m_tree->property("kartendScrollClippedLabels").toBool() ||
                       m_tree->property("kartendScrollClippedLabelsOnHover").toBool()) &&
                      m_panel && !m_panel->isHidden();
  if (!wanted) {
    if (m_labelScrollTimer) {
      m_labelScrollTimer->stop();
    }
    return;
  }
  if (!m_labelScrollTimer) {
    // Parented to `this` — the timer must die with the controller, and
    // parent() is a lifetime guard in this codebase, not just bookkeeping.
    m_labelScrollTimer = new QTimer(this);
    // ~20fps. Fast enough to read as motion rather than stepping, slow enough
    // that a sidebar repaint is nowhere near a frame budget; this box also
    // streams its desktop, so a 60fps repaint of a static panel is waste.
    m_labelScrollTimer->setInterval(50);
    connect(m_labelScrollTimer, &QTimer::timeout, this, [this]() {
      if (!m_tree || !m_tree->viewport()) {
        return;
      }
      // The delegate sets this while painting whenever it had to clip a label.
      // Read LAST frame's answer, then clear it so this frame can answer afresh.
      const bool sawClipped = m_tree->property("kartendSawClippedLabel").toBool();
      m_tree->setProperty("kartendSawClippedLabel", false);
      if (sawClipped) {
        m_labelScrollIdleTicks = 0;
      } else if (++m_labelScrollIdleTicks > 4) {
        // Nothing has needed scrolling for four frames — stop, and let a
        // rebuild/resize/settings change restart us. Without this the panel
        // repaints forever on a library whose names all fit.
        m_labelScrollTimer->stop();
        m_labelScrollIdleTicks = 0;
        return;
      }
      ++m_labelScrollPhase;
      m_tree->setProperty("kartendLabelScrollPhase", m_labelScrollPhase);
      m_tree->viewport()->update();
    });
  }
  if (!m_labelScrollTimer->isActive()) {
    m_labelScrollIdleTicks = 0;
    m_labelScrollTimer->start();
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
  insertPanelAt(position, m_insertedJustification, m_insertedMode);
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
  // The SELECTION takes the desktop's TITLEBAR colour (user request
  // 2026-08-18). Not the accent and not the collection's primary colour:
  // with accent-from-wallpaper the titlebar and the accent are different
  // oranges, so the earlier accent version still visibly mismatched. The
  // collection's colour still carries the header text.
  const QColor titlebar = KdeColorScheme::activeTitlebarColor();
  const QString selectionColor =
      titlebar.isValid() ? titlebar.name() : QStringLiteral("palette(highlight)");
  if (m_tree) {
    // Handed to the delegate, which stops the fill at the scrollbar lane.
    m_tree->setProperty("kartendSelectionColor",
                        titlebar.isValid() ? titlebar
                                           : m_tree->palette().color(QPalette::Highlight));
  }
  m_panel->setStyleSheet(QStringLiteral("QWidget#collectionTreePanel {"
                                        " background-color: palette(window); }"
                                        "QLabel#collectionTreeHeader {"
                                        " color: %1; font-weight: bold; }"
                                        "QTreeWidget#collectionTreeWidget {"
                                        " background-color: palette(window);"
                                        " color: palette(text); border: none; }"
                                        // background TRANSPARENT: the delegate
                                        // paints the selection so it can stop at
                                        // the scrollbar lane. A styled fill here
                                        // always spans the full row.
                                        "QTreeWidget#collectionTreeWidget::item:selected {"
                                        " background-color: transparent;"
                                        " color: palette(highlighted-text); }"
                                        // The 5px drag grip sits between the panel and the
                                        // content and was painting in the panel's own colour —
                                        // measured as a 10-physical-pixel dark band splitting
                                        // the sidebar from the toolbar (field report
                                        // 2026-08-18: "there are gaps"). Same fill as the
                                        // toolbar makes the seam disappear while staying
                                        // draggable.
                                        // TRANSPARENT, not filled (field report
                                        // 2026-08-18: "still noticably thicker").
                                        // The details pane has no painted divider
                                        // at all — its grip is an invisible hit
                                        // zone — so any fill here reads as a
                                        // thicker edge. The 8px drag target stays.
                                        "QWidget#collectionTreeGrip {"
                                        " background-color: transparent; }")
                             .arg(accent, selectionColor));
}

void CollectionTreeController::refreshDesktopTint() {
  // Both surfaces: the SELECTION lives in the panel stylesheet, the
  // root-collection fill is a per-item background baked in refreshIcons.
  // Refreshing only the stylesheet left the root row on the previous
  // desktop's colour.
  applyPrimaryColor(m_accentColor);
  refreshIcons();
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
