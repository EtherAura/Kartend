#ifndef UICONSTANTS_METADATA_H
#define UICONSTANTS_METADATA_H

namespace UIConstants {

// =============================================================================
// Metadata Display
// Metadata sidebar layout constants.
// =============================================================================
namespace Metadata {
/// Size for artwork preview in sidebar
inline constexpr int ARTWORK_SIZE = 200;
/// Hard cap on the dynamically-sized artwork preview tile in vertical
/// dock mode. previewBoxSize() returns the smaller of (viewport-20)
/// and this constant. The cap is set high enough that any reasonably
/// sized sidebar (300-700px) uses its full width; the scrolling
/// description + metadata card below absorb anything pushed past the
/// fold.
inline constexpr int ARTWORK_SIZE_MAX = 720;
/// Maximum length before truncating file paths
inline constexpr int PATH_TRUNCATE_LENGTH = 50;
/// Bytes per KB for file size display
inline constexpr int FILE_SIZE_KB = 1024;
/// Spacing between metadata sections
inline constexpr int SECTION_SPACING = 12;
/// Spacing after label text
inline constexpr int LABEL_SPACING = 4;
/// Spacing between value lines
inline constexpr int VALUE_SPACING = 2;
/// Indentation for value text
inline constexpr int VALUE_INDENT = 12;
/// Padding around value text
inline constexpr int VALUE_PADDING = 2;
/// Height of separator lines
inline constexpr int SEPARATOR_HEIGHT = 8;
/// Side length of an artwork gallery thumbnail. Sized to fit
/// 3 thumbs across the sidebar's MIN_WIDTH (150) and 4 across FIXED_WIDTH
/// (300) with the standard layout margins.
inline constexpr int GALLERY_THUMB_SIZE = 64;
/// Horizontal/vertical spacing between gallery thumbnails.
inline constexpr int GALLERY_THUMB_SPACING = 6;
/// Padding inside each gallery thumbnail button (around the pixmap).
inline constexpr int GALLERY_THUMB_PADDING = 4;
} // namespace Metadata
} // namespace UIConstants

#endif
