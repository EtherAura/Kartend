#ifndef OVERLAYSCROLLBARS_H
#define OVERLAYSCROLLBARS_H

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

/// Attach (or detach) overlay scrollbars for @p area. Idempotent, and
/// detaching restores the scrollbar policies captured at attach time.
void apply(QAbstractScrollArea *area, bool enabled);

/// Apply to every scrollable surface the user points at: the item grid,
/// the collection tree, and each scroll area inside the details pane.
/// Takes plain widgets so both MainWindow and the settings dialog can call
/// it without either reaching across a layer for the other.
void applyToSurfaces(QWidget *itemScrollArea, QWidget *collectionTree, QWidget *detailsPane,
                     bool enabled);

} // namespace OverlayScrollbars

#endif // OVERLAYSCROLLBARS_H
