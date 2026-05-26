#ifndef TEXTZOOM_H
#define TEXTZOOM_H

// Process-wide UI text zoom percentage. Owns the global zoom state so widget
// modules (item factory, virtual scroll engine, cover-flow renderer, sidebar
// painter) can apply zoom to per-widget font sizes without dragging in
// MainWindow's full header. MainWindow still owns the user-facing zoom
// shortcuts and persistence; it forwards to these helpers internally.

namespace TextZoom {

// Bounds for the configurable text-zoom percentage. setPercent() and
// primeFromSettings() clamp inputs into [MIN_PERCENT, MAX_PERCENT]; callers
// that present the value in UI (settings dialog, HUD readout) should reuse
// these constants instead of hard-coding 50 / 300.
inline constexpr int DEFAULT_PERCENT = 100;
inline constexpr int MIN_PERCENT = 50;
inline constexpr int MAX_PERCENT = 300;

// Returns the configured zoom percentage. Always between MIN_PERCENT and
// MAX_PERCENT.
[[nodiscard]] int percent();

// Initialise the percent from settings before any widget is built. Clamps
// the input to [50, 300]. Does NOT trigger refreshes — applyTextZoom() in
// MainWindow is the runtime entry point that actually updates widgets.
void primeFromSettings(int percent);

// Set the runtime zoom percent. Clamped to [50, 300]. Returns the clamped
// value. Caller is responsible for refreshing widgets after a change.
int setPercent(int percent);

// Return the zoom-scaled font size for a baseline value. Returns baseSize
// unchanged when zoom is 100% or baseSize is non-positive. Floors to 1 to
// avoid Qt's font system mishandling pointSize=0 on some platforms.
[[nodiscard]] int zoomedFontSize(int baseSize);

} // namespace TextZoom

#endif // TEXTZOOM_H
