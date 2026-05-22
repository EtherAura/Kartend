#ifndef UICONSTANTS_DIALOG_H
#define UICONSTANTS_DIALOG_H

namespace UIConstants {

// =============================================================================
// Dialog Sizes
// Dimensions for application dialogs.
// =============================================================================
namespace Dialog {
/// Width of the About dialog
inline constexpr int ABOUT_WIDTH = 400;
/// Height of the About dialog
inline constexpr int ABOUT_HEIGHT = 200;
} // namespace Dialog

// =============================================================================
// Scrape result dialog (single-item + batch unified surface)
// Sizes used by the scraper result dialog's tree / metadata / candidate
// chrome. Kept together so a theme tweak in one place adjusts the whole
// dialog consistently.
// =============================================================================
namespace ScrapeResultDialog {
/// Minimum width of the left-side collection tree.
inline constexpr int COLLECTION_TREE_MIN_WIDTH = 220;
/// Outer layout spacing for the dialog root.
inline constexpr int ROOT_LAYOUT_SPACING = 8;
/// Inner layout spacing for metadata / candidate sections.
inline constexpr int SECTION_LAYOUT_SPACING = 6;
/// Tight row spacing for chip-style horizontal layouts.
inline constexpr int CHIP_ROW_SPACING = 4;
/// Width of metadata field labels (left column of the meta grid).
inline constexpr int META_LABEL_WIDTH = 90;
/// Width of metadata value chips (right column of the meta grid).
inline constexpr int META_VALUE_CHIP_WIDTH = 90;
/// Min/max height of the description text area.
inline constexpr int DESCRIPTION_MIN_HEIGHT = 80;
inline constexpr int DESCRIPTION_MAX_HEIGHT = 110;
/// Candidate-row "candidate" label minimum width (keeps the chip aligned).
inline constexpr int CANDIDATE_LABEL_MIN_WIDTH = 78;
/// Thumbnail strip maximum height + tile spacing.
inline constexpr int THUMBS_STRIP_MAX_HEIGHT = 108;
inline constexpr int THUMBS_STRIP_SPACING = 2;
} // namespace ScrapeResultDialog
} // namespace UIConstants

#endif
