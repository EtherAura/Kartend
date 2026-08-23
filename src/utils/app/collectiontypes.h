#ifndef COLLECTIONTYPES_H
#define COLLECTIONTYPES_H

// Standalone enums extracted from collectionutils.h. Files that only need
// the type enums (e.g. for function signatures, switch dispatch, or
// settings-form bindings) can include this header instead of dragging in
// CollectionConfig + UIConstants + the hierarchy cache. collectionutils.h
// re-includes this header so existing callers keep compiling unchanged.

enum class HorizontalAlignment { Left = 0, Center = 1, Right = 2 };

/// Per-surface scrollbar policy (user request 2026-08-19). Replaces the
/// hide/show bools these settings used to be — Autohide is the state that
/// needed a third value: nothing is drawn until the pointer comes near the
/// lane the bar would occupy, then it appears.
///
/// Order matters for the settings combos, which cast index <-> enum.
/// Persisted as a STRING so the extra state was not a silent bool
/// reinterpretation; the readers still accept the legacy "true"/"false".
enum class ScrollbarMode { Show = 0, Autohide = 1, Hide = 2 };

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

/// Kartend-auh7u: vertical extent of a left/right-docked sidebar panel (the
/// details pane's Fixed L/R dock and the collection tree). BelowToolbar is
/// the classic layout — the items toolbar spans the full window width and
/// the panel lives under it. FullHeight hoists the panel outside the
/// toolbar+content column, so the panel runs the entire window height and
/// the toolbar stops at the panel's edge. Top/Bottom docks and Overlay mode
/// ignore it.
enum class SidebarJustification { BelowToolbar = 0, FullHeight = 1 };

/// How the collection tree panel renders its row icons (user request
/// 2026-08-17). Normal shows the artwork as-is; the monochrome pair recolour
/// the icon's alpha silhouette to a fixed dark/light ink; Tinted recolours it
/// with `treeIconTintColor` (or the collection's accent when that is empty).
/// Enum order is the settings combo's row order — keep them in sync.
/// Where the toolbar's fill colour comes from (user request 2026-08-18).
/// The desktop-derived options exist because the collection's own colour
/// cannot match the window titlebar, which is what prompted this.
enum class ToolbarColorSource {
  CollectionPrimary, ///< The collection's primaryColor — the long-standing behaviour.
  Titlebar,          ///< The desktop titlebar (kdeglobals [WM] activeBackground).
  Accent,            ///< The desktop accent (kdeglobals AccentColor).
  Highlight,         ///< The palette's highlight/selection colour.
};

enum class TreeIconStyle { Normal = 0, MonochromeDark = 1, MonochromeLight = 2, Tinted = 3 };

/// What a collection-tree row shows when it HAS artwork (Kartend-j1mtg, user
/// request 2026-08-22). Replaces the treeIconsOnly bool, which only ever chose
/// between the last two of these — a row with artwork always lost its name,
/// because TreeIconDelegate cleared the label unconditionally on the icon path.
/// IconAndText is the third behaviour that was never reachable.
///
/// Rows with NO artwork are unaffected and always show their name: a blank
/// unlabelled row would be unusable, so text is the fallback in every mode.
///
/// Order matters for the settings combo, which casts index <-> enum.
enum class TreeIconDisplay { TextOnly = 0, IconAndText = 1, IconOnly = 2 };

/// What a collection's RetroArch system glyph depicts (Kartend-1kkk2, user
/// request 2026-08-22: "i want to support the console icon or controller
/// icon"). This is a SEPARATE mark from the row artwork TreeIconDisplay
/// governs — a small fixed-size glyph drawn left of the name in every display
/// mode, sourced from a local RetroArch install rather than from a scrape.
///
/// The three values do not select three files. RetroArch's icon packs are
/// subject-consistent — `monochrome` draws a controller for every system,
/// `systematic` draws a console for every system — so Console and Controller
/// resolve the SAME per-system file out of DIFFERENT packs. Content is the
/// odd one out: it is the `-content` sibling (cartridge, disc, tape) that
/// every pack ships beside each system icon, so it is a file variant within
/// whichever pack is in play. See retroarchicons.h.
///
/// Order matters for the settings combo, which casts index <-> enum.
enum class SystemIconSubject { Controller = 0, Console = 1, Content = 2 };

/// Where the system glyph sits relative to the collection's name (user
/// request 2026-08-22: "can we have an option to put the icons on the right of
/// the text - after the title/before the nav border").
///
/// BeforeName and AfterName travel WITH the name — the glyph hugs it, so on a
/// centred row the pair stays centred together and the glyph's x follows the
/// text. RowEnd instead pins the glyph to the panel's inner edge, which lines
/// every glyph up in a column no matter how long the names are; the trade is
/// that on a short name the glyph sits outside the row's highlight rather than
/// inside it.
///
/// Order matters for the settings combo, which casts index <-> enum.
enum class SystemIconPlacement { BeforeName = 0, AfterName = 1, RowEnd = 2 };

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
enum class ViewType {
  Grid = 0,
  List = 1,
  CoverFlow = 2,
  Horizontal = 3,
  // Sentinel == number of real view types. Keep last; used by static_asserts
  // that keep per-ViewType tables complete when a view type is added
  // (Kartend-ox2go). Never a runtime value, so it is never serialized.
  Count
};

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
