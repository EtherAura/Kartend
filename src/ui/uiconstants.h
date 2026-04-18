#ifndef UICONSTANTS_H
#define UICONSTANTS_H

#include <QIcon>
#include <QString>

namespace UIConstants {

// =============================================================================
// Grid Layout
// Virtual scrolling grid configuration and container sizing.
// =============================================================================
namespace Grid {
/// Default number of items per row
inline constexpr int DEFAULT_WIDTH = 7;
/// Minimum items per row
inline constexpr int MIN_WIDTH = 1;
/// Maximum items per row
inline constexpr int MAX_WIDTH = 40;
/// Spacing between grid items in pixels
inline constexpr int SPACING = 20;
/// Margins around the grid in pixels
inline constexpr int MARGINS = 10;
/// Extra rows to render above/below viewport for smooth scrolling
inline constexpr int BUFFER_ROWS = 2;
/// Default number of visible rows for pool sizing
inline constexpr int DEFAULT_VISIBLE_ROWS = 6;
/// Y offset for virtual container positioning
inline constexpr int CONTAINER_OFFSET = -20;
/// Left offset for virtual container
inline constexpr int CONTAINER_LEFT_OFFSET = 20;
/// Right offset for virtual container
inline constexpr int CONTAINER_RIGHT_OFFSET = 16;
/// Extra height buffer for container sizing
inline constexpr int CONTAINER_HEIGHT_BUFFER = 25;
} // namespace Grid

// =============================================================================
// Item Dimensions
// ItemWidget sizing, fonts, and validation bounds.
// =============================================================================
namespace Item {
/// Default item width when no collection-specific value set
inline constexpr int DEFAULT_WIDTH = 220;
/// Default item height when no collection-specific value set
inline constexpr int DEFAULT_HEIGHT = 245;
/// Minimum allowed item width in configuration
inline constexpr int MIN_WIDTH = 100;
/// Minimum allowed item height in configuration
inline constexpr int MIN_HEIGHT = 100;
/// Maximum allowed item width (4K resolution)
inline constexpr int MAX_WIDTH = 3840;
/// Maximum allowed item height (4K resolution)
inline constexpr int MAX_HEIGHT = 2160;
/// Minimum font size for item titles
inline constexpr int MIN_FONT_SIZE = 4;
/// Default font size for item titles
inline constexpr int DEFAULT_FONT_SIZE = 8;
/// Maximum font size for item titles
inline constexpr int MAX_FONT_SIZE = 72;
/// Minimum corner radius for item artwork
inline constexpr int MIN_CORNER_RADIUS = 0;
/// Default corner radius for item artwork
inline constexpr int DEFAULT_CORNER_RADIUS = 0;
/// Maximum corner radius for item artwork
inline constexpr int MAX_CORNER_RADIUS = 100;
} // namespace Item

// =============================================================================
// List View
// Constants for list view mode (text-only, no artwork per item).
// =============================================================================
namespace ListView {
/// Default row height in list view mode (text only, no artwork)
inline constexpr int DEFAULT_ROW_HEIGHT = 32;
/// Minimum row height in list view
inline constexpr int MIN_ROW_HEIGHT = 20;
/// Maximum row height in list view
inline constexpr int MAX_ROW_HEIGHT = 100;
/// Left padding for text in list view
inline constexpr int TEXT_LEFT_PADDING = 16;
/// Right padding for text in list view
inline constexpr int TEXT_RIGHT_PADDING = 16;
/// Vertical spacing between list rows (0 = no gaps, eliminates background color
/// bleeding through)
inline constexpr int ROW_SPACING = 0;
/// Header row height for column headers
inline constexpr int HEADER_HEIGHT = 28;
/// Width of folder icon column for subcollections/virtual folders in list mode
inline constexpr int FOLDER_ICON_COLUMN_WIDTH = 20;
} // namespace ListView

// =============================================================================
// Timing - General Delays
// All timing values are in milliseconds unless otherwise noted.
// =============================================================================
namespace Timing {
/// Minimal delay for immediate-next-event-loop deferral
inline constexpr int SHORT_DELAY_MS = 50;
/// Standard debounce interval for rapid events
inline constexpr int MEDIUM_DELAY_MS = 100;
/// Delay for operations that need visual settling
inline constexpr int LONG_DELAY_MS = 200;
/// Initial delay before first collection load
inline constexpr int STARTUP_DELAY_MS = 300;
/// Debounce for layout recalculation after resize/config change
inline constexpr int LAYOUT_UPDATE_DELAY_MS = 100;
/// Debounce for viewport-based artwork loading
inline constexpr int VIEWPORT_UPDATE_DELAY_MS = 100;
/// Throttle interval for scroll event processing (normal scrolling)
inline constexpr int SCROLL_THROTTLE_DELAY_MS = 100;
/// How long tooltips remain visible
inline constexpr int TOOLTIP_DISPLAY_MS = 2000;
/// Delay before showing tooltip after hover
inline constexpr int TOOLTIP_DELAY_MS = 50;
/// Legacy alias for LAYOUT_UPDATE_DELAY_MS
inline constexpr int LAYOUT_DELAY_MS = 100;
/// Time to wait for Qt layout system to stabilize
inline constexpr int VIEWPORT_DELAY_MS = 300;
/// Base interval for settings dialog retry operations
inline constexpr int SETTINGS_RETRY_BASE_MS = 50;
/// Time without interaction before triggering idle behavior
inline constexpr int USER_IDLE_THRESHOLD_MS = 2000;
/// Delay after resize before re-centering selection
inline constexpr int RESIZE_RECENTER_DELAY_MS = 150;
} // namespace Timing

// =============================================================================
// Concurrency
// Thread pool sizing for background work.
// =============================================================================
namespace Concurrency {
/// Minimum worker threads for background pools
inline constexpr int WORKER_POOL_MIN_THREADS = 1;
/// Maximum worker threads for background pools (caps CPU contention)
inline constexpr int WORKER_POOL_MAX_THREADS = 4;
/// Ideal thread count divisor (e.g., 2 means use ~half of available threads)
inline constexpr int WORKER_POOL_DIVISOR = 2;
} // namespace Concurrency

// =============================================================================
// Database
// Limits and thresholds for database-backed scanning and cache validation.
// =============================================================================
namespace Database {
/// How many directory samples are stored per collection for change detection.
inline constexpr int DIR_SIGNATURE_SAMPLE_COUNT = 64;
/// Max number of directories inspected when seeding a signature without
/// scanning.
inline constexpr int DIR_SIGNATURE_SEED_MAX_DIRS = 256;
/// Minimum interval between scan progress emissions.
/// Prevents high-frequency UI updates during very fast scans.
inline constexpr int SCAN_PROGRESS_MIN_INTERVAL_MS = 33;

/// Maximum number of file entries a single directory scan task will enqueue
/// at once. Bounds peak memory when scanning very large folders.
inline constexpr int SCAN_DIR_RESULT_CHUNK_SIZE = 2048;

/// Maximum number of pending scan result chunks buffered between worker
/// threads and the consuming scan loop. Provides backpressure to keep memory
/// bounded during very fast directory scans.
inline constexpr int SCAN_READY_MAX_RESULTS = 32;

/// FTS backfill batch size when building the index lazily.
/// Kept modest to reduce lock contention with normal query operations.
inline constexpr int FTS_BACKFILL_BATCH_SIZE = 2000;

/// Time budget for each incremental FTS backfill slice.
/// Keeps the scan worker responsive and avoids long write locks.
inline constexpr int FTS_BACKFILL_TIME_BUDGET_MS = 80;

/// Delay between incremental FTS backfill slices.
/// Prevents the scan worker from running a tight 0ms timer loop that can
/// consume a full CPU core on very large databases.
inline constexpr int FTS_BACKFILL_SLICE_DELAY_MS = 20;

/// Default chunk size for on-demand range loading during virtual scroll.
/// Balances latency (smaller = faster first paint) vs throughput (larger =
/// fewer round-trips).
inline constexpr int RANGE_CHUNK_SIZE_DEFAULT = 100;

/// Larger chunk size for showAllSubcollectionItems mode with many items.
/// Reduces database round-trips when flattening 1M+ items across
/// subcollections.
inline constexpr int RANGE_CHUNK_SIZE_LARGE = 1000;

/// Item count threshold to switch to larger chunk size.
/// When total items exceed this, use RANGE_CHUNK_SIZE_LARGE for efficiency.
inline constexpr int RANGE_CHUNK_LARGE_THRESHOLD = 5000;

/// Number of chunks to prefetch ahead of scroll position.
/// Prefetching reduces perceived latency during continuous scrolling.
inline constexpr int RANGE_PREFETCH_CHUNKS = 3;

/// Item count threshold to precompute sorted order for O(1) range lookups.
/// When items exceed this, fetchItemCount creates a temp table with sorted
/// positions. Avoids expensive ORDER BY + OFFSET for every range query on large
/// collections.
inline constexpr int PRECOMPUTE_SORT_THRESHOLD = 10000;

/// SQLite busy_timeout for the main DatabaseManager connection (ms).
/// Higher value tolerates long-running scan-worker write transactions.
inline constexpr int MAIN_BUSY_TIMEOUT_MS = 30000;

/// SQLite busy_timeout for the QueryManager worker connection (ms).
/// Lower than the main connection: query slots are expected to fail fast and
/// retry rather than block the UI worker thread for seconds.
inline constexpr int WORKER_BUSY_TIMEOUT_MS = 500;

/// Number of reconnect attempts before giving up on a lost worker connection.
inline constexpr int WORKER_RECONNECT_ATTEMPTS = 3;

/// Delay between worker reconnection attempts (ms).
inline constexpr int WORKER_RECONNECT_DELAY_MS = 100;
} // namespace Database

// =============================================================================
// Animation
// Durations and keyframe positions for scroll and pulse animations.
// =============================================================================
namespace Animation {
/// Duration for animated scroll to center selected item
inline constexpr int CENTER_SCROLL_DURATION_MS = 1500;
/// Minimum duration for smooth scroll animations
inline constexpr int SMOOTH_SCROLL_MIN_DURATION_MS = 1500;
/// Maximum duration for smooth scroll animations
inline constexpr int SMOOTH_SCROLL_MAX_DURATION_MS = 1500;
/// Duration for mouse wheel triggered smooth scroll
inline constexpr int SMOOTH_SCROLL_WHEEL_DURATION_MS = 1500;
/// Duration for horizontal glide animation between items
inline constexpr int HSCROLL_ANIM_DURATION_MS = 140;
/// Full cycle duration for selection pulse animation
inline constexpr int PULSE_DURATION_MS = 3000;
/// Minimum opacity during pulse animation cycle
inline constexpr double PULSE_OPACITY_LOW = 0.25;
/// Maximum opacity during pulse animation cycle
inline constexpr double PULSE_OPACITY_HIGH = 1.0;
/// Delay before pulse animation starts after selection
inline constexpr int PULSE_INACTIVITY_DELAY_MS = 100;
/// Keyframe position (0-1) for pulse peak opacity
inline constexpr double PULSE_KEYFRAME_MID_POS = 0.5;
} // namespace Animation

// =============================================================================
// Keyboard Navigation
// Timing for arrow key navigation, repeat rates, and centering behavior.
// =============================================================================
namespace Keyboard {
/// Base interval between arrow key repeat steps
inline constexpr int BASE_INTERVAL_MS = 260;
/// Extra delay added for vertical navigation (row changes)
inline constexpr int VERTICAL_EXTRA_MS = 120;
/// Time to wait for animation to complete before next step
inline constexpr int ANIMATION_SETTLE_MS = 200;
/// Initial delay before key repeat begins
inline constexpr int REPEAT_START_DELAY_MS = 260;
/// Timer interval for view updates during navigation
inline constexpr int VIEW_UPDATE_INTERVAL_MS = 30;
/// Delay to clear arrow center suppression after selection restore
inline constexpr int ARROW_CENTER_CLEAR_AFTER_RESTORE_MS = 300;
/// Extended suppression after selection restore completes
inline constexpr int ARROW_CENTER_EXTRA_SUPPRESS_AFTER_RESTORE_MS = 500;
/// Delay before checking if arrow center can be cleared
inline constexpr int ARROW_CENTER_CLEAR_CHECK_DELAY_MS = 440;
/// Delay to clear arrow center suppression after explicit set
inline constexpr int ARROW_CENTER_CLEAR_AFTER_SET_MS = 240;
} // namespace Keyboard

// =============================================================================
// Gamepad
// Analog stick deadzones for digital navigation.
// =============================================================================
namespace Gamepad {
/// Threshold to consider an axis direction engaged (press)
inline constexpr double AXIS_DEADZONE_ON = 0.60;
/// Threshold to consider an axis direction released (hysteresis)
inline constexpr double AXIS_DEADZONE_OFF = 0.45;
/// Poll interval for gamepad state in SDL2 fallback backend (when connected)
inline constexpr int POLL_INTERVAL_MS = 16;
/// Slow poll interval when no controller is connected (reduces idle CPU)
inline constexpr int POLL_INTERVAL_IDLE_MS = 1000;
} // namespace Gamepad

// =============================================================================
// Mouse and Click Handling
// Timing for click-hold scrolling, wheel events, and double-click detection.
// =============================================================================
namespace Mouse {
/// Delay before click-hold scroll mode activates
inline constexpr int CLICK_HOLD_START_MS = 500;
/// Interval between horizontal scroll steps during click-hold
inline constexpr int CLICK_HOLD_HORIZONTAL_INTERVAL_MS = 320;
/// Duration to suppress arrow centering after wheel scroll
inline constexpr int WHEEL_SUPPRESS_ARROW_CENTER_MS = 420;
/// Duration to suppress position updates after double-click
inline constexpr int DOUBLE_CLICK_POS_SUPPRESS_MS = 300;
/// Time without scroll events to consider scrolling stopped
inline constexpr int CONTINUOUS_SCROLL_IDLE_MS = 300;
/// Timer to detect when user scroll interaction ends
inline constexpr int USER_SCROLL_IDLE_TIMER_MS = 240;
/// Delay before clearing user scroll active flag
inline constexpr int USER_SCROLL_ACTIVE_CLEAR_DELAY_MS = 50;
/// Brief delay before recentering after repeat stops
inline constexpr int STOP_REPEAT_RECENTER_DELAY_MS = 10;
/// Angle delta per wheel click (Qt standard: 120 = 1 step)
inline constexpr int WHEEL_ANGLE_STEP = 120;
/// Pixel scroll amount per wheel step
inline constexpr int WHEEL_PIXEL_STEP = 120;
} // namespace Mouse

// =============================================================================
// Launch
// Limits for spawning external processes and extracting archives.
// =============================================================================
namespace Launch {
/// Maximum directory depth walked when locating an extracted file inside
/// a temp extraction directory. Bounds work and limits the blast radius if
/// a malicious archive contains deeply nested or symlinked structures.
inline constexpr int MAX_EXTRACTION_DEPTH = 16;
/// Hard ceiling on the number of files inspected while scanning an
/// extraction directory for a target extension.
inline constexpr int MAX_EXTRACTION_FILES_INSPECTED = 50000;
} // namespace Launch

// =============================================================================
// Scroll
// Tunables for virtual scrolling, viewport sizing, and artwork prewarm.
// =============================================================================
namespace Scroll {
/// Item count threshold above which artwork prewarm is gated to a single
/// debounced batch (instead of fanning out for every visible row change).
inline constexpr int ARTWORK_PREWARM_LARGE_COLLECTION_THRESHOLD = 100;
/// Minimum gap between consecutive artwork prewarm batches (ms).
inline constexpr qint64 ARTWORK_PREWARM_DEBOUNCE_MS = 200;
/// Floor for the effective viewport width used in grid math (px). Prevents
/// degenerate layouts when the widget is briefly given a near-zero width
/// during construction or splitter resizes.
inline constexpr int MIN_EFFECTIVE_VIEWPORT_WIDTH = 200;
} // namespace Scroll

// =============================================================================
// Overlay
// Timing for transient overlays (search loading indicator, etc.).
// =============================================================================
namespace Overlay {
/// Fade in/out duration for the search loading overlay (ms).
inline constexpr int SEARCH_LOADING_FADE_DURATION_MS = 150;
/// Pulse animation interval for the search loading overlay (ms).
inline constexpr int SEARCH_LOADING_PULSE_INTERVAL_MS = 800;
} // namespace Overlay

// =============================================================================
// Search
// Debounce intervals for search input and UI refocus timing.
// =============================================================================
namespace Search {
/// Debounce delay before executing search query
inline constexpr int DEBOUNCE_DELAY_MS = 120;
/// Debounce for rapid typing in search bar
inline constexpr int TYPING_DEBOUNCE_MS = 180;
/// Short delay before refocusing search bar
inline constexpr int REFOCUS_DELAY_SHORT_MS = 180;
/// Longer delay for refocus after complex operations
inline constexpr int REFOCUS_DELAY_LONG_MS = 400;
} // namespace Search

// =============================================================================
// Selection
// Timing for selection restore process and double-click handling.
// =============================================================================
namespace Selection {
/// Number of steps in progressive selection restore
inline constexpr int RESTORE_STEPS = 6;
/// Delay between each restore step
inline constexpr int RESTORE_STEP_DELAY_MS = 120;
/// Maximum total time for selection restore process
inline constexpr int RESTORE_MAX_DELAY_MS = 900;
/// First early verification check during restore
inline constexpr int RESTORE_EARLY_VERIFY_1_MS = 140;
/// Second early verification check during restore
inline constexpr int RESTORE_EARLY_VERIFY_2_MS = 320;
/// Suppress double-click immediately after subcollection enter
inline constexpr int DOUBLE_CLICK_SUPPRESS_AFTER_ENTER_MS = 700;
/// Delay before clearing double-click suppression
inline constexpr int DOUBLE_CLICK_SUPPRESS_CLEAR_DELAY_MS = 800;
/// Debounce delay before pushing the selected item's metadata into the
/// sidebar. Prevents thrash during rapid keyboard / mouse navigation.
inline constexpr int METADATA_SIDEBAR_UPDATE_DELAY_MS = 120;
/// Selection-overlay glide animation: travel speed in pixels-per-second.
/// Used to derive a duration proportional to the distance the overlay must
/// move so short hops feel snappy and long jumps stay readable.
inline constexpr double OVERLAY_GLIDE_PIXELS_PER_SECOND = 1500.0;
/// Hard cap on selection-overlay glide animation duration (ms).
/// Prevents very long jumps from feeling sluggish.
inline constexpr int OVERLAY_GLIDE_MAX_DURATION_MS = 300;
} // namespace Selection

// =============================================================================
// Navigation
// Timing for collection navigation, progress indicators, and scrollbar
// recovery.
// =============================================================================
namespace Navigation {
/// Early clear of loading progress indicator
inline constexpr int PROGRESS_CLEAR_EARLY_MS = 200;
/// Standard delay before clearing progress indicator
inline constexpr int PROGRESS_CLEAR_MS = 700;
/// Delay before centering after entering subcollection
inline constexpr int SUBCOLLECTION_SCROLL_CENTER_DELAY_MS = 600;
/// First attempt to recover scrollbar position
inline constexpr int SCROLLBAR_RECOVERY_ATTEMPT_1_MS = 90;
/// Second attempt to recover scrollbar position
inline constexpr int SCROLLBAR_RECOVERY_ATTEMPT_2_MS = 240;
/// Final attempt to recover scrollbar position
inline constexpr int SCROLLBAR_RECOVERY_ATTEMPT_3_MS = 460;
} // namespace Navigation

// =============================================================================
// Artwork Loading
// Batch sizes, timing, and throttling for async artwork loading.
// =============================================================================
namespace Artwork {
/// Target size for artwork scaling (pixels)
inline constexpr int BOX_SIZE = 400;
/// Batch size for high-priority (visible) artwork loading
inline constexpr int BATCH_HIGH = 10;
/// Batch size for low-priority (prefetch) artwork loading
inline constexpr int BATCH_LOW = 5;
/// Batch size for immediate viewport artwork
inline constexpr int IMMEDIATE_BATCH = 15;
/// Idle time before starting silent background loading
inline constexpr int SILENT_LOAD_IDLE_TIME_MS = 500;
/// Batch size for silent loading during scroll
inline constexpr int SILENT_LOAD_BATCH_SIZE = 30;
/// Default batch size for silent loading
inline constexpr int SILENT_LOAD_BATCH_SIZE_DEFAULT = 20;
/// Interval between silent load batches (higher = less CPU, slower precache)
inline constexpr int SILENT_LOAD_INTERVAL_MS = 200;
/// Delay before starting silent loading after scroll
inline constexpr int DEFER_SILENT_LOADING_DELAY_MS = 100;
/// Divisor for throttling silent load batch size
inline constexpr int SILENT_LOAD_THROTTLE_DIVISOR = 8;
/// Batch size for persistent silent load when idle
inline constexpr int PERSISTENT_SILENT_BATCH_IDLE = 8;
/// Batch size for persistent silent load when active (higher = faster but more
/// CPU)
inline constexpr int PERSISTENT_SILENT_BATCH_ACTIVE = 4;
/// Interval for persistent silent load operations (higher = less CPU, slower
/// precache)
inline constexpr int PERSISTENT_SILENT_LOAD_INTERVAL_MS = 300;
/// Minimum cooldown after a batch completes before starting another (prevents
/// CPU saturation)
inline constexpr int SILENT_LOAD_COOLDOWN_MS = 500;
/// Delay before starting silent load after items loaded
inline constexpr int START_SILENT_LOAD_AFTER_ITEMS_MS = 150;
/// Delay before reapplying filter after artwork load
inline constexpr int FILTER_REAPPLY_DELAY_MS = 150;
} // namespace Artwork

// =============================================================================
// Cache
// Memory cache sizing, disk persistence timing, and lazy loading.
// =============================================================================
namespace Cache {
/// Maximum pixmap cache size in KB (50 MB)
inline constexpr int PIXMAP_CACHE_KB = 1024 * 50;
/// Interval for lazy loading additional cache entries
inline constexpr int LAZY_LOAD_INTERVAL_MS = 500;
/// Batch size for lazy load operations
inline constexpr int LAZY_LOAD_BATCH_SIZE = 10;
/// Maximum entries in memory cache (LRU eviction)
inline constexpr int MAX_MEMORY_CACHE_SIZE = 256;
/// Minimum pixmap dimension to cache
inline constexpr int MIN_PIXMAP_SIZE = 200;
/// Delay after scroll stops before triggering silent load
inline constexpr int SCROLL_SILENT_LOAD_TRIGGER_DELAY_MS = 1500;
/// Time without scroll to consider inactivity
inline constexpr int SCROLL_INACTIVITY_MS = 500;
/// Delay before persisting cache changes to disk
inline constexpr int SAVE_DEFER_MS = 2000;
/// Interval between automatic cache saves (5 minutes)
inline constexpr int SAVE_INTERVAL_MS = 300000;
/// Check cache save condition every N operations
inline constexpr int CHECK_INTERVAL = 50;
/// Trigger save when cache grows by this factor
inline constexpr double SAVE_GROWTH_FACTOR = 1.2;
/// Quick save delay for urgent persistence
inline constexpr int QUICK_SAVE_DELAY_MS = 200;
/// Debounce interval for flush operations
inline constexpr int FLUSH_DEBOUNCE_MS = 1000;
} // namespace Cache

// =============================================================================
// Sidebar
// Metadata sidebar dimensions and timing.
// =============================================================================
namespace Sidebar {
/// Minimum sidebar width in pixels
inline constexpr int MIN_WIDTH = 150;
/// Maximum sidebar width in pixels
inline constexpr int MAX_WIDTH = 350;
/// Fixed sidebar width when visible
inline constexpr int FIXED_WIDTH = 300;
/// Margin around sidebar content
inline constexpr int MARGIN = 0;
/// Offset to account for scrollbar width
inline constexpr int SCROLLBAR_OFFSET = 80;
/// Additional margin offset for layout
inline constexpr int MARGIN_OFFSET = 40;
/// Delay before recalculating metrics after resize
inline constexpr int METRICS_RECALC_DELAY_MS = 200;
/// Delay before notifying layout change
inline constexpr int LAYOUT_NOTIFY_DELAY_MS = 100;
/// Delay before initial center scroll on show
inline constexpr int INITIAL_CENTER_SCROLL_DELAY_MS = 50;
} // namespace Sidebar

// =============================================================================
// Viewport
// Main content area sizing constraints.
// =============================================================================
namespace Viewport {
/// Minimum viewport width in pixels
inline constexpr int MIN_WIDTH = 600;
/// Default viewport width in pixels
inline constexpr int DEFAULT_WIDTH = 1200;
/// Minimum spacing adjustment (negative = tighter)
inline constexpr int SPACING_MIN = -100;
/// Maximum spacing adjustment (positive = looser)
inline constexpr int SPACING_MAX = 50;
} // namespace Viewport

// =============================================================================
// Widget Styling
// ItemWidget dimensions, borders, and pool sizing.
// =============================================================================
namespace Widget {
/// Margin around widget content
inline constexpr int MARGIN = 10;
/// Spacing between artwork and title
inline constexpr int SPACING = 8;
/// Internal padding for widget content
inline constexpr int PADDING = 20;
/// Margin around icons within widgets
inline constexpr int ICON_MARGIN = 5;
/// Large widget size preset
inline constexpr int LARGE_SIZE = 220;
/// Base height for widget calculation
inline constexpr int HEIGHT_BASE = 200;
/// Extra height added to base
inline constexpr int HEIGHT_EXTRA = 25;
/// Minimum icon size in widgets
inline constexpr int MIN_ICON_SIZE = 60;
/// Default icon size when not specified
inline constexpr int DEFAULT_ICON_SIZE = 60;
/// Size of subcollection indicator triangle
inline constexpr int TRIANGLE_SIZE = 12;
/// Offset from corner for triangle indicator
inline constexpr int TRIANGLE_OFFSET = 10;
/// Border width for selected items
inline constexpr int BORDER_WIDTH_SELECTION = 4;
/// Border radius for rounded corners
inline constexpr int BORDER_RADIUS = 5;
/// Factor for darkening highlight color
inline constexpr int HIGHLIGHT_DARKEN_FACTOR = 120;

/// Widget pool sizing constants
namespace Pool {
/// Minimum widgets to keep in pool
inline constexpr int MIN_SIZE = 20;
/// Maximum widgets to keep in pool
inline constexpr int MAX_SIZE = 100;
/// Multiplier for buffer calculation
inline constexpr int BUFFER_MULTIPLIER = 2;
/// Idle time before triggering pool prewarm (ms)
inline constexpr int PREWARM_IDLE_MS = 500;
} // namespace Pool
} // namespace Widget

// =============================================================================
// Metadata Display
// Metadata sidebar layout constants.
// =============================================================================
namespace Metadata {
/// Size for artwork preview in sidebar
inline constexpr int ARTWORK_SIZE = 200;
/// Maximum length before truncating file paths
inline constexpr int PATH_TRUNCATE_LENGTH = 50;
/// Bytes per KB for file size display
inline constexpr int FILE_SIZE_KB = 1024;
/// Spacing between metadata sections
inline constexpr int SECTION_SPACING = 12;
/// Spacing after label text
inline constexpr int LABEL_SPACING = 4;
/// Spacing between value lines
inline constexpr int VALUE_SPACING = 2;
/// Indentation for value text
inline constexpr int VALUE_INDENT = 12;
/// Padding around value text
inline constexpr int VALUE_PADDING = 2;
/// Height of separator lines
inline constexpr int SEPARATOR_HEIGHT = 8;
} // namespace Metadata

// =============================================================================
// Position Offsets
// Common offset values for layout calculations.
// =============================================================================
namespace Offset {
/// Small offset (5 pixels)
inline constexpr int SMALL = 5;
/// Medium offset (10 pixels)
inline constexpr int MEDIUM = 10;
/// Large offset (20 pixels)
inline constexpr int LARGE = 20;
} // namespace Offset

// =============================================================================
// Collection Icons
// Sizing for collection preview/icon display.
// =============================================================================
namespace CollectionIcon {
/// Bounding box size for collection icons
inline constexpr int BOX_SIZE = 200;
/// Maximum dimension for scaled icon
inline constexpr int MAX_SIZE = 180;
/// Spacing between items in collection preview
inline constexpr int ITEM_SPACING = 5;
} // namespace CollectionIcon

// =============================================================================
// Colors and Theming
// Color manipulation constants for theme-aware rendering.
// =============================================================================
namespace Color {
/// Saturation level for title tint (0-255)
inline constexpr int TITLE_TINT_SATURATION = 180;
/// Lightness level for title tint - lower = darker (0-255)
inline constexpr int TITLE_TINT_LIGHTNESS = 75;
/// Maximum channel value (255 for 8-bit color)
inline constexpr int CHANNEL_MAX = 255;
/// Base for hexadecimal color parsing
inline constexpr int HEX_BASE = 16;
} // namespace Color

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

// =============================================================================
// Dialog Sizes
// Dimensions for application dialogs.
// =============================================================================
namespace Dialog {
/// Width of the About dialog
inline constexpr int ABOUT_WIDTH = 400;
/// Height of the About dialog
inline constexpr int ABOUT_HEIGHT = 200;
} // namespace Dialog

// =============================================================================
// Theme Icons
// Breeze/Plasma icon names for visual indicators.
// Use with QIcon::fromTheme() or UIConstants::Icons::fromTheme().
// =============================================================================
namespace Icons {
/// Folder icon for collections
inline constexpr const char *FOLDER = "folder";
/// Open folder icon for subcollections
inline constexpr const char *SUBCOLLECTION = "folder-open";
/// Card file box icon for virtual folders (subfolder navigation)
inline constexpr const char *VIRTUAL_FOLDER = "folder-documents";
/// Magnifying glass for search
inline constexpr const char *SEARCH = "edit-find";
/// Search current collection only
inline constexpr const char *SEARCH_LOCAL = "edit-find";
/// Search current + subcollections
inline constexpr const char *SEARCH_SUBCOLLECTIONS = "folder-saved-search";
/// Search all collections (global)
inline constexpr const char *SEARCH_GLOBAL = "system-search";
/// Globe for global search mode
inline constexpr const char *GLOBE = "internet-services";
/// Image/picture icon for artwork preview
inline constexpr const char *IMAGE = "view-preview";

/// Get a themed icon with fallback support
/// @param names List of icon names to try in order
/// @param fallback Optional fallback text if no icon found
/// @return The first available icon, or empty icon if none found
inline QIcon fromTheme(std::initializer_list<const char *> names) {
  for (const char *name : names) {
    QIcon icon = QIcon::fromTheme(QString::fromUtf8(name));
    if (!icon.isNull()) {
      return icon;
    }
  }
  return {};
}

/// Get a themed icon by single name
inline QIcon fromTheme(const char *name) {
  return QIcon::fromTheme(QString::fromUtf8(name));
}
} // namespace Icons

} // namespace UIConstants

#endif
