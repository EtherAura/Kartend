#ifndef ITEMPLACEHOLDERRENDERER_H
#define ITEMPLACEHOLDERRENDERER_H

#include <QColor>
#include <QFont>
#include <QPixmap>
#include <QString>

/// Pure rendering helpers for ItemWidget's "missing artwork" tile and the
/// optional centred title overlay that goes on top of it. Stateless — every
/// piece of theme data the helpers need is passed in. The hatched-tile
/// builder keeps a private size-keyed cache (cleared automatically when the
/// theme tile color changes) so the sidebar's pattern background and the
/// item-card placeholder can share decoded pixmaps without thrashing.
namespace ItemPlaceholderRenderer {

/// Theme inputs threaded through buildTile. Both colors come from
/// ItemWidget's static configuration; passing them in keeps the renderer
/// free of singleton lookups.
struct TileTheme {
  /// Per-collection background color (empty / invalid = use the palette's
  /// Mid color instead).
  QColor tileColor;
  /// Tint mixed into the diagonal hatch lines — picks up the per-collection
  /// title color so the placeholder reads as a member of the active theme.
  QColor accentColor;
};

/// Build a hatched placeholder tile of @p width × @p height. @p cornerRadius
/// masks the corners to match a rounded artwork preview; pass 0 for a sharp
/// rectangle. @p applyGradient adds the legacy top-to-bottom alpha fade
/// (item cards keep it; long sidebar pattern backgrounds disable it so the
/// fade doesn't get stretched). @p baseOverride lets a caller substitute
/// its own widget-instance Mid color (default = QApplication::palette()
/// Mid) — pass an invalid color to use the default. @p lineAlphaScale
/// (0.0–1.0) scales both primary + secondary line alphas; the sidebar
/// dims to ~0.5 because lines run much longer at sidebar scale.
[[nodiscard]] QPixmap buildTile(int width, int height, int cornerRadius, bool applyGradient,
                                const QColor &baseOverride, double lineAlphaScale,
                                const TileTheme &theme);

/// Title-overlay inputs threaded through drawTitle.
struct TitleStyle {
  /// Optional custom font family override. Empty = inherit from baseFont.
  QString customFontFamily;
  /// Pre-resolved tint color for the title text. Caller picks the
  /// per-item cached tint or the global titleTint() default.
  QColor tintColor;
};

/// Paint @p itemName centered on @p pixmap with the given title tint and a
/// translucent dark wash for legibility. No-op when itemName is empty or
/// the pixmap has zero area. @p baseFont supplies the default family +
/// size (typically the item's nameLabel font); @p dpr scales the point
/// size when the pixmap is in physical pixels (placeholder pattern uses
/// dpr=1, the scaled-artwork path uses the screen DPR).
void drawTitle(QPixmap &pixmap, const QString &itemName, const QFont &baseFont,
               const TitleStyle &style, qreal dpr);

} // namespace ItemPlaceholderRenderer

#endif // ITEMPLACEHOLDERRENDERER_H
