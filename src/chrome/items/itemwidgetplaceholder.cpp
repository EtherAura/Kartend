// Sibling translation unit for ItemWidget: thin wrappers that thread
// ItemWidget's static theme configuration into the pure
// ItemPlaceholderRenderer helpers. The cached hatch-tile builder + title
// overlay live in itemplaceholderrenderer.{h,cpp}; this TU exists so the
// public ItemWidget::buildPlaceholderTile static API stays callable from
// places that don't want to know about the renderer namespace
// (DetailsPane's pattern background, in particular).
#include "itemplaceholderrenderer.h"
#include "itemwidget.h"

#include <QApplication>
#include <QColor>
#include <QLabel>

ItemPlaceholderRenderer::TileTheme ItemWidget::currentTileTheme() {
  ItemPlaceholderRenderer::TileTheme theme;
  if (!s_tileColor.isEmpty() && QColor::isValidColorName(s_tileColor)) {
    theme.tileColor = QColor(s_tileColor);
  }
  theme.accentColor = titleTint();
  return theme;
}

auto ItemWidget::buildPlaceholderTile(int width, int height, int cornerRadius, bool applyGradient,
                                      const QColor &baseOverride, double lineAlphaScale)
    -> QPixmap {
  return ItemPlaceholderRenderer::buildTile(width, height, cornerRadius, applyGradient,
                                            baseOverride, lineAlphaScale, currentTileTheme());
}

// Instance wrapper used by per-item rendering — delegates with the widget's
// own corner radius.
auto ItemWidget::buildPlaceholderPattern(int width, int height) const -> QPixmap {
  return ItemWidget::buildPlaceholderTile(width, height, m_cornerRadius);
}

// Render the item title onto a placeholder pixmap. The placeholder hatch
// and any user-supplied placeholder image both go through this so the
// overlay is consistent across both paths. Painted on a copy of the cached
// tile (the renderer's cache is shared across items).
void ItemWidget::drawTitleOnPlaceholder(QPixmap &pixmap, qreal dpr) const {
  if (!s_showTitleInPlaceholder) {
    return;
  }
  ItemPlaceholderRenderer::TitleStyle style;
  style.customFontFamily = s_customFontFamily;
  // Title text drawn ON the placeholder tile follows the TEXT colour; the
  // tile's hatch accent above deliberately still uses titleTint().
  style.tintColor = m_titleTintColor.isValid() ? m_titleTintColor : titleTextColor();
  // Match the on-tile font to the item's nameLabel font when available so
  // the overlay reads as the title's natural typeface; fall back to the
  // app font when nameLabel hasn't been laid out yet (e.g. early reuse
  // from the pool).
  const QFont baseFont = nameLabel ? nameLabel->font() : QApplication::font();
  ItemPlaceholderRenderer::drawTitle(pixmap, itemName, baseFont, style, dpr);
}
