#ifndef COLLECTIONTYPES_H
#define COLLECTIONTYPES_H

// Standalone enums extracted from collectionutils.h. Files that only need
// the type enums (e.g. for function signatures, switch dispatch, or
// settings-form bindings) can include this header instead of dragging in
// CollectionConfig + UIConstants + the hierarchy cache. collectionutils.h
// re-includes this header so existing callers keep compiling unchanged.

enum class HorizontalAlignment { Left = 0, Center = 1, Right = 2 };

/// anchor for the per-collection header logo overlay. Drawn at
/// the top of the items viewport over the grid. Center matches the typical
/// "title slate" usage; Left/Right are for users who want the logo offset so
/// it doesn't fight a centered toolbar element.
enum class HeaderLogoPosition { TopLeft = 0, TopCenter = 1, TopRight = 2 };

enum class DetailsPaneMode { Overlay = 0, Expand = 1 };

/// which edge of the items viewport the details
/// pane docks against. Right/Left are width-driven; Top/Bottom are
/// height-driven. In Expand mode the layout swaps the insertion index and
/// orientation; in Overlay mode the anchor edge is swapped.
enum class DetailsPanePosition { Right = 0, Left = 1, Top = 2, Bottom = 3 };

/// how the details-pane background is rendered. Color and Image
/// mirror the main-view BackgroundType values. Pattern adds a
/// procedurally-drawn pattern (currently only Crosshatch) tinted by
/// `detailsPanePatternColor`.
enum class DetailsPaneBackgroundType { Color = 0, Image = 1, Pattern = 2 };

/// built-in details-pane background patterns. Single value today;
/// dots/lines/etc. can be added without breaking persistence because the int
/// representation is what's serialized.
enum class DetailsPanePattern { Crosshatch = 0 };

/// which built-in tab is active in the details pane. Item is the
/// per-item view (artwork + metadata); Collection forces the collection
/// summary even with a selection; File is reserved for a user-customizable
/// pane in a later iteration.
enum class DetailsPaneTab { Item = 0, Collection = 1, File = 2 };

/// per-collection background can be a flat color, a wallpaper
/// image, or a looping muted video file. Video uses BackgroundVideoWidget
/// (QMediaPlayer + QVideoSink) parented to the items viewport; Image and
/// Color route through QSS on the viewport as before.
enum class BackgroundType { Color = 0, Image = 1, Video = 2 };

/// View type for displaying collection items.
/// Horizontal flips the virtual-scrolling axis so items flow
/// top-to-bottom then left-to-right. The collection's `gridWidth` is reused
/// as "items per column" (the fixed dimension) instead of items per row, and
/// the items area scrolls horizontally instead of vertically.
enum class ViewType { Grid = 0, List = 1, CoverFlow = 2, Horizontal = 3 };

/// Sort mode for collection items
enum class SortMode {
  NameAscending = 0,        // A → Z (default)
  NameDescending = 1,       // Z → A
  CollectionAscending = 2,  // Collection name A → Z
  CollectionDescending = 3, // Collection name Z → A
  ArtworkFirst = 4,         // Items with artwork first
  ArtworkLast = 5,          // Items with artwork last
  Random = 6,               // Shuffle
  DateDescending = 7,       // Newest modified first
  DateAscending = 8,        // Oldest modified first
  SizeDescending = 9,       // Largest file first
  SizeAscending = 10        // Smallest file first
};

#endif // COLLECTIONTYPES_H
