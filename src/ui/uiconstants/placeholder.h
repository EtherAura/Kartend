#ifndef UICONSTANTS_PLACEHOLDER_H
#define UICONSTANTS_PLACEHOLDER_H

namespace UIConstants {

// =============================================================================
// Placeholder Pattern
// Constants for generating placeholder artwork with deterministic patterns.
// =============================================================================
namespace Placeholder {
/// Color delta for primary pattern in dark mode
inline constexpr int PRIMARY_DELTA_DARK = 300;
/// Color delta for primary pattern in light mode
inline constexpr int PRIMARY_DELTA_LIGHT = -105;
/// Color delta for secondary pattern in dark mode
inline constexpr int SECONDARY_DELTA_DARK = 55;
/// Color delta for secondary pattern in light mode
inline constexpr int SECONDARY_DELTA_LIGHT = -55;
/// Alpha for primary pattern color
inline constexpr int PRIMARY_ALPHA = 215;
/// Alpha for secondary pattern color
inline constexpr int SECONDARY_ALPHA = 125;
/// Minimum step size for pattern generation
inline constexpr int STEP_MIN = 5;
/// Maximum step size for pattern generation
inline constexpr int STEP_MAX = 12;
/// Divisor for step calculation
inline constexpr int STEP_DIVISOR = 10;
/// Amplitude for noise variation
inline constexpr int NOISE_AMPLITUDE = 3;
/// Stride for noise sampling
inline constexpr int NOISE_STRIDE = 4;
/// Seed for deterministic noise generation
inline constexpr quint32 NOISE_SEED = 0x51A3D7U;
/// Mask for noise value extraction
inline constexpr int NOISE_MASK = 0x07;
/// Bias added to noise values
inline constexpr int NOISE_BIAS = 3;
/// Alpha for top gradient overlay
inline constexpr int GRADIENT_TOP_ALPHA = 28;
/// Alpha for bottom gradient overlay
inline constexpr int GRADIENT_BOTTOM_ALPHA = 12;
/// Numerator for primary tint blend ratio
inline constexpr int PRIMARY_TINT_NUM = 3;
/// Denominator for primary tint blend ratio
inline constexpr int PRIMARY_TINT_DEN = 5;
/// Numerator for secondary tint blend ratio
inline constexpr int SECONDARY_TINT_NUM = 4;
/// Denominator for secondary tint blend ratio
inline constexpr int SECONDARY_TINT_DEN = 5;
} // namespace Placeholder
} // namespace UIConstants

#endif
