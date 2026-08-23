#include "virtualcontainermanager.h"

#include "filtermanager.h"
#include "overlayscrollbars.h"
#include "scrollhelpers.h"
#include "selectionoverlaymanager.h"
#include "uiconstants/grid.h"
#include <QLayout>
#include <QLayoutItem>
#include <QLoggingCategory>
#include <QMargins>
#include <QScrollArea>
#include <QScrollBar>
#include <QWidget>

Q_DECLARE_LOGGING_CATEGORY(lcScrollManager)

VirtualContainerManager::VirtualContainerManager(QObject *parent) : QObject(parent) {}

VirtualContainerManager::~VirtualContainerManager() {
  cleanupContainer();
}

void VirtualContainerManager::setOverlayManager(SelectionOverlayManager *overlay) {
  m_overlayManager = overlay;
}

void VirtualContainerManager::setFilterManager(FilterManager *filter) {
  m_filterManager = filter;
}

void VirtualContainerManager::createContainer() {
  cleanupContainer();
  // A new collection re-measures its own cell inset; forget the previous
  // one's so a stale value cannot leak across the switch.
  m_lastContentInset = -1;

  if (!m_gridContainer) {
    qCWarning(lcScrollManager) << "VirtualContainerManager::createContainer: m_gridContainer "
                                  "is null, cannot create container";
    return;
  }

  m_virtualContainer = new QWidget(m_gridContainer);
  m_virtualContainer->setObjectName("virtualContainer");
  m_virtualContainer->setAttribute(Qt::WA_TransparentForMouseEvents, false);
  m_virtualContainer->setStyleSheet("background: transparent;");

  // Update overlay manager's parent widget
  if (m_overlayManager) {
    m_overlayManager->setParentWidget(m_virtualContainer);
    m_overlayManager->setGridContainer(m_gridContainer);
  }
}

void VirtualContainerManager::cleanupContainer() {
  if (!m_virtualContainer) {
    return;
  }
  m_virtualContainer->deleteLater();
  m_virtualContainer = nullptr;
}

int VirtualContainerManager::getEffectiveViewportWidth() const {
  if (!m_scrollArea || !m_scrollArea->viewport()) {
    return 0;
  }
  static constexpr int MIN_EFFECTIVE_VIEWPORT_WIDTH = 200;
  // Overlay scrollbars occupy a lane at the right edge. Content laid out
  // into that lane is covered by the handle (field report 2026-08-19: "the
  // grid scrollbar also overlaps the items"), so the usable width stops
  // short of it. Zero when overlay bars are off, and constant while they
  // are on — a handle fading in or out never moves an item.
  const int usable =
      m_scrollArea->viewport()->width() - OverlayScrollbars::reservedGutter(m_scrollArea);
  return qMax(MIN_EFFECTIVE_VIEWPORT_WIDTH, usable);
}

int VirtualContainerManager::getScrollbarWidth() const {
  if (!m_scrollArea) {
    return 0;
  }
  QScrollBar *verticalScrollbar = m_scrollArea->verticalScrollBar();
  if (!verticalScrollbar) {
    return 0;
  }
  if (verticalScrollbar->isVisible()) {
    return verticalScrollbar->width();
  }
  // Estimate scrollbar width if not visible but will be needed
  int barWidth = verticalScrollbar->sizeHint().width();
  static constexpr int DEFAULT_SCROLLBAR_WIDTH = 16;
  return barWidth > 0 ? barWidth : DEFAULT_SCROLLBAR_WIDTH;
}

bool VirtualContainerManager::willNeedVerticalScrollbar(int totalHeight) const {
  if (!m_scrollArea || !m_scrollArea->viewport()) {
    return false;
  }
  return totalHeight > m_scrollArea->viewport()->height();
}

