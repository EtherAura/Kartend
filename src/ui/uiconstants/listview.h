#ifndef UICONSTANTS_LISTVIEW_H
#define UICONSTANTS_LISTVIEW_H

namespace UIConstants {

// =============================================================================
// List View
// Constants for list view mode (text-only, no artwork per item).
// =============================================================================
namespace ListView {
/// Default row height in list view mode (text only, no artwork)
inline constexpr int DEFAULT_ROW_HEIGHT = 32;
/// Minimum row height in list view
inline constexpr int MIN_ROW_HEIGHT = 20;
/// Maximum row height in list view
inline constexpr int MAX_ROW_HEIGHT = 100;
/// Left padding for text in list view
inline constexpr int TEXT_LEFT_PADDING = 16;
/// Right padding for text in list view
inline constexpr int TEXT_RIGHT_PADDING = 16;
/// Vertical spacing between list rows (0 = no gaps, eliminates background color
/// bleeding through)
inline constexpr int ROW_SPACING = 0;
/// Header row height for column headers
inline constexpr int HEADER_HEIGHT = 28;
/// Width of folder icon column for subcollections/virtual folders in list mode
inline constexpr int FOLDER_ICON_COLUMN_WIDTH = 20;
} // namespace ListView
} // namespace UIConstants

#endif
