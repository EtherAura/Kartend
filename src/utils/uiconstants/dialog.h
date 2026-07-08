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
/// Preferred (initial) dialog size; the dialog reflows responsively when
/// the user resizes (narrower → fewer chips per row, wider → more). Sized so
/// the whole setup view — collection tree + items list, the 3-column
/// "What to scrape" grid, and the scrape-options panel — fits without any
/// scrollbar (Kartend-1hose).
inline constexpr int DEFAULT_WIDTH = 1200;
inline constexpr int DEFAULT_HEIGHT = 920;
/// Hard lower bound so the dialog stays usable on low-res screens while
/// still letting the user shrink it below the preferred size. Tall enough that
/// the scrape controls still fit under the tree/list without clipping.
inline constexpr int MIN_WIDTH = 780;
inline constexpr int MIN_HEIGHT = 700;
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
/// Kartend-hg07v: promoted from bare literals in scraperesultdialogunified.cpp.
/// Uniform content margin for the root + metadata section containers.
inline constexpr int CONTENT_MARGIN = 8;
/// Post-scrape section container margins (horizontal / vertical).
inline constexpr int POST_SECTION_H_MARGIN = 12;
inline constexpr int POST_SECTION_V_MARGIN = 14;
/// Live-thumbnail strip inner margin + per-tile icon / grid cell sizes.
inline constexpr int THUMBS_STRIP_MARGIN = 4;
inline constexpr int THUMB_ICON_SIZE = 96;
inline constexpr int THUMB_GRID_SIZE = 100;
} // namespace ScrapeResultDialog

// =============================================================================
// Statistics dialog (usage / play-history surface).
// =============================================================================
namespace StatisticsDialog {
/// Preferred (initial) dialog size.
inline constexpr int DEFAULT_WIDTH = 920;
inline constexpr int DEFAULT_HEIGHT = 640;
} // namespace StatisticsDialog

// =============================================================================
// Command palette (Spotlight-style command picker) — small + centered.
// =============================================================================
namespace CommandPaletteDialog {
/// Preferred (initial) dialog size.
inline constexpr int DEFAULT_WIDTH = 560;
inline constexpr int DEFAULT_HEIGHT = 380;
} // namespace CommandPaletteDialog

// =============================================================================
// Edit-item-metadata dialog.
// =============================================================================
namespace EditMetadataDialog {
/// Preferred (initial) dialog size.
inline constexpr int DEFAULT_WIDTH = 560;
inline constexpr int DEFAULT_HEIGHT = 520;
/// Fixed height of the notes field — keeps the dialog compact; the field
/// scrolls when the user writes past it.
inline constexpr int NOTES_FIELD_HEIGHT = 96;
} // namespace EditMetadataDialog
} // namespace UIConstants

#endif
