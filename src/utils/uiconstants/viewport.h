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
inline constexpr int SPACING_MIN = -100;
/// Maximum stored spacing adjustment (positive = looser)
inline constexpr int SPACING_MAX = 50;
} // namespace Viewport
} // namespace UIConstants

#endif
