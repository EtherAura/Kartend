#ifndef IARTWORKPREVIEWSCROLL_H
#define IARTWORKPREVIEWSCROLL_H

#include <QString>

/**
 * @brief Artwork/media-preview role of the scroll layer (Kartend-h1l8f).
 *
 * One of the six role interfaces IScrollManager unions: the expand-mode /
 * middle-click-peek preview overlay. Matches the SelectionDisplayManager-owned
 * ArtworkPreviewOverlay side of the implementation split.
 *
 * setArtworkPreviewGallery() is intentionally absent: its parameter type
 * (ArtworkPreviewOverlay::GalleryEntry) is a nested type from a ui/ header and
 * cannot reach this neutral api/ header. Its sole caller obtains the concrete
 * ScrollManager for that one call.
 *
 * Plain abstract class, not a QObject — ScrollManager derives QObject once,
 * through IScrollManager. Reached via ctx->scrollPreview().
 */
class IArtworkPreviewScroll {
public:
  virtual ~IArtworkPreviewScroll() = default;

  /// Check if the artwork preview overlay is currently visible.
  [[nodiscard]] virtual bool isArtworkPreviewVisible() const = 0;
  /// Hide the artwork preview overlay if visible (returns true if it was
  /// visible).
  virtual bool hideArtworkPreview() = 0;
  /// Show a video-first preview overlay; falls back to artwork when no video
  /// is found in @p videoDir. Returns true when a preview was shown, false
  /// when the item has neither a video nor artwork.
  virtual bool showMediaPreview(const QString &filePath, const QString &artworkDir,
                                const QString &videoDir) = 0;
};

#endif // IARTWORKPREVIEWSCROLL_H
