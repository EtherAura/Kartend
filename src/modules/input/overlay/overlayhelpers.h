#ifndef OVERLAYHELPERS_H
#define OVERLAYHELPERS_H

#include <QPoint>
#include <QRect>

// Pure helpers extracted from the overlay managers. The managers own
// QWidget + QPropertyAnimation graphs that aren't unit-testable on their
// own; the geometry math and the glide-duration policy live here so they
// can be exercised without a QWidget tree.
namespace OverlayHelpers {

// Glide animation duration for the selection overlay. Computes the
// pixel distance between two rect centers and converts it to a duration
// in milliseconds at `pixelsPerSecond`, then clamps to [minMs, maxMs].
// A negative pixelsPerSecond is treated as the max duration — the math
// would otherwise produce a negative time.
[[nodiscard]] int glideDurationMs(const QPoint &fromCenter, const QPoint &toCenter,
                                  double pixelsPerSecond, int minMs, int maxMs);

// Pick the rect the glide animation starts from. When the overlay is
// already on screen and the proposed startRect is valid, use it
// verbatim — that lets the animation pick up from wherever the overlay
// currently sits, mid-glide if necessary. When the overlay isn't
// visible OR the proposed start is invalid, fall back to the target
// rect so the first appearance just snaps in without a stray
// transition from (0,0).
[[nodiscard]] QRect chooseGlideStart(const QRect &startRect, const QRect &targetRect,
                                     bool overlayVisible);

// Selection-overlay rect for an item placed at `pos` of (itemWidth,
// itemHeight), expanded by `inset` pixels on each side. Pure geometry.
[[nodiscard]] QRect overlayRectForPosition(const QPoint &pos, int itemWidth, int itemHeight,
                                           int inset);

} // namespace OverlayHelpers

#endif // OVERLAYHELPERS_H