void VirtualContainerManager::positionContainer(const ContainerPositionParams &params) {
  if (!m_virtualContainer || !m_scrollArea) {
    return;
  }

  // The grid container's height is clamped to totalHeight, and a QWidgetItem
  // with no alignment CENTERS a max-constrained child in its layout cell.
  // That centering is the deliberate look for an unfiltered tile view (a
  // short collection floats mid-viewport, hub-page style) — but it is wrong
  // in two places, both filed as the "empty band" bugs:
  //   * list mode, always: the rows float away from the pinned column
  //     header, leaving a blank band under it (Kartend-99rcn);
  //   * a SEARCHED or FILTERED grid: typing a query dropped the few matches
  //     a band lower than the unfiltered first row, which reads as the
  //     results arriving somewhere random rather than at the top
  //     (Kartend-d3813). Both flags matter — FilterManager filtering and the
  //     DB-backed search modes are disjoint mechanisms (see
  //     ContainerPositionParams::isSearch).
  // Horizontal mode keeps the centered strip even when narrowed — the whole
  // view is a vertically-centered band by design. RESET the alignment on
  // every pass so leaving a pinned state by any path restores centering.
  if (m_gridContainer) {
    if (QWidget *host = m_gridContainer->parentWidget()) {
      if (QLayout *hostLayout = host->layout()) {
        const bool pinTop =
            params.isList || ((params.isFiltered || params.isSearch) && !params.isHorizontal);
        hostLayout->setAlignment(m_gridContainer,
                                 pinTop ? Qt::Alignment(Qt::AlignTop) : Qt::Alignment());
      }
    }
  }

  if (params.isHorizontal) {
    // horizontal layout pins the container to the top-left and
    // sizes the grid container to match the long-axis width so the
    // horizontal scrollbar reflects the full content. Vertical alignment /
    // centering is unused — the column always fits in the viewport.
    if (m_gridContainer) {
      m_gridContainer->setMinimumSize(params.totalWidth, params.totalHeight);
      m_gridContainer->setMaximumSize(params.totalWidth, params.totalHeight);
    }
    m_virtualContainer->setFixedSize(params.totalWidth, params.totalHeight);
    m_virtualContainer->move(0, 0);
    // Scrollbar policy is owned by ScrollManager::updateViewType /
    // applyHorizontalScrollbarSetting — don't override it here.
    if (m_scrollArea->horizontalScrollBarPolicy() != Qt::ScrollBarAlwaysOff) {
      if (auto *hbar = m_scrollArea->horizontalScrollBar()) {
        hbar->show();
      }
    }
    return;
  }

  // The viewport ALREADY excludes any visible scrollbar — it lives inside
  // the bars — and overlay scrollbars reserve no space at all. This used to
  // subtract a PREDICTED bar width whenever the bar was not visible, which
  // is double-counting: every layout lost ~90px that nothing occupied, so
  // right-aligned content sat that far left of the edge it was supposed to
  // hug and the first column could be pushed clean off screen (reproduced
  // in the VM, 2026-08-18 — the first tile vanished entirely).
  const int availableWidth = getEffectiveViewportWidth();

  int contentWidth = params.totalWidth;
  bool overflow = contentWidth > availableWidth;

  setupContainerSizes(availableWidth, contentWidth, params.totalHeight, overflow);

  HorizontalAlignment align = getEffectiveAlignment(params);

  // ALIGNMENT IS RECOMPUTED EVERY PASS, including when only the chrome around
  // the grid changed size (maintainer, 2026-08-20: "the alignment should be
  // recalculated each time any sidebar width changes, according to the
  // specified alignment setting").
  //
  // This deliberately replaces the window-anchored HOLD that lived here
  // through several rounds of "the grid shouldn't move when chrome resizes".
  // That hold was answering a symptom: an overlay scrollbar lane reserved 21px
  // that nothing painted in (Kartend-3o4i4), so the grid genuinely did sit
  // wrong beside the details pane and re-aligning made it visibly chase the
  // pane. With the lane gone the honest behaviour is the simple one — the
  // alignment setting means what it says, continuously.
  const int contentInset = resolveContentInset(params.contentInset, m_lastContentInset);
  const int containerX =
      calculateContainerPosition(availableWidth, contentWidth, align, contentInset);
  m_lastContentInset = contentInset;

  // Permanent diagnostic for "there is still a gap beside the pane" reports,
  // which have now cost several rounds of guesswork each. Everything needed to
  // attribute a gap is here: the reserved lane, the painted band (which is what
  // the user actually SEES, cell dead-margin excluded), and the resulting gap
  // on each side. Enable with:
  //   QT_LOGGING_RULES='kartend.scrollmanager.debug=true'
  if (lcScrollManager().isDebugEnabled()) {
    const int paintedLeft = containerX + contentInset;
    const int painted = contentWidth - 2 * contentInset;
    qCDebug(lcScrollManager).nospace()
        << "GRIDGAP viewportWinX="
        << (m_scrollArea->window()
                ? m_scrollArea->viewport()->mapTo(m_scrollArea->window(), QPoint(0, 0)).x()
                : -1)
        << " viewport=" << m_scrollArea->viewport()->width()
        << " lane=" << (m_scrollArea->viewport()->width() - availableWidth)
        << " usable=" << availableWidth << " contentW=" << contentWidth
        << " cellDeadMargin=" << contentInset << " painted=" << painted << " x=" << containerX
        << " | gapLeft=" << paintedLeft
        << " gapRight=" << (m_scrollArea->viewport()->width() - (paintedLeft + painted))
        << " align="
        << static_cast<int>(align)
        // What sits between the viewport and the scroll area's own edge: the
        // frame, and any NATIVE scrollbar (the viewport excludes it). A wide
        // styled bar here reads as a margin beside the details pane.
        << " | areaW=" << m_scrollArea->width() << " areaWinX="
        << (m_scrollArea->window() ? m_scrollArea->mapTo(m_scrollArea->window(), QPoint(0, 0)).x()
                                   : -1)
        << " frame=" << m_scrollArea->frameWidth() << " vbarVisible="
        << (m_scrollArea->verticalScrollBar() && m_scrollArea->verticalScrollBar()->isVisible())
        << " vbarW="
        << (m_scrollArea->verticalScrollBar() ? m_scrollArea->verticalScrollBar()->width() : 0)
        << " policy=" << static_cast<int>(m_scrollArea->verticalScrollBarPolicy())
        << " overlayAttached="
        << OverlayScrollbars::isAttached(m_scrollArea)
        // Where the area's width actually goes: viewport rect inside the area.
        // Anything left over on the right is margins/frame/bar slot.
        << " | vpRect=" << m_scrollArea->viewport()->x() << "," << m_scrollArea->viewport()->y()
        << " " << m_scrollArea->viewport()->width() << "x" << m_scrollArea->viewport()->height()
        << " rightLeftover="
        << (m_scrollArea->width() - m_scrollArea->viewport()->x() -
            m_scrollArea->viewport()->width());

    // The scroll area's ROW at settle. If the area is narrower than the space
    // the row offers, something is reserving width the area never uses — which
    // is a gap nobody owns.
    if (QWidget *rowHost = m_scrollArea->parentWidget()) {
      QString dump;
      if (QLayout *row = rowHost->layout()) {
        for (int i = 0; i < row->count(); ++i) {
          QLayoutItem *it = row->itemAt(i);
          if (!it) continue;
          if (QWidget *w = it->widget()) {
            dump +=
                QStringLiteral(" [%1 x=%2 w=%3 vis=%4]")
                    .arg(w->objectName().isEmpty() ? w->metaObject()->className() : w->objectName())
                    .arg(w->x())
                    .arg(w->width())
                    .arg(w->isVisible() ? 1 : 0);
          } else if (it->spacerItem()) {
            dump += QStringLiteral(" [SPACER w=%1]").arg(it->geometry().width());
          }
        }
        const QMargins m = row->contentsMargins();
        dump += QStringLiteral(" margins=%1,%2 spacing=%3")
                    .arg(m.left())
                    .arg(m.right())
                    .arg(row->spacing());
      } else {
        dump = QStringLiteral(" (no layout)");
      }
      qCDebug(lcScrollManager).nospace()
          << "GRIDROW host=" << rowHost->objectName() << " hostW=" << rowHost->width() << dump;
    }
  }

  configureHorizontalScrollbar(overflow);
  m_virtualContainer->move(containerX, 0);
  m_virtualContainer->resize(params.totalWidth, params.totalHeight);
}

