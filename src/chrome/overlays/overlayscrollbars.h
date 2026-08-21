#ifndef OVERLAYSCROLLBARS_H
#define OVERLAYSCROLLBARS_H

#include <QColor>

#include "collectiontypes.h"

class QAbstractScrollArea;
class QWidget;

/// Thin, fading, non-reserving scrollbars (user request 2026-08-18).
///
/// The native bars are turned OFF entirely rather than toggled, because
/// toggling a scrollbar's policy resizes the viewport — every item in the
/// grid shifts sideways the moment a bar appears or disappears, which is
/// exactly what the user asked to stop. A slim handle is painted ON TOP of
/// the viewport instead: it reserves no space, so item sizes and positions
/// never move, and it fades in while scrolling or hovering and fades out
/// again when idle. No groove, no frame line — just the handle.
namespace OverlayScrollbars {

/// The colour the handle paints in. Exposed because it carries a real
/// constraint: the tree's selection and root-collection rows are filled
/// with the titlebar colour, so a handle equal to it disappears the moment
/// a row is selected. Tested against that.
[[nodiscard]] QColor handleColor();

/// Attach (or detach) overlay scrollbars for @p area. Idempotent, and
/// detaching restores the scrollbar policies captured at attach time.
void apply(QAbstractScrollArea *area, bool enabled);

/// Apply @p mode to BOTH scrollbar mechanisms for @p area — the native bars
/// and any overlay handle. One entry point because driving only one of them
/// is what made the grid's own toggle look broken: the overlay forces the
/// native policy off and paints its own handle, so a user who hid the
/// scrollbar still saw one (field report 2026-08-19).
///
/// Show      — normal behaviour, the handle fades in while scrolling/hovering.
/// Autohide  — nothing until the pointer comes near the lane the bar occupies.
/// Hide      — never drawn. Scrolling itself is unaffected in every mode.
///
/// Idempotent. The policies in force when Hide first takes hold are stashed
/// and put back when it lifts rather than assuming ScrollBarAsNeeded —
/// several of these surfaces are deliberately AlwaysOff already (the details
/// pane's content view), and a blanket restore would conjure a bar they never
/// had.
///
/// AUTOHIDE NEEDS THE OVERLAY. Proximity is a property of the painted handle;
/// a native bar cannot do it. With overlay scrollbars switched off globally,
/// Autohide therefore behaves as Show rather than hiding a bar that could
/// never come back — silently showing nothing would be the worse failure.
void setScrollbarMode(QAbstractScrollArea *area, ScrollbarMode mode);

/// setScrollbarMode() for every scroll area inside @p root. The details pane
/// is several scroll areas (content view, artwork strip, metadata card), and
/// the user's choice applies to all of them.
void setPaneScrollbarMode(QWidget *root, ScrollbarMode mode);

/// Apply to every scrollable surface the user points at: the item grid,
/// the collection tree, and each scroll area inside the details pane.
/// Takes plain widgets so both MainWindow and the settings dialog can call
/// it without either reaching across a layer for the other.
/// Width the content must leave clear on the right so the handle never
/// covers it. Zero when this area has no overlay bars attached. Layout code
/// subtracts this from the usable width — the lane is a LAYOUT concept, not
/// a paint-time one; drawing the handle over the content is exactly the
/// overlap that was reported (2026-08-19).
[[nodiscard]] int reservedGutter(const QWidget *area);

/// True when overlay handles are driving @p area. While attached the overlay
/// OWNS the native scrollbar policies (both forced AlwaysOff) and paints its
/// own handle. Anything that sets a native policy must ask this first: two
/// call sites re-asserted ScrollBarAsNeeded after attachment, which brought a
/// real 21px scrollbar back and read as a permanent margin between the grid
/// and the details pane (field report 2026-08-20).
[[nodiscard]] bool isAttached(const QWidget *area);

void applyToSurfaces(QWidget *itemScrollArea, QWidget *collectionTree, QWidget *detailsPane,
                     bool enabled);

} // namespace OverlayScrollbars

#endif // OVERLAYSCROLLBARS_H
