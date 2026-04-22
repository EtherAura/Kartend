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
} // namespace Metadata
} // namespace UIConstants

#endif
