#ifndef UICONSTANTS_SEARCH_H
#define UICONSTANTS_SEARCH_H

namespace UIConstants {

// =============================================================================
// Search
// Debounce intervals for search input and UI refocus timing.
// =============================================================================
namespace Search {
/// Debounce delay before executing search query
inline constexpr int DEBOUNCE_DELAY_MS = 120;
/// Debounce for rapid typing in search bar
inline constexpr int TYPING_DEBOUNCE_MS = 180;
/// Short delay before refocusing search bar
inline constexpr int REFOCUS_DELAY_SHORT_MS = 180;
/// Longer delay for refocus after complex operations
inline constexpr int REFOCUS_DELAY_LONG_MS = 400;
} // namespace Search
} // namespace UIConstants

#endif
