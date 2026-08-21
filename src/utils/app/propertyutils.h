#ifndef PROPERTYUTILS_H
#define PROPERTYUTILS_H

/**
 * Widget-specific Qt dynamic property keys.
 *
 * Most interaction state has been migrated to InteractionStateHolder
 * (see stateutils.h). These remaining properties are per-widget state
 * that cannot be centralized.
 */
namespace PropertyKeys {

// Per-widget artwork state (ItemWidget)
inline constexpr const char *TrackedByArtwork = "_tracked_by_artwork";
inline constexpr const char *DeferArtworkUpdate = "_defer_artwork_update";
inline constexpr const char *ForcePlaceholder = "_force_placeholder";

// Parent widget glide state - read by ItemWidget from scroll area
inline constexpr const char *GlideAnimating = "_glide_animating";

/// Set on a QAbstractScrollArea while OverlayScrollbars is driving it. The
/// overlay OWNS both native scrollbar policies (forced AlwaysOff) and paints
/// its own handle, so anything below chrome/ that would set a native policy
/// must check this first and leave the policies alone when it is set.
/// The name is the contract; OverlayScrollbars writes it, others only read.
inline constexpr const char *OverlayScrollbarsAttached = "kartendOverlayScrollbars";

} // namespace PropertyKeys

#endif