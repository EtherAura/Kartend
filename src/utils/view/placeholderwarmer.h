#ifndef PLACEHOLDERWARMER_H
#define PLACEHOLDERWARMER_H

#include <functional>

#include <QString>
#include <QStringList>

class QPixmap;
struct CollectionConfig;

/// Batch-export of procedural placeholder PNGs into a collection's artwork
/// directory.
///
/// Walks `mediaDirectory` recursively, filters by `collection.extensions`,
/// and for every item that does NOT already resolve to artwork (per
/// ArtworkUtils::findArtworkForFile) generates the same hatched placeholder
/// the live grid would render and writes it as `<basename>.png` into
/// `artworkDirectory`.
///
/// Why callers want this: the per-item placeholder is normally regenerated
/// lazily on every grid render. Exporting them once turns each into a real
/// file the user can replace with a real cover (drag-and-drop into the
/// artwork dir, then delete the .png) without having to disable the
/// procedural fallback.
///
/// Side effects: writes files into `artworkDirectory`. Skips items that
/// already have artwork. Does not touch the media tree.
namespace PlaceholderWarmer {

/// Counters + first-N failures from a single warmer run.
struct Result {
  qint64 itemsScanned = 0;
  qint64 itemsAlreadyHadArtwork = 0;
  qint64 itemsExported = 0;
  qint64 itemsFailed = 0;
  /// First handful of failure descriptions, capped so the UI summary stays
  /// short. Format: "<absolute media path>: <reason>".
  QStringList firstFailures;
};

/// Pre-flight problems detected before any file I/O happens. The dialog
/// uses these to refuse to run rather than producing a half-empty Result.
enum class PreflightError {
  None,
  EmptyMediaDirectory,
  EmptyArtworkDirectory,
  MediaDirectoryMissing,
  ArtworkDirectoryNotWritable,
  NoExtensionsConfigured,
};

/// Result of pre-flight validation. `error` is None when the warmer can
/// proceed; otherwise `humanMessage` is a translated, user-facing
/// explanation the dialog can show verbatim.
struct PreflightResult {
  PreflightError error = PreflightError::None;
  QString humanMessage;
  /// Resolved (expanded) absolute paths used by the run. Populated even on
  /// success so the confirmation dialog can echo them back.
  QString resolvedMediaDirectory;
  QString resolvedArtworkDirectory;
};

/// Validates the inputs without touching the filesystem beyond existence
/// checks + a writability probe on the artwork dir (creating it if needed).
/// Pass an explicit @p artworkDirectoryOverride when the UI's line edit may
/// differ from the saved CollectionConfig (the panel hasn't applied yet);
/// pass an empty override to fall back to `collection.artworkDirectory`.
[[nodiscard]] PreflightResult preflight(const CollectionConfig &collection,
                                        const QString &artworkDirectoryOverride);

/// Tile factory signature: (width, height, cornerRadius) -> ready-to-save
/// pixmap. The production caller passes ItemWidget::buildPlaceholderTile so
/// warmed PNGs match the live grid render exactly; tests pass a stub so
/// they don't need a QApplication or the widget statics.
using TileFactory = std::function<QPixmap(int width, int height, int cornerRadius)>;

/// Run the export. Caller is expected to have called `preflight` and bailed
/// on a non-None error. @p artworkDirectoryOverride mirrors `preflight`.
/// @p tileWidth/tileHeight/cornerRadius come from CollectionConfig fields
/// of the same name so generated PNGs match what the live grid renders.
/// @p tileFactory builds the pixmap (see TileFactory).
[[nodiscard]] Result exportMissingPlaceholders(const CollectionConfig &collection,
                                               const QString &artworkDirectoryOverride,
                                               int tileWidth, int tileHeight, int cornerRadius,
                                               const TileFactory &tileFactory);

} // namespace PlaceholderWarmer

#endif // PLACEHOLDERWARMER_H
