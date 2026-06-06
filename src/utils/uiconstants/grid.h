#ifndef UICONSTANTS_GRID_H
#define UICONSTANTS_GRID_H

namespace UIConstants {

// =============================================================================
// Grid Layout
// Virtual scrolling grid configuration and container sizing.
// =============================================================================
namespace Grid {
/// Default number of items per row
inline constexpr int DEFAULT_WIDTH = 7;
/// Minimum items per row
inline constexpr int MIN_WIDTH = 1;
// No project-level maximum: grid width is unbounded for large displays
// (4K/8K wall mounts, kiosk layouts). QSpinBox::setMaximum uses INT_MAX at
// the call sites; the pixel-dimension safety net in
// GridLayoutCalculator (kQtMaxWidgetSize - 1000) is what actually catches
// runaway values, not a column-count cap.
/// Spacing between grid items in pixels
inline constexpr int SPACING = 20;
/// Margins around the grid in pixels
inline constexpr int MARGINS = 10;
/// Extra rows to render above/below viewport for smooth scrolling
inline constexpr int BUFFER_ROWS = 2;
/// Default number of visible rows for pool sizing
inline constexpr int DEFAULT_VISIBLE_ROWS = 6;
/// Y offset for virtual container positioning
inline constexpr int CONTAINER_OFFSET = -20;
/// Left offset for virtual container
inline constexpr int CONTAINER_LEFT_OFFSET = 20;
/// Right offset for virtual container
inline constexpr int CONTAINER_RIGHT_OFFSET = 16;
/// Extra height buffer for container sizing
inline constexpr int CONTAINER_HEIGHT_BUFFER = 25;
} // namespace Grid
} // namespace UIConstants

#endif