void VirtualContainerManager::setupContainerSizes(int availableWidth, int contentWidth,
                                                  int totalHeight, bool overflow) {
  if (!m_gridContainer || !m_virtualContainer) {
    return;
  }

  if (overflow) {
    m_gridContainer->setMinimumSize(availableWidth, totalHeight);
    m_gridContainer->setMaximumWidth(availableWidth);
  } else {
    m_gridContainer->setMinimumSize(qMax(availableWidth, contentWidth), totalHeight);
    m_gridContainer->setMaximumWidth(QWIDGETSIZE_MAX);
  }
  m_gridContainer->setMaximumHeight(totalHeight);
  m_virtualContainer->setFixedSize(contentWidth, totalHeight);
}

HorizontalAlignment
VirtualContainerManager::getEffectiveAlignment(const ContainerPositionParams &params) const {
  return ScrollHelpers::effectiveAlignment(params.alignment, params.isFiltered, params.totalItems,
                                           params.itemsPerRow);
}

int VirtualContainerManager::calculateContainerPosition(int availableWidth, int contentWidth,
                                                        HorizontalAlignment align,
                                                        int contentInset) const {
  // The whole contract (user, 2026-08-18): an OVERSIZED grid may clip —
  // that is unavoidable — but a grid that FITS must never clip, whatever
  // the alignment. Slack is what is left over; it is negative exactly when
  // the content is too wide, and the same three cases serve both.
  //
  // This replaces a pile of empirical nudges (a 20px shift, a -10 centre
  // tweak, hidden-scrollbar compensations of -5/-20/-10). Those pushed a
  // FITTING grid to a negative x, so the first column was clipped even
  // though there was room for every tile — and on overflow they collapsed
  // Left and Right onto the same position.
  //
  // Alignment anchors the PAINTED band, not the container box. The first and
  // last cell each carry `contentInset` px the artwork never paints (the art
  // box is a square capped by the cell HEIGHT, so a wider cell is blank at
  // the edges). Anchoring the box left that dead margin against the viewport
  // edge — "the gap between the grid and the details pane is still too big"
  // (field report 2026-08-19) — even though the box itself was flush.
  // Working in painted coordinates and shifting back by the inset puts what
  // the user SEES against the edge. Centre is unaffected either way: a
  // symmetric inset cancels.
  const int painted = contentWidth - 2 * contentInset;
  const int slack = availableWidth - painted;
  switch (align) {
  case HorizontalAlignment::Left:
    // Painted left edge at the viewport's left. The container starts before
    // it by the dead margin, which is legitimately negative — the cell's
    // blank edge hangs off-screen and there is nothing there to clip.
    return -contentInset;
  case HorizontalAlignment::Right:
    return slack - contentInset; // painted right edge flush with the viewport
  case HorizontalAlignment::Center:
  default:
    return slack / 2 - contentInset; // centred on the painted band
  }
}

void VirtualContainerManager::configureHorizontalScrollbar(bool overflow) {
  if (overflow && m_scrollArea) {
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if (auto *horizontalScrollbar = m_scrollArea->horizontalScrollBar()) {
      horizontalScrollbar->setValue(0);
      horizontalScrollbar->hide();
    }
  }
}
