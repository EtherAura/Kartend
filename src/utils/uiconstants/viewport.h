#ifndef UICONSTANTS_VIEWPORT_H
#define UICONSTANTS_VIEWPORT_H

namespace UIConstants {

// =============================================================================
// Viewport
// Main content area sizing constraints.
// =============================================================================
namespace Viewport {
/// Minimum viewport width in pixels
inline constexpr int MIN_WIDTH = 600;
/// Default viewport width in pixels
inline constexpr int DEFAULT_WIDTH = 1200;
/// Minimum stored spacing adjustment (negative = tighter/overlap)
/// Grid spacing is the GAP BETWEEN TILE EDGES, so it floors at zero
/// (Kartend-hxly2). It was -100 back when a negative value meant the grid
/// packed cells closer than their own size; CollectionConfig::clampValues
/// migrates any config still carrying one.
inline constexpr int SPACING_MIN = 0;
/// Maximum stored spacing adjustment (positive = looser)
inline constexpr int SPACING_MAX = 50;
} // namespace Viewport
} // namespace UIConstants

#endif
