#ifndef INTERACTIONHELPERS_H
#define INTERACTIONHELPERS_H

#include "collectiontypes.h"

#include <QString>

// Pure helpers extracted from InteractionManager so they can be unit-tested
// without instantiating the full UI/manager graph.
//
// All functions are stateless and free of Qt object dependencies; they take
// their inputs explicitly so tests can construct minimal fixtures.
namespace InteractionHelpers {

// Returns the stepSize the mouse-hold vertical scroller should advance by.
//
// List and CoverFlow views step by one item per tick. Grid and Horizontal
// views step by a full row (gridWidth). When `gridWidth <= 0` the helper
// returns 1 to keep the scroller usable rather than freezing on an unknown
// layout.
[[nodiscard]] auto stepSizeForViewType(ViewType viewType, int gridWidth) -> int;

// Result of a single vertical hold-scroll tick: the new selection index and
// whether the index wrapped around (so callers can apply force-immediate
// centering and wrap-sequence flags).
struct HoldScrollStep {
  int nextIndex = -1;
  bool didWrap = false;
};

// Computes one vertical mouse-hold-scroll step.
//
// - `direction` is +1 (down) or -1 (up).
// - `stepSize` is the number of items to advance per tick (see stepSizeForViewType).
// - When `wrap` is true, an out-of-range step modulo-wraps to the other end
//   and `didWrap` is set. When false, the result clamps to [0, totalItems-1].
// - When totalItems <= 0 the helper returns {-1, false} (caller stops scroll).
// - When the computed nextIndex equals currentIndex (e.g. clamped at the
//   edge) the helper still returns the value; the caller decides whether a
//   no-op step should short-circuit downstream work.
[[nodiscard]] auto computeVerticalHoldStep(int currentIndex, int direction, int stepSize,
                                           int totalItems, bool wrap) -> HoldScrollStep;

// Picks the effective collection index for an item launch / preview action.
//
// Production call sites pass `dbIndex` from
// DatabaseManager::getCollectionIndexForFile (the file's owning collection,
// or -1 if unmapped) and `fallbackIndex` from the currently-viewed collection.
//
// Returns `dbIndex` when it points into the collections list; otherwise the
// `fallbackIndex` if that is in range; otherwise -1.
[[nodiscard]] auto resolveOwnerIndex(int dbIndex, int fallbackIndex, int collectionsSize) -> int;

// Structural classification of a right-clicked item, driving which top-level
// context-menu entries are offered.
//
// - A "media item" is anything that is neither a subcollection tile nor a
//   virtual-folder tile.
// - "Launch" is offered only for media items that carry a file path (a media
//   tile without a resolved path has nothing to hand the launcher).
// - "Open" is the enter-equivalent for the two navigable tile kinds.
struct ContextTargetFlags {
  bool isMediaItem = false;
  bool showLaunch = false;
  bool showOpen = false;
};

[[nodiscard]] auto classifyContextTarget(bool isSubcollection, bool isVirtualFolder,
                                         bool hasFilePath) -> ContextTargetFlags;

// Visibility of the playlist-scoped context-menu actions.
//
// - "Remove from playlist" only inside a *static* playlist — removal from a
//   smart playlist would not stick (the next open re-evaluates the filter).
// - "Edit smart filter…" only for smart playlists (nothing filter-shaped to
//   edit on a static one).
// - "Delete playlist…" hidden for reserved built-ins (PlaylistManager refuses
//   the call anyway; surfacing a button that always errors is worse UX).
struct PlaylistMenuFlags {
  bool showRemoveFromPlaylist = false;
  bool showEditSmartFilter = false;
  bool showDeletePlaylist = false;
};

[[nodiscard]] auto playlistContextFlags(bool insidePlaylist, bool isSmartPlaylist,
                                        bool isReservedPlaylist) -> PlaylistMenuFlags;

// Picks the launcher index an item activation will use.
//
// Mirrors the production precedence shared by the launch-preview dialog and
// the "Always launch with…" default: the per-item override wins when it is a
// valid index, otherwise the collection's default launcher clamped into
// range. `launcherCount <= 0` returns 0 (the "no configured launchers"
// placeholder the preview path uses).
[[nodiscard]] auto pickLauncherIndex(int overrideIndex, int defaultLauncherIndex, int launcherCount)
    -> int;

// Expand-mode (two-stage activation) routing decision.
//
// - LaunchDirectly: expand-mode is off for the viewing collection — the
//   activation proceeds straight to launch.
// - CollapseThenLaunch: this exact item is already expanded AND the overlay is
//   still visible — hide the overlay, clear the expand state, and launch.
// - TryExpand: first-stage activation — attempt to show the preview overlay
//   (the caller still falls back to launch when the overlay has nothing to
//   show, so a media-less item can never be trapped un-launchable).
enum class ExpandActivation { LaunchDirectly, CollapseThenLaunch, TryExpand };

[[nodiscard]] auto classifyExpandActivation(bool expandModeEnabled, int expandedItemIndex,
                                            int activationIndex, bool overlayVisible)
    -> ExpandActivation;

// Human-readable display title derived from a media file path: the complete
// base name with underscores flattened to spaces and whitespace runs
// collapsed. Empty input yields an empty title.
[[nodiscard]] auto displayTitleForFilePath(const QString &filePath) -> QString;

} // namespace InteractionHelpers

#endif // INTERACTIONHELPERS_H
