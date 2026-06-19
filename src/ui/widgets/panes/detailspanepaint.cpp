// Sibling translation unit for DetailsPane (/ vbs theming +
// text zoom). Hosts every method that paints the panel
// background or applies appearance/typography from a CollectionConfig:
// applyAppearance, paintEvent, captureLabelFontBaselines, applySidebarFont,
// applyBubbleStyles. Member access only — these remain DetailsPane methods.

#include <algorithm>
#include <QColor>
#include <QFont>
#include <QFutureWatcher>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPixmap>
#include <QPointer>
#include <QSet>
#include <QSize>
#include <QStyle>
#include <QtConcurrent/QtConcurrentRun>

#include "collection/collectionconfig.h"
#include "detailspane.h"
#include "detailspaneresizegrip.h"
#include "extensionutils.h"
#include "imagedecodeutils.h"
#include "itemwidget.h"
#include "textzoom.h"
#include "ui_detailspane.h"

void DetailsPane::applyAppearance(const CollectionConfig &collection, bool reloadBackground) {
  // m_position must be set BEFORE setActiveTab, because
  // applyTabVisibility() consults it to decide whether the section-title
  // chrome (titleLabel / artworkLabel / fileInfoTitle) should be hidden in
  // T/B dock. Calling setActiveTab first would run that visibility logic
  // against the previous orientation and the user would see ghost headers
  // on the first paint after a position change.
  m_widthLocked = collection.sidebar.sidebarWidthLocked;
  m_position = collection.sidebar.sidebarPosition;
  if (m_resizeGrip) {
    m_resizeGrip->setLocked(m_widthLocked);
    m_resizeGrip->setPosition(m_position);
  }
  // flip the inner content layout direction + scrollbar policy
  // to match the active dock edge. Done before setActiveTab so the wrappers
  // and chrome state are in place when applyTabVisibility runs.
  applyDockOrientation();
  setActiveTab(collection.sidebar.sidebarActiveTab);
  // Mouse tracking is required so we can change the cursor over the grip
  // strip without waiting for a click.
  setMouseTracking(!m_widthLocked);
  if (m_widthLocked) {
    unsetCursor();
  }
  // install/uninstall ourselves as event filter on inner widgets
  // so grip-zone clicks that would otherwise be eaten by them get routed back
  // here. Done every applyAppearance to handle the lock toggle. Mouse tracking
  // is enabled on these widgets too so cursor hints work without a click.
  const auto installFilter = [this](QWidget *w) {
    if (!w) return;
    if (m_widthLocked) {
      w->removeEventFilter(this);
      w->setMouseTracking(false);
      w->unsetCursor();
    } else {
      w->installEventFilter(this);
      w->setMouseTracking(true);
    }
  };
  if (ui->scrollArea) {
    installFilter(ui->scrollArea);
    installFilter(ui->scrollArea->viewport());
  }
  installFilter(ui->contentWidget);
  // in horizontal dock the.ui's scrollArea is hidden and the
  // dedicated m_horizontalView (plus its children) sits in front of every
  // pixel of the pane — without filtering them, grip-zone clicks get eaten
  // by the horizontal-view children and the resize handle effectively
  // doesn't exist in T/B + Horizontal-scrolling mode (bug). We
  // walk findChildren so any future widget added in setupHorizontalView is
  // covered without having to repeat their names here.
  if (m_horizontalView) {
    installFilter(m_horizontalView);
    for (QWidget *w : m_horizontalView->findChildren<QWidget *>()) {
      installFilter(w);
    }
  }
  m_bgType = collection.sidebar.sidebarBackgroundType;
  m_bgColor = QColor(collection.sidebar.sidebarBackgroundColor);
  m_bgPattern = collection.sidebar.sidebarPattern;
  m_patternIntensity = std::clamp(collection.sidebar.sidebarPatternIntensity, 0, 100);
  m_patternColor = QColor(collection.sidebar.sidebarPatternColor);
  // Decode the sidebar background off the GUI thread so a large wallpaper no
  // longer blocks the UI on every collection switch. Clear immediately (so the
  // previous collection's image doesn't linger under the new colour/pattern)
  // and bump a generation so a rapid switch's slow decode can't overwrite the
  // current collection's background when it finally lands.
  //
  // Kartend-gzptz: a colour-only re-theme (system-accent change) passes
  // reloadBackground=false — the wallpaper can't have changed, so re-running the
  // off-thread decode is pure waste, and the clear-then-reload below would flash
  // the sidebar blank until the decode lands. Skip it and keep the cached
  // pixmap; the colour/pattern/accent work below still re-applies.
  if (reloadBackground) {
    m_bgImage = QPixmap();
    const int bgGen = ++m_bgImageGeneration;
    if (ExtensionUtils::isDecodableImagePath(collection.sidebar.sidebarBackgroundImage)) {
      const QString bgPath = collection.sidebar.sidebarBackgroundImage;
      QPointer<DetailsPane> self = this;
      auto *watcher = new QFutureWatcher<QImage>(this);
      connect(watcher, &QFutureWatcherBase::finished, this, [self, watcher, bgGen]() {
        watcher->deleteLater();
        if (!self || self->m_bgImageGeneration != bgGen) return;
        const QImage img = watcher->result();
        self->m_bgImage = img.isNull() ? QPixmap() : QPixmap::fromImage(img);
        self->update();
      });
      watcher->setFuture(
          QtConcurrent::run([bgPath]() { return ImageDecodeUtils::loadCapped(bgPath); }));
    }
    // Empty or non-image path leaves m_bgImage cleared — never hand it to
    // QPixmap's autodetect, which would route a .pdf to the abort()-prone PDF
    // image plugin.
  }

  // Make children transparent so the painted background shows through.
  // The default .ui sets autoFillBackground on each of these so the sidebar
  // draws as a solid palette(Window); flipping them off lets paintEvent
  // own the visual.
  setAutoFillBackground(false);
  if (ui->scrollArea) {
    ui->scrollArea->setAutoFillBackground(false);
    if (auto *vp = ui->scrollArea->viewport()) {
      vp->setAutoFillBackground(false);
      vp->setStyleSheet("background: transparent;");
    }
  }
  if (ui->contentWidget) {
    ui->contentWidget->setAutoFillBackground(false);
  }

  // Plumb text + accent color through the contentWidget palette so the
  // existing palette(highlight) / palette(windowtext) stylesheet hooks pick
  // them up without re-styling each label.
  if (ui->contentWidget) {
    // Start from a default-constructed palette (empty resolve mask) rather
    // than the widget's current one. Only the roles a collection actually
    // overrides get pinned; every other role is left unset so it keeps
    // inheriting the live application palette. This is what lets the system
    // accent reach contentWidget's Highlight when the collection has no custom
    // accent — and clears a custom accent pinned by a *previous* collection
    // when switching to one that follows the system accent. (A re-run of this
    // pass on QEvent::ApplicationPaletteChange therefore re-syncs the pane to
    // a runtime accent change; see MainWindow::reapplyDerivedThemingFromSystemPalette.)
    QPalette pal;
    if (!collection.sidebar.sidebarTextColor.isEmpty()) {
      const QColor textColor(collection.sidebar.sidebarTextColor);
      if (textColor.isValid()) {
        pal.setColor(QPalette::WindowText, textColor);
        pal.setColor(QPalette::Text, textColor);
      }
    }
    if (!collection.sidebar.sidebarAccentColor.isEmpty()) {
      const QColor accent(collection.sidebar.sidebarAccentColor);
      if (accent.isValid()) {
        pal.setColor(QPalette::Highlight, accent);
      }
    }
    ui->contentWidget->setPalette(pal);
  }

  // per-collection sidebar font override. Layered on top of the
  // designer-set baseline so reverting (empty family + 0 size) restores the
  // original .ui look without us walking detailspane.ui at runtime.
  applySidebarFont(collection.sidebar.sidebarFontFamily, collection.sidebar.sidebarFontPointSize);

  // bubble backgrounds for readability over patterned bg.
  // The bubble color is RGB-only; the user-controlled opacity is layered
  // on top via the matching *Opacity field. When the color is blank, fall
  // back to a sensible default derived from the other sidebar colors so
  // bubbles always give some contrast without forcing a manual pick:
  //   • Header → accent color (or palette highlight).
  //   • Section → darker variant of the sidebar background color (or
  //     palette window darkened).
  auto composeBubble = [&](const QString &hex, int opacity, const QColor &fallback) -> QColor {
    QColor c = hex.isEmpty() ? fallback : QColor(hex);
    if (!c.isValid()) c = fallback;
    c.setAlpha(std::clamp(opacity, 0, 255));
    return c;
  };
  // Section fallback uses palette(Mid) — same color the missing-artwork
  // item placeholder paints as its bg, so the value rows tint to match the
  // item tiles. Header fallback is a slightly darker shade for hierarchy
  // *and* readability: a Highlight-colored header on top of a brighter
  // Highlight-derived bubble made the windowtext text disappear on dark
  // themes. A darker Mid keeps light text legible.
  QColor sectionFallback = QColor(collection.sidebar.sidebarBackgroundColor);
  if (!sectionFallback.isValid()) sectionFallback = palette().color(QPalette::Mid);
  const QColor headerFallback = sectionFallback.darker(115);

  const QColor header = composeBubble(collection.sidebar.sidebarHeaderBgColor,
                                      collection.sidebar.sidebarHeaderBgOpacity, headerFallback);
  const QColor section = composeBubble(collection.sidebar.sidebarSectionBgColor,
                                       collection.sidebar.sidebarSectionBgOpacity, sectionFallback);
  applyBubbleStyles(header.alpha() == 0 ? QString() : header.name(QColor::HexArgb),
                    section.alpha() == 0 ? QString() : section.name(QColor::HexArgb));

  // re-anchor every layout item to AlignHCenter after the
  // appearance pass. The bubble stylesheet's polish step + any sections
  // inserted since the last call would otherwise drop back to flush-left.
  applyContentAlignment();

  update();
}
void DetailsPane::paintEvent(QPaintEvent *event) {
  QPainter painter(this);
  const QColor fallbackBase = palette().color(QPalette::Window);
  const QColor baseColor = m_bgColor.isValid() ? m_bgColor : fallbackBase;

  switch (m_bgType) {
  case DetailsPaneBackgroundType::Color:
    painter.fillRect(rect(), baseColor);
    break;
  case DetailsPaneBackgroundType::Image:
    painter.fillRect(rect(), baseColor);
    if (!m_bgImage.isNull()) {
      // Scale to cover the sidebar while preserving aspect ratio. Centered
      // crop matches the main-view background-position: center semantics.
      const QSize target = size();
      QPixmap scaled =
          m_bgImage.scaled(target, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
      const int x = (target.width() - scaled.width()) / 2;
      const int y = (target.height() - scaled.height()) / 2;
      painter.drawPixmap(x, y, scaled);
    }
    break;
  case DetailsPaneBackgroundType::Pattern: {
    // Single full-size pattern (no tiling, no vertical gradient).
    // Pass our own palette Mid so the pattern picks up any palette overrides
    // inherited from an ancestor (itemsPage / m_mainContentWidget). Without
    // this the static helper used QApplication::palette() Mid, which on
    // some setups is visibly lighter than the items grid's effective Mid.
    const QColor mid = palette().color(QPalette::Mid);
    // Cache key encodes everything that affects the rendered tile:
    // width, height, intensity, palette Mid color. If any changes, we
    // rebuild; otherwise we reuse the cached pixmap. Without this cache
    // every sidebar repaint (which fires during scroll due to damage-
    // event cascade from sibling widgets) re-rasterized the full sidebar
    // diagonal-hatching pattern via a fresh QPainter pass — the dominant
    // user-perceived scroll judder source on collections that use the
    // pattern background (Kartend-9q8d round 5).
    const int curW = width();
    const int curH = height();
    const QRgb midRgba = mid.rgba();
    if (m_cachedPatternTile.isNull() || m_cachedPatternW != curW || m_cachedPatternH != curH ||
        m_cachedPatternIntensity != m_patternIntensity || m_cachedPatternMidRgba != midRgba) {
      m_cachedPatternTile =
          ItemWidget::buildPlaceholderTile(curW, curH, /*cornerRadius=*/0,
                                           /*applyGradient=*/false, mid,
                                           /*lineAlphaScale=*/m_patternIntensity / 100.0);
      m_cachedPatternW = curW;
      m_cachedPatternH = curH;
      m_cachedPatternIntensity = m_patternIntensity;
      m_cachedPatternMidRgba = midRgba;
    }
    if (!m_cachedPatternTile.isNull()) {
      painter.drawPixmap(0, 0, m_cachedPatternTile);
    } else {
      painter.fillRect(rect(), baseColor);
    }
    // Optional user-controlled tint on top. Lines are already dimmed via
    // lineAlphaScale=0.5 above, so no default overlay is needed; users who
    // want extra darkening can set sidebarPatternColor.
    if (m_patternColor.isValid() && m_patternColor.alpha() > 0) {
      painter.fillRect(rect(), m_patternColor);
    }
    break;
  }
  }

  // paint a guideline on the inner (grid-facing)
  // edge while the resize lock is off so the user can see where to grab.
  // Skipped during the live drag so the line doesn't smear. A 1px line was
  // too easy to miss on T/B docks, where the items area's horizontal
  // scrollbar lives right next to the grip — bump it to a 2px band and
  // overlay a short central tick so the handle reads as a deliberate
  // affordance rather than a stray highlight pixel.
  if (!m_widthLocked && m_resizeGrip && !m_resizeGrip->isDragging()) {
    QColor edgeColor = palette().color(QPalette::Highlight);
    edgeColor.setAlpha(180);
    QColor tickColor = edgeColor;
    tickColor.setAlpha(255);
    const int w = width();
    const int h = height();
    auto drawHandle = [&](int x1, int y1, int x2, int y2, bool horizontalEdge) {
      painter.setPen(QPen(edgeColor, 2));
      painter.drawLine(x1, y1, x2, y2);
      // Center tick: a short perpendicular bar (~24px) drawn full-alpha so
      // the handle is visible even when the band sits behind a scrollbar
      // shadow on dark themes.
      painter.setPen(QPen(tickColor, 2));
      const int tickHalf = 12;
      if (horizontalEdge) {
        const int cx = w / 2;
        painter.drawLine(cx - tickHalf, y1, cx + tickHalf, y1);
      } else {
        const int cy = h / 2;
        painter.drawLine(x1, cy - tickHalf, x1, cy + tickHalf);
      }
    };
    switch (m_position) {
    case DetailsPanePosition::Left:
      drawHandle(w - 1, 0, w - 1, h, /*horizontalEdge=*/false);
      break;
    case DetailsPanePosition::Top:
      drawHandle(0, h - 1, w, h - 1, /*horizontalEdge=*/true);
      break;
    case DetailsPanePosition::Bottom:
      drawHandle(0, 0, w, 0, /*horizontalEdge=*/true);
      break;
    case DetailsPanePosition::Right:
    default:
      drawHandle(0, 0, 0, h, /*horizontalEdge=*/false);
      break;
    }
  }

  QWidget::paintEvent(event);
}

void DetailsPane::captureLabelFontBaselines() {
  // Sweep every current QLabel in the subtree and record any that we haven't
  // seen yet. Each label's *current* font is taken as its baseline — this is
  // correct because we only call this from applySidebarFont before applying
  // the override, so the snapshot is the un-overridden designer/code font.
  // Dead pointers from previous item changes are dropped here so the list
  // doesn't grow unbounded across many selection changes.
  m_labelFontBaselines.removeIf([](const LabelFontBaseline &b) { return b.label.isNull(); });
  QSet<QLabel *> known;
  known.reserve(m_labelFontBaselines.size());
  for (const auto &b : m_labelFontBaselines) {
    if (b.label) {
      known.insert(b.label.data());
    }
  }
  const auto labels = findChildren<QLabel *>();
  for (QLabel *lbl : labels) {
    if (lbl && !known.contains(lbl)) {
      m_labelFontBaselines.append({QPointer<QLabel>(lbl), lbl->font()});
    }
  }
  m_labelFontBaselinesCaptured = true;
}
void DetailsPane::applySidebarFont(const QString &family, int pointSize) {
  m_activeSidebarFontFamily = family.trimmed();
  m_activeSidebarFontPointSize = pointSize;
  captureLabelFontBaselines();
  // scale the override (or each label's baseline pointSize when
  // the user hasn't picked one) by the active text-zoom multiplier so the
  // sidebar tracks Ctrl+= / Ctrl+- alongside the rest of the UI.
  const int zoomedOverride = TextZoom::zoomedFontSize(m_activeSidebarFontPointSize);
  for (const auto &baseline : m_labelFontBaselines) {
    QLabel *lbl = baseline.label.data();
    if (!lbl) {
      continue;
    }
    QFont f = baseline.font;
    if (!m_activeSidebarFontFamily.isEmpty()) {
      f.setFamily(m_activeSidebarFontFamily);
    }
    if (zoomedOverride > 0) {
      f.setPointSize(zoomedOverride);
    } else if (baseline.font.pointSize() > 0) {
      // No explicit override → scale the baseline so the title-vs-body
      // hierarchy from the .ui file is preserved while still tracking zoom.
      f.setPointSize(TextZoom::zoomedFontSize(baseline.font.pointSize()));
    }
    if (lbl->font() != f) {
      lbl->setFont(f);
    }
  }
}

void DetailsPane::applyBubbleStyles(const QString &headerHex, const QString &sectionHex) {
  if (!ui->contentWidget) {
    return;
  }
  // Build a stylesheet that selects the existing static-label objectNames in
  // detailspane.ui. Dynamic detail rows are tagged via objectName at
  // creation in appendDetailRow / ensureGallerySection so they pick up the
  // same bubble. Empty hex disables that bubble (no rule emitted).
  QString sheet;
  const auto qcolorOrEmpty = [](const QString &hex) {
    if (hex.isEmpty()) return QColor();
    QColor c(hex);
    return c.isValid() ? c : QColor();
  };
  const QColor headerColor = qcolorOrEmpty(headerHex);
  const QColor sectionColor = qcolorOrEmpty(sectionHex);

  if (headerColor.isValid()) {
    const QString rgba = QString("rgba(%1,%2,%3,%4)")
                             .arg(headerColor.red())
                             .arg(headerColor.green())
                             .arg(headerColor.blue())
                             .arg(headerColor.alpha());
    // titleBar wraps the title label + item name in one row — applying
    // the bubble to the QFrame paints a single backdrop behind both
    // children instead of two separate pills. Other header labels keep
    // their per-label bubble. The titleRow QHBoxLayout inside titleBar
    // now uses zero margins so the bubble's own padding defines the
    // inner spacing — previously both contributed and the total
    // 8+8px vertical padding read as too thick on unscraped items
    // where the title bar is the only thing in the sidebar.
    sheet += QString("QFrame#titleBar, QLabel#fileInfoTitle, "
                     "QLabel[sidebarRole=\"header\"] { "
                     "background-color: %1; border-radius: 6px; padding: 2px 6px; }")
                 .arg(rgba);
  }
  if (sectionColor.isValid()) {
    const QString rgba = QString("rgba(%1,%2,%3,%4)")
                             .arg(sectionColor.red())
                             .arg(sectionColor.green())
                             .arg(sectionColor.blue())
                             .arg(sectionColor.alpha());
    // itemNameValue dropped from this rule — it now lives inside the
    // titleBar frame which carries the header bubble. Other static
    // value labels keep the section bubble. QScrollArea is included
    // so the scrolling description (sidebarRole="value" on the scroll
    // area) gets the same backdrop as the static value pills; the
    // inner viewport + label use the descendant rule to stay
    // transparent so the bubble color shows through.
    // Asymmetric horizontal padding (left > right) so the bold key
    // prefix has a touch of breathing room from the bubble edge.
    // Kept tight so the rows don't waste vertical / horizontal space.
    sheet += QString("QLabel#filePathValue, QLabel#fileSizeValue, "
                     "QLabel#lastModifiedValue, QLabel#fileExtensionValue, "
                     "QLabel[sidebarRole=\"value\"], "
                     "QScrollArea[sidebarRole=\"value\"] { "
                     "background-color: %1; border-radius: 6px; padding: 2px 4px 2px 6px; } "
                     "QScrollArea[sidebarRole=\"value\"] > QWidget { background: transparent; } "
                     "QScrollArea[sidebarRole=\"value\"] QLabel { "
                     "background: transparent; border: none; }")
                 .arg(rgba);

    // Metadata backdrop sits behind the post-description rows. Render
    // it a noticeably darker shade of the section color (and slightly
    // more opaque) so the inner row pills float on a recognisable
    // card. Using `darker(140)` on RGB lands ~30% darker which reads
    // as a distinct second layer without losing the section tint.
    const QColor backdropColor = sectionColor.darker(140);
    const QString backdropRgba = QString("rgba(%1,%2,%3,%4)")
                                     .arg(backdropColor.red())
                                     .arg(backdropColor.green())
                                     .arg(backdropColor.blue())
                                     .arg(qMin(255, sectionColor.alpha() + 30));
    sheet += QString("QFrame#metadataBackdrop { "
                     "background-color: %1; border-radius: 8px; }")
                 .arg(backdropRgba);
    // Gallery section gets the same section bubble color as the
    // value pills. Matching tone (rather than the darker backdrop
    // shade) keeps the visual hierarchy clear: gallery is a
    // sibling pill of the value pills, not the deeper metadata
    // card below.
    sheet += QString("QWidget#galleryBackdrop { "
                     "background-color: %1; border-radius: 8px; }")
                 .arg(rgba);
  }
  ui->contentWidget->setStyleSheet(sheet);
  // Force a polish so newly-applied stylesheet rules take effect on already
  // visible labels — without this, the first switch from blank → set bg
  // sometimes leaves the labels unstyled until the next layout event.
  ui->contentWidget->style()->polish(ui->contentWidget);
}
