#ifndef PLACEHOLDERWARMER_H
#define PLACEHOLDERWARMER_H

#include <functional>

#include <QImage>
#include <QString>
#include <QStringList>

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
  /// Kartend-qe9a: true when the run stopped early because the cancel
  /// predicate fired; the counts reflect work done up to that point.
  bool cancelled = false;
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
/// QImage. Kartend-qe9a: QImage (not QPixmap) so the render runs off the GUI
/// thread; the production caller snapshots the theme on the GUI thread and
/// renders via ItemPlaceholderRenderer::buildTileImage so warmed PNGs match
/// the live grid. Tests pass a stub so they don't need a QApplication.
using TileFactory = std::function<QImage(int width, int height, int cornerRadius)>;

/// Progress callback: (itemsScanned, itemsExported) so far. Kartend-qe9a:
/// invoked periodically during the walk so a worker run can drive a progress
/// UI. Called on the thread exportMissingPlaceholders runs on (a worker), so
/// the caller must marshal to the GUI thread itself. Empty = no reporting.
using ProgressFn = std::function<void(qint64 scanned, qint64 exported)>;

/// Cancel predicate: return true to stop the walk early (Kartend-qe9a). Polled
/// once per item; must be safe to call from the worker thread (e.g. read an
/// atomic). Empty = never cancel.
using CancelFn = std::function<bool()>;

/// Run the export. Caller is expected to have called `preflight` and bailed
/// on a non-None error. @p artworkDirectoryOverride mirrors `preflight`.
/// @p tileWidth/tileHeight/cornerRadius come from CollectionConfig fields
/// of the same name so generated PNGs match what the live grid renders.
/// @p tileFactory builds the image (see TileFactory). @p onProgress and
/// @p isCancelled (Kartend-qe9a) let a worker run report progress and bail
/// early; both are optional. Safe to run on a worker thread — it touches only
/// the filesystem and the supplied callbacks.
[[nodiscard]] Result exportMissingPlaceholders(const CollectionConfig &collection,
                                               const QString &artworkDirectoryOverride,
                                               int tileWidth, int tileHeight, int cornerRadius,
                                               const TileFactory &tileFactory,
                                               const ProgressFn &onProgress = {},
                                               const CancelFn &isCancelled = {});

} // namespace PlaceholderWarmer

#endif // PLACEHOLDERWARMER_H
