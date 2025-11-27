#ifndef UICONSTANTS_H
#define UICONSTANTS_H

#include <QString>

namespace UIConstants {

// =============================================================================
// Grid Layout
// =============================================================================
namespace Grid {
inline constexpr int DEFAULT_WIDTH = 6;
inline constexpr int MIN_WIDTH = 1;
inline constexpr int MAX_WIDTH = 40;
inline constexpr int SPACING = 20;
inline constexpr int MARGINS = 10;
inline constexpr int BUFFER_ROWS = 2;
inline constexpr int DEFAULT_VISIBLE_ROWS = 6;
inline constexpr int CONTAINER_OFFSET = -20;
inline constexpr int CONTAINER_LEFT_OFFSET = 20;
inline constexpr int CONTAINER_RIGHT_OFFSET = 16;
inline constexpr int CONTAINER_HEIGHT_BUFFER = 25;
} // namespace Grid

// Legacy aliases for backward compatibility
inline constexpr int DEFAULT_GRID_WIDTH = Grid::DEFAULT_WIDTH;
inline constexpr int MIN_GRID_WIDTH = Grid::MIN_WIDTH;
inline constexpr int MAX_GRID_WIDTH = Grid::MAX_WIDTH;
inline constexpr int GRID_SPACING = Grid::SPACING;
inline constexpr int GRID_MARGINS = Grid::MARGINS;
inline constexpr int BUFFER_ROWS = Grid::BUFFER_ROWS;
inline constexpr int DEFAULT_VISIBLE_ROWS = Grid::DEFAULT_VISIBLE_ROWS;
inline constexpr int CONTAINER_OFFSET = Grid::CONTAINER_OFFSET;
inline constexpr int CONTAINER_LEFT_OFFSET = Grid::CONTAINER_LEFT_OFFSET;
inline constexpr int CONTAINER_RIGHT_OFFSET = Grid::CONTAINER_RIGHT_OFFSET;
inline constexpr int CONTAINER_HEIGHT_BUFFER = Grid::CONTAINER_HEIGHT_BUFFER;

// =============================================================================
// Item Dimensions
// =============================================================================
namespace Item {
inline constexpr int WIDTH = 220;
inline constexpr int HEIGHT = 270;
inline constexpr int DEFAULT_WIDTH = 220;
inline constexpr int DEFAULT_HEIGHT = 245;
inline constexpr int MIN_WIDTH = 100;
inline constexpr int MIN_HEIGHT = 100;
inline constexpr int MAX_WIDTH = 3840;
inline constexpr int MAX_HEIGHT = 2160;
inline constexpr int MIN_FONT_SIZE = 4;
inline constexpr int DEFAULT_FONT_SIZE = 8;
inline constexpr int MAX_FONT_SIZE = 72;
} // namespace Item

// Legacy aliases
inline constexpr int ITEM_WIDTH = Item::WIDTH;
inline constexpr int ITEM_HEIGHT = Item::HEIGHT;
inline constexpr int DEFAULT_ITEM_WIDTH = Item::DEFAULT_WIDTH;
inline constexpr int DEFAULT_ITEM_HEIGHT = Item::DEFAULT_HEIGHT;
inline constexpr int MIN_ITEM_WIDTH = Item::MIN_WIDTH;
inline constexpr int MIN_ITEM_HEIGHT = Item::MIN_HEIGHT;
inline constexpr int MAX_ITEM_WIDTH = Item::MAX_WIDTH;
inline constexpr int MAX_ITEM_HEIGHT = Item::MAX_HEIGHT;
inline constexpr int MIN_FONT_SIZE = Item::MIN_FONT_SIZE;
inline constexpr int DEFAULT_FONT_SIZE = Item::DEFAULT_FONT_SIZE;
inline constexpr int MAX_FONT_SIZE = Item::MAX_FONT_SIZE;

// =============================================================================
// Timing - General Delays
// =============================================================================
namespace Timing {
inline constexpr int SHORT_DELAY_MS = 50;
inline constexpr int MEDIUM_DELAY_MS = 100;
inline constexpr int LONG_DELAY_MS = 200;
inline constexpr int STARTUP_DELAY_MS = 300;
inline constexpr int LAYOUT_UPDATE_DELAY_MS = 100;
inline constexpr int VIEWPORT_UPDATE_DELAY_MS = 100;
inline constexpr int SCROLL_THROTTLE_DELAY_MS = 200;
inline constexpr int TOOLTIP_DISPLAY_MS = 2000;
inline constexpr int TOOLTIP_DELAY_MS = 50;
inline constexpr int LAYOUT_DELAY_MS = 100;
inline constexpr int VIEWPORT_DELAY_MS = 300;
inline constexpr int SETTINGS_RETRY_BASE_MS = 50;
inline constexpr int USER_IDLE_THRESHOLD_MS = 2000;
} // namespace Timing

// Legacy aliases
inline constexpr int STARTUP_DELAY = Timing::STARTUP_DELAY_MS;
inline constexpr int LAYOUT_UPDATE_DELAY = Timing::LAYOUT_UPDATE_DELAY_MS;
inline constexpr int VIEWPORT_UPDATE_DELAY = Timing::VIEWPORT_UPDATE_DELAY_MS;
inline constexpr int SCROLL_THROTTLE_DELAY = Timing::SCROLL_THROTTLE_DELAY_MS;
inline constexpr int SHORT_TIMER_DELAY = Timing::SHORT_DELAY_MS;
inline constexpr int MEDIUM_TIMER_DELAY = Timing::MEDIUM_DELAY_MS;
inline constexpr int LONG_TIMER_DELAY = Timing::LONG_DELAY_MS;
inline constexpr int TOOLTIP_DISPLAY_TIME = Timing::TOOLTIP_DISPLAY_MS;
inline constexpr int TOOLTIP_DELAY = Timing::TOOLTIP_DELAY_MS;
inline constexpr int LAYOUT_DELAY = Timing::LAYOUT_DELAY_MS;
inline constexpr int VIEWPORT_DELAY = Timing::VIEWPORT_DELAY_MS;
inline constexpr int SETTINGS_RETRY_BASE = Timing::SETTINGS_RETRY_BASE_MS;
inline constexpr int USER_IDLE_THRESHOLD_MS = Timing::USER_IDLE_THRESHOLD_MS;

// =============================================================================
// Animation
// =============================================================================
namespace Animation {
// Scroll animation durations (consolidated - previously 8 redundant constants)
inline constexpr int CENTER_SCROLL_DURATION_MS = 1500;
inline constexpr int SMOOTH_SCROLL_MIN_DURATION_MS = 1500;
inline constexpr int SMOOTH_SCROLL_MAX_DURATION_MS = 1500;
inline constexpr int SMOOTH_SCROLL_WHEEL_DURATION_MS = 1500;
inline constexpr int HSCROLL_ANIM_DURATION_MS = 140;

// Pulse animation
inline constexpr int PULSE_DURATION_MS = 3000;
inline constexpr double PULSE_OPACITY_LOW = 0.25;
inline constexpr double PULSE_OPACITY_HIGH = 1.0;
inline constexpr int PULSE_INACTIVITY_DELAY_MS = 100;
inline constexpr double PULSE_KEYFRAME_MID_POS = 0.5;
} // namespace Animation

// Legacy aliases for scroll animation
inline constexpr int SMOOTH_SCROLL_MIN_DURATION = Animation::SMOOTH_SCROLL_MIN_DURATION_MS;
inline constexpr int SMOOTH_SCROLL_MAX_DURATION = Animation::SMOOTH_SCROLL_MAX_DURATION_MS;
inline constexpr int SMOOTH_SCROLL_WHEEL_DURATION = Animation::SMOOTH_SCROLL_WHEEL_DURATION_MS;
inline constexpr int CENTER_SCROLL_BASE_DURATION = Animation::CENTER_SCROLL_DURATION_MS;
inline constexpr int CENTER_SCROLL_BASE_DURATION_REPEAT = Animation::CENTER_SCROLL_DURATION_MS;
inline constexpr int CENTER_SCROLL_PER_ROW = Animation::CENTER_SCROLL_DURATION_MS;
inline constexpr int CENTER_SCROLL_PER_ROW_REPEAT = Animation::CENTER_SCROLL_DURATION_MS;
inline constexpr int CENTER_SCROLL_MIN_DURATION = Animation::CENTER_SCROLL_DURATION_MS;
inline constexpr int CENTER_SCROLL_MIN_DURATION_REPEAT = Animation::CENTER_SCROLL_DURATION_MS;
inline constexpr int CENTER_SCROLL_MAX_DURATION = Animation::CENTER_SCROLL_DURATION_MS;
inline constexpr int CENTER_SCROLL_MAX_DURATION_REPEAT = Animation::CENTER_SCROLL_DURATION_MS;
inline constexpr int HSCROLL_ANIM_DURATION_MS = Animation::HSCROLL_ANIM_DURATION_MS;
inline constexpr int PULSE_ANIMATION_DURATION = Animation::PULSE_DURATION_MS;
inline constexpr double PULSE_OPACITY_LOW = Animation::PULSE_OPACITY_LOW;
inline constexpr double PULSE_OPACITY_HIGH = Animation::PULSE_OPACITY_HIGH;
inline constexpr int PULSE_INACTIVITY_DELAY_MS = Animation::PULSE_INACTIVITY_DELAY_MS;
inline constexpr double PULSE_KEYFRAME_MID_POS = Animation::PULSE_KEYFRAME_MID_POS;

// =============================================================================
// Keyboard Navigation
// =============================================================================
namespace Keyboard {
inline constexpr int BASE_INTERVAL_MS = 260;
inline constexpr int VERTICAL_EXTRA_MS = 120;
inline constexpr int ANIMATION_SETTLE_MS = 200;
inline constexpr int REPEAT_START_DELAY_MS = 260;
inline constexpr int VIEW_UPDATE_INTERVAL_MS = 30;
inline constexpr int ARROW_CENTER_CLEAR_AFTER_RESTORE_MS = 300;
inline constexpr int ARROW_CENTER_EXTRA_SUPPRESS_AFTER_RESTORE_MS = 500;
inline constexpr int ARROW_CENTER_CLEAR_CHECK_DELAY_MS = 440;
inline constexpr int ARROW_CENTER_CLEAR_AFTER_SET_MS = 240;
} // namespace Keyboard

// Legacy aliases
inline constexpr int ARROW_KEY_BASE_INTERVAL_MS = Keyboard::BASE_INTERVAL_MS;
inline constexpr int ARROW_KEY_VERTICAL_EXTRA_MS = Keyboard::VERTICAL_EXTRA_MS;
inline constexpr int ARROW_KEY_ANIMATION_SETTLE_MS = Keyboard::ANIMATION_SETTLE_MS;
inline constexpr int KEY_REPEAT_START_DELAY_MS = Keyboard::REPEAT_START_DELAY_MS;
inline constexpr int ARROW_KEY_VIEW_UPDATE_INTERVAL_MS = Keyboard::VIEW_UPDATE_INTERVAL_MS;
inline constexpr int ARROW_CENTER_CLEAR_AFTER_RESTORE_MS = Keyboard::ARROW_CENTER_CLEAR_AFTER_RESTORE_MS;
inline constexpr int ARROW_CENTER_EXTRA_SUPPRESS_AFTER_RESTORE_MS = Keyboard::ARROW_CENTER_EXTRA_SUPPRESS_AFTER_RESTORE_MS;
inline constexpr int ARROW_CENTER_CLEAR_CHECK_DELAY_MS = Keyboard::ARROW_CENTER_CLEAR_CHECK_DELAY_MS;
inline constexpr int ARROW_CENTER_CLEAR_AFTER_SET_MS = Keyboard::ARROW_CENTER_CLEAR_AFTER_SET_MS;

// =============================================================================
// Mouse and Click Handling
// =============================================================================
namespace Mouse {
inline constexpr int CLICK_HOLD_START_MS = 500;
inline constexpr int CLICK_HOLD_HORIZONTAL_INTERVAL_MS = 320;
inline constexpr int WHEEL_SUPPRESS_ARROW_CENTER_MS = 420;
inline constexpr int DOUBLE_CLICK_POS_SUPPRESS_MS = 300;
inline constexpr int CONTINUOUS_SCROLL_IDLE_MS = 300;
inline constexpr int USER_SCROLL_IDLE_TIMER_MS = 240;
inline constexpr int USER_SCROLL_ACTIVE_CLEAR_DELAY_MS = 220;
inline constexpr int STOP_REPEAT_RECENTER_DELAY_MS = 10;
} // namespace Mouse

// Legacy aliases
inline constexpr int CLICK_HOLD_START_MS = Mouse::CLICK_HOLD_START_MS;
inline constexpr int CLICK_HOLD_HORIZONTAL_INTERVAL_MS = Mouse::CLICK_HOLD_HORIZONTAL_INTERVAL_MS;
inline constexpr int WHEEL_SUPPRESS_ARROW_CENTER_MS = Mouse::WHEEL_SUPPRESS_ARROW_CENTER_MS;
inline constexpr int DOUBLE_CLICK_POS_SUPPRESS_MS = Mouse::DOUBLE_CLICK_POS_SUPPRESS_MS;
inline constexpr int CONTINUOUS_SCROLL_IDLE_MS = Mouse::CONTINUOUS_SCROLL_IDLE_MS;
inline constexpr int USER_SCROLL_IDLE_TIMER_MS = Mouse::USER_SCROLL_IDLE_TIMER_MS;
inline constexpr int USER_SCROLL_ACTIVE_CLEAR_DELAY_MS = Mouse::USER_SCROLL_ACTIVE_CLEAR_DELAY_MS;
inline constexpr int STOP_REPEAT_RECENTER_DELAY_MS = Mouse::STOP_REPEAT_RECENTER_DELAY_MS;

// =============================================================================
// Search
// =============================================================================
namespace Search {
inline constexpr int DEBOUNCE_DELAY_MS = 120;
inline constexpr int TYPING_DEBOUNCE_MS = 180;
inline constexpr int REFOCUS_DELAY_SHORT_MS = 180;
inline constexpr int REFOCUS_DELAY_LONG_MS = 400;
} // namespace Search

// Legacy aliases
inline constexpr int SEARCH_DEBOUNCE_DELAY_MS = Search::DEBOUNCE_DELAY_MS;
inline constexpr int SEARCH_TYPING_DEBOUNCE_MS = Search::TYPING_DEBOUNCE_MS;
inline constexpr int SEARCH_REFOCUS_DELAY_SHORT_MS = Search::REFOCUS_DELAY_SHORT_MS;
inline constexpr int SEARCH_REFOCUS_DELAY_LONG_MS = Search::REFOCUS_DELAY_LONG_MS;

// =============================================================================
// Selection and Navigation
// =============================================================================
namespace Selection {
inline constexpr int RESTORE_STEPS = 6;
inline constexpr int RESTORE_STEP_DELAY_MS = 120;
inline constexpr int RESTORE_MAX_DELAY_MS = 900;
inline constexpr int RESTORE_EARLY_VERIFY_1_MS = 140;
inline constexpr int RESTORE_EARLY_VERIFY_2_MS = 320;
inline constexpr int DOUBLE_CLICK_SUPPRESS_AFTER_ENTER_MS = 700;
inline constexpr int DOUBLE_CLICK_SUPPRESS_CLEAR_DELAY_MS = 800;
} // namespace Selection

// Legacy aliases
inline constexpr int SELECTION_RESTORE_STEPS = Selection::RESTORE_STEPS;
inline constexpr int SELECTION_RESTORE_STEP_DELAY_MS = Selection::RESTORE_STEP_DELAY_MS;
inline constexpr int SELECTION_RESTORE_MAX_DELAY_MS = Selection::RESTORE_MAX_DELAY_MS;
inline constexpr int SELECTION_RESTORE_EARLY_VERIFY_1_MS = Selection::RESTORE_EARLY_VERIFY_1_MS;
inline constexpr int SELECTION_RESTORE_EARLY_VERIFY_2_MS = Selection::RESTORE_EARLY_VERIFY_2_MS;
inline constexpr int DOUBLE_CLICK_SUPPRESS_AFTER_ENTER_MS = Selection::DOUBLE_CLICK_SUPPRESS_AFTER_ENTER_MS;
inline constexpr int DOUBLE_CLICK_SUPPRESS_CLEAR_DELAY_MS = Selection::DOUBLE_CLICK_SUPPRESS_CLEAR_DELAY_MS;

// =============================================================================
// Navigation
// =============================================================================
namespace Navigation {
inline constexpr int PROGRESS_CLEAR_EARLY_MS = 200;
inline constexpr int PROGRESS_CLEAR_MS = 700;
inline constexpr int SUBCOLLECTION_SCROLL_CENTER_DELAY_MS = 600;
inline constexpr int SCROLLBAR_RECOVERY_ATTEMPT_1_MS = 90;
inline constexpr int SCROLLBAR_RECOVERY_ATTEMPT_2_MS = 240;
inline constexpr int SCROLLBAR_RECOVERY_ATTEMPT_3_MS = 460;
} // namespace Navigation

// Legacy aliases
inline constexpr int NAVIGATION_PROGRESS_CLEAR_EARLY_MS = Navigation::PROGRESS_CLEAR_EARLY_MS;
inline constexpr int NAVIGATION_PROGRESS_CLEAR_MS = Navigation::PROGRESS_CLEAR_MS;
inline constexpr int SUBCOLLECTION_SCROLL_CENTER_DELAY_MS = Navigation::SUBCOLLECTION_SCROLL_CENTER_DELAY_MS;
inline constexpr int SCROLLBAR_RECOVERY_ATTEMPT_1_MS = Navigation::SCROLLBAR_RECOVERY_ATTEMPT_1_MS;
inline constexpr int SCROLLBAR_RECOVERY_ATTEMPT_2_MS = Navigation::SCROLLBAR_RECOVERY_ATTEMPT_2_MS;
inline constexpr int SCROLLBAR_RECOVERY_ATTEMPT_3_MS = Navigation::SCROLLBAR_RECOVERY_ATTEMPT_3_MS;

// =============================================================================
// Artwork Loading
// =============================================================================
namespace Artwork {
inline constexpr int BOX_SIZE = 200;
inline constexpr int BATCH_HIGH = 10;
inline constexpr int BATCH_LOW = 5;
inline constexpr int IMMEDIATE_BATCH = 15;
inline constexpr int SILENT_LOAD_IDLE_TIME_MS = 500;
inline constexpr int SILENT_LOAD_BATCH_SIZE = 30;
inline constexpr int SILENT_LOAD_BATCH_SIZE_DEFAULT = 20;
inline constexpr int SILENT_LOAD_INTERVAL_MS = 25;
inline constexpr int DEFER_SILENT_LOADING_DELAY_MS = 100;
inline constexpr int SILENT_LOAD_THROTTLE_DIVISOR = 8;
inline constexpr int PERSISTENT_SILENT_BATCH_IDLE = 8;
inline constexpr int PERSISTENT_SILENT_BATCH_ACTIVE = 2;
inline constexpr int PERSISTENT_SILENT_LOAD_INTERVAL_MS = 50;
inline constexpr int START_SILENT_LOAD_AFTER_ITEMS_MS = 150;
inline constexpr int FILTER_REAPPLY_DELAY_MS = 150;
} // namespace Artwork

// Legacy aliases
inline constexpr int ARTWORK_BOX = Artwork::BOX_SIZE;
inline constexpr int ARTWORK_BATCH_HIGH = Artwork::BATCH_HIGH;
inline constexpr int ARTWORK_BATCH_LOW = Artwork::BATCH_LOW;
inline constexpr int ARTWORK_IMMEDIATE_BATCH = Artwork::IMMEDIATE_BATCH;
inline constexpr int SILENT_LOAD_IDLE_TIME = Artwork::SILENT_LOAD_IDLE_TIME_MS;
inline constexpr int SILENT_LOAD_BATCH_SIZE = Artwork::SILENT_LOAD_BATCH_SIZE;
inline constexpr int SILENT_LOAD_BATCH_SIZE_DEFAULT = Artwork::SILENT_LOAD_BATCH_SIZE_DEFAULT;
inline constexpr int SILENT_LOAD_INTERVAL = Artwork::SILENT_LOAD_INTERVAL_MS;
inline constexpr int DEFER_SILENT_LOADING_DELAY_MS = Artwork::DEFER_SILENT_LOADING_DELAY_MS;
inline constexpr int SILENT_LOAD_THROTTLE_DIVISOR = Artwork::SILENT_LOAD_THROTTLE_DIVISOR;
inline constexpr int PERSISTENT_SILENT_BATCH_IDLE = Artwork::PERSISTENT_SILENT_BATCH_IDLE;
inline constexpr int PERSISTENT_SILENT_BATCH_ACTIVE = Artwork::PERSISTENT_SILENT_BATCH_ACTIVE;
inline constexpr int PERSISTENT_SILENT_LOAD_INTERVAL_MS = Artwork::PERSISTENT_SILENT_LOAD_INTERVAL_MS;
inline constexpr int START_SILENT_LOAD_AFTER_ITEMS_MS = Artwork::START_SILENT_LOAD_AFTER_ITEMS_MS;
inline constexpr int FILTER_REAPPLY_DELAY_MS = Artwork::FILTER_REAPPLY_DELAY_MS;

// =============================================================================
// Cache
// =============================================================================
namespace Cache {
inline constexpr int PIXMAP_CACHE_KB = 1024 * 50;
inline constexpr int LAZY_LOAD_INTERVAL_MS = 500;
inline constexpr int LAZY_LOAD_BATCH_SIZE = 10;
inline constexpr int MAX_MEMORY_CACHE_SIZE = 256;
inline constexpr int MIN_PIXMAP_SIZE = 200;
inline constexpr int SCROLL_SILENT_LOAD_TRIGGER_DELAY_MS = 1500;
inline constexpr int SCROLL_INACTIVITY_MS = 500;
inline constexpr int SAVE_DEFER_MS = 2000;
inline constexpr int SAVE_INTERVAL_MS = 300000;
inline constexpr int CHECK_INTERVAL = 50;
inline constexpr double SAVE_GROWTH_FACTOR = 1.2;
inline constexpr int QUICK_SAVE_DELAY_MS = 200;
inline constexpr int FLUSH_DEBOUNCE_MS = 1000;
} // namespace Cache

// Legacy aliases
inline constexpr int PIXMAP_CACHE_KB = Cache::PIXMAP_CACHE_KB;
inline constexpr int LAZY_LOAD_INTERVAL = Cache::LAZY_LOAD_INTERVAL_MS;
inline constexpr int LAZY_LOAD_BATCH_SIZE = Cache::LAZY_LOAD_BATCH_SIZE;
inline constexpr int MAX_MEMORY_CACHE_SIZE = Cache::MAX_MEMORY_CACHE_SIZE;
inline constexpr int MIN_PIXMAP_SIZE = Cache::MIN_PIXMAP_SIZE;
inline constexpr int SCROLL_SILENT_LOAD_TRIGGER_DELAY_MS = Cache::SCROLL_SILENT_LOAD_TRIGGER_DELAY_MS;
inline constexpr int SCROLL_INACTIVITY_MS = Cache::SCROLL_INACTIVITY_MS;
inline constexpr int PERSISTENT_CACHE_SAVE_DEFER_MS = Cache::SAVE_DEFER_MS;
inline constexpr int PERSISTENT_CACHE_SAVE_INTERVAL_MS = Cache::SAVE_INTERVAL_MS;
inline constexpr int PERSISTENT_CACHE_CHECK_INTERVAL = Cache::CHECK_INTERVAL;
inline constexpr double PERSISTENT_CACHE_SAVE_GROWTH_FACTOR = Cache::SAVE_GROWTH_FACTOR;
inline constexpr int PERSISTENT_CACHE_QUICK_SAVE_DELAY_MS = Cache::QUICK_SAVE_DELAY_MS;
inline constexpr int ARTWORK_CACHE_FLUSH_DEBOUNCE_MS = Cache::FLUSH_DEBOUNCE_MS;

// =============================================================================
// Sidebar
// =============================================================================
namespace Sidebar {
inline constexpr int MIN_WIDTH = 150;
inline constexpr int MAX_WIDTH = 350;
inline constexpr int FIXED_WIDTH = 300;
inline constexpr int MARGIN = 0;
inline constexpr int SCROLLBAR_OFFSET = 80;
inline constexpr int MARGIN_OFFSET = 40;
inline constexpr int METRICS_RECALC_DELAY_MS = 200;
inline constexpr int LAYOUT_NOTIFY_DELAY_MS = 100;
inline constexpr int INITIAL_CENTER_SCROLL_DELAY_MS = 50;
} // namespace Sidebar

// Legacy aliases
inline constexpr int SIDEBAR_MIN_WIDTH = Sidebar::MIN_WIDTH;
inline constexpr int SIDEBAR_MAX_WIDTH = Sidebar::MAX_WIDTH;
inline constexpr int FIXED_SIDEBAR_WIDTH = Sidebar::FIXED_WIDTH;
inline constexpr int SIDEBAR_MARGIN = Sidebar::MARGIN;
inline constexpr int SIDEBAR_SCROLLBAR_OFFSET = Sidebar::SCROLLBAR_OFFSET;
inline constexpr int SIDEBAR_MARGIN_OFFSET = Sidebar::MARGIN_OFFSET;
inline constexpr int SIDEBAR_METRICS_RECALC_DELAY_MS = Sidebar::METRICS_RECALC_DELAY_MS;
inline constexpr int SIDEBAR_LAYOUT_NOTIFY_DELAY_MS = Sidebar::LAYOUT_NOTIFY_DELAY_MS;
inline constexpr int INITIAL_CENTER_SCROLL_DELAY_MS = Sidebar::INITIAL_CENTER_SCROLL_DELAY_MS;

// =============================================================================
// Viewport
// =============================================================================
namespace Viewport {
inline constexpr int MIN_WIDTH = 600;
inline constexpr int DEFAULT_WIDTH = 1200;
inline constexpr int SPACING_MIN = -100;
inline constexpr int SPACING_MAX = 50;
} // namespace Viewport

// Legacy aliases
inline constexpr int MIN_VIEWPORT_WIDTH = Viewport::MIN_WIDTH;
inline constexpr int DEFAULT_VIEWPORT_WIDTH = Viewport::DEFAULT_WIDTH;
inline constexpr int SPACING_MIN = Viewport::SPACING_MIN;
inline constexpr int SPACING_MAX = Viewport::SPACING_MAX;

// =============================================================================
// Widget Styling
// =============================================================================
namespace Widget {
inline constexpr int MARGIN = 10;
inline constexpr int SPACING = 0;
inline constexpr int PADDING = 20;
inline constexpr int ICON_MARGIN = 5;
inline constexpr int LARGE_SIZE = 220;
inline constexpr int HEIGHT_BASE = 200;
inline constexpr int HEIGHT_EXTRA = 25;
inline constexpr int MIN_ICON_SIZE = 60;
inline constexpr int DEFAULT_ICON_SIZE = 60;
inline constexpr int TRIANGLE_SIZE = 12;
inline constexpr int TRIANGLE_OFFSET = 10;
inline constexpr int BORDER_WIDTH_SELECTION = 4;
inline constexpr int BORDER_RADIUS = 5;
inline constexpr int HIGHLIGHT_DARKEN_FACTOR = 120;

// Widget Pool - sizes for recycling MediaItemWidgets
namespace Pool {
inline constexpr int MIN_SIZE = 20;           // Minimum pool size
inline constexpr int MAX_SIZE = 100;          // Maximum pool size cap
inline constexpr int BUFFER_MULTIPLIER = 2;   // Pool = visible widgets * multiplier
} // namespace Pool
} // namespace Widget

// Legacy aliases
inline constexpr int WIDGET_MARGIN = Widget::MARGIN;
inline constexpr int WIDGET_SPACING = Widget::SPACING;
inline constexpr int WIDGET_PADDING = Widget::PADDING;
inline constexpr int ICON_MARGIN = Widget::ICON_MARGIN;
inline constexpr int LARGE_WIDGET_SIZE = Widget::LARGE_SIZE;
inline constexpr int WIDGET_HEIGHT_BASE = Widget::HEIGHT_BASE;
inline constexpr int WIDGET_HEIGHT_EXTRA = Widget::HEIGHT_EXTRA;
inline constexpr int MIN_ICON_SIZE = Widget::MIN_ICON_SIZE;
inline constexpr int DEFAULT_ICON_SIZE = Widget::DEFAULT_ICON_SIZE;
inline constexpr int TRIANGLE_SIZE = Widget::TRIANGLE_SIZE;
inline constexpr int TRIANGLE_OFFSET = Widget::TRIANGLE_OFFSET;
inline constexpr int BORDER_WIDTH_SELECTION = Widget::BORDER_WIDTH_SELECTION;
inline constexpr int BORDER_RADIUS = Widget::BORDER_RADIUS;
inline constexpr int HIGHLIGHT_DARKEN_FACTOR = Widget::HIGHLIGHT_DARKEN_FACTOR;

// =============================================================================
// Metadata Display
// =============================================================================
namespace Metadata {
inline constexpr int ARTWORK_SIZE = 200;
inline constexpr int PATH_TRUNCATE_LENGTH = 50;
inline constexpr int FILE_SIZE_KB = 1024;
inline constexpr int SECTION_SPACING = 12;
inline constexpr int LABEL_SPACING = 4;
inline constexpr int VALUE_SPACING = 2;
inline constexpr int VALUE_INDENT = 12;
inline constexpr int VALUE_PADDING = 2;
inline constexpr int SEPARATOR_HEIGHT = 8;
} // namespace Metadata

// Legacy aliases
inline constexpr int METADATA_ARTWORK_SIZE = Metadata::ARTWORK_SIZE;
inline constexpr int PATH_TRUNCATE_LENGTH = Metadata::PATH_TRUNCATE_LENGTH;
inline constexpr int FILE_SIZE_KB = Metadata::FILE_SIZE_KB;
inline constexpr int SECTION_SPACING = Metadata::SECTION_SPACING;
inline constexpr int LABEL_SPACING = Metadata::LABEL_SPACING;
inline constexpr int VALUE_SPACING = Metadata::VALUE_SPACING;
inline constexpr int VALUE_INDENT = Metadata::VALUE_INDENT;
inline constexpr int METADATA_VALUE_PADDING = Metadata::VALUE_PADDING;
inline constexpr int METADATA_VALUE_INDENT = Metadata::VALUE_INDENT;
inline constexpr int SEPARATOR_HEIGHT = Metadata::SEPARATOR_HEIGHT;

// =============================================================================
// Position Offsets
// =============================================================================
namespace Offset {
inline constexpr int SMALL = 5;
inline constexpr int MEDIUM = 10;
inline constexpr int LARGE = 20;
} // namespace Offset

// Legacy aliases
inline constexpr int POSITION_OFFSET_SMALL = Offset::SMALL;
inline constexpr int POSITION_OFFSET_MEDIUM = Offset::MEDIUM;
inline constexpr int POSITION_OFFSET_LARGE = Offset::LARGE;

// =============================================================================
// Collection Icons
// =============================================================================
namespace CollectionIcon {
inline constexpr int BOX_SIZE = 200;
inline constexpr int MAX_SIZE = 180;
inline constexpr int ITEM_SPACING = 5;
} // namespace CollectionIcon

// Legacy aliases
inline constexpr int COLLECTION_ICON_BOX = CollectionIcon::BOX_SIZE;
inline constexpr int COLLECTION_ICON_MAX = CollectionIcon::MAX_SIZE;
inline constexpr int COLLECTION_ITEM_SPACING = CollectionIcon::ITEM_SPACING;

// =============================================================================
// Colors and Theming
// =============================================================================
namespace Color {
inline constexpr int TITLE_TINT_SATURATION = 100;
inline constexpr int TITLE_TINT_LIGHTNESS_OFFSET = 20;
inline constexpr int CHANNEL_MAX = 255;
inline constexpr int HEX_BASE = 16;
} // namespace Color

// Legacy aliases
inline constexpr int TITLE_TINT_SATURATION = Color::TITLE_TINT_SATURATION;
inline constexpr int TITLE_TINT_LIGHTNESS_OFFSET = Color::TITLE_TINT_LIGHTNESS_OFFSET;
inline constexpr int COLOR_CHANNEL_MAX = Color::CHANNEL_MAX;
inline constexpr int HEX_BASE = Color::HEX_BASE;

// =============================================================================
// Placeholder Pattern
// =============================================================================
namespace Placeholder {
inline constexpr int PRIMARY_DELTA_DARK = 300;
inline constexpr int PRIMARY_DELTA_LIGHT = -105;
inline constexpr int SECONDARY_DELTA_DARK = 55;
inline constexpr int SECONDARY_DELTA_LIGHT = -55;
inline constexpr int PRIMARY_ALPHA = 215;
inline constexpr int SECONDARY_ALPHA = 125;
inline constexpr int STEP_MIN = 5;
inline constexpr int STEP_MAX = 12;
inline constexpr int STEP_DIVISOR = 10;
inline constexpr int NOISE_AMPLITUDE = 3;
inline constexpr int NOISE_STRIDE = 4;
inline constexpr quint32 NOISE_SEED = 0x51A3D7U;
inline constexpr int NOISE_MASK = 0x07;
inline constexpr int NOISE_BIAS = 3;
inline constexpr int GRADIENT_TOP_ALPHA = 28;
inline constexpr int GRADIENT_BOTTOM_ALPHA = 12;
inline constexpr int PRIMARY_TINT_NUM = 3;
inline constexpr int PRIMARY_TINT_DEN = 5;
inline constexpr int SECONDARY_TINT_NUM = 4;
inline constexpr int SECONDARY_TINT_DEN = 5;
} // namespace Placeholder

// Legacy aliases (using static to avoid multiple definition issues)
static const int PLACEHOLDER_PRIMARY_DELTA_DARK = Placeholder::PRIMARY_DELTA_DARK;
static const int PLACEHOLDER_PRIMARY_DELTA_LIGHT = Placeholder::PRIMARY_DELTA_LIGHT;
static const int PLACEHOLDER_SECONDARY_DELTA_DARK = Placeholder::SECONDARY_DELTA_DARK;
static const int PLACEHOLDER_SECONDARY_DELTA_LIGHT = Placeholder::SECONDARY_DELTA_LIGHT;
static const int PLACEHOLDER_PRIMARY_ALPHA = Placeholder::PRIMARY_ALPHA;
static const int PLACEHOLDER_SECONDARY_ALPHA = Placeholder::SECONDARY_ALPHA;
static const int PLACEHOLDER_STEP_MIN = Placeholder::STEP_MIN;
static const int PLACEHOLDER_STEP_MAX = Placeholder::STEP_MAX;
static const int PLACEHOLDER_STEP_DIVISOR = Placeholder::STEP_DIVISOR;
static const int PLACEHOLDER_NOISE_AMPLITUDE = Placeholder::NOISE_AMPLITUDE;
static const int PLACEHOLDER_NOISE_STRIDE = Placeholder::NOISE_STRIDE;
static const quint32 PLACEHOLDER_NOISE_SEED = Placeholder::NOISE_SEED;
static const int PLACEHOLDER_NOISE_MASK = Placeholder::NOISE_MASK;
static const int PLACEHOLDER_NOISE_BIAS = Placeholder::NOISE_BIAS;
static const int PLACEHOLDER_GRADIENT_TOP_ALPHA = Placeholder::GRADIENT_TOP_ALPHA;
static const int PLACEHOLDER_GRADIENT_BOTTOM_ALPHA = Placeholder::GRADIENT_BOTTOM_ALPHA;
static const int PLACEHOLDER_PRIMARY_TINT_NUM = Placeholder::PRIMARY_TINT_NUM;
static const int PLACEHOLDER_PRIMARY_TINT_DEN = Placeholder::PRIMARY_TINT_DEN;
static const int PLACEHOLDER_SECONDARY_TINT_NUM = Placeholder::SECONDARY_TINT_NUM;
static const int PLACEHOLDER_SECONDARY_TINT_DEN = Placeholder::SECONDARY_TINT_DEN;

// =============================================================================
// Dialog Sizes
// =============================================================================
namespace Dialog {
inline constexpr int ABOUT_WIDTH = 400;
inline constexpr int ABOUT_HEIGHT = 200;
} // namespace Dialog

// Legacy aliases
inline constexpr int ABOUT_DIALOG_WIDTH = Dialog::ABOUT_WIDTH;
inline constexpr int ABOUT_DIALOG_HEIGHT = Dialog::ABOUT_HEIGHT;

// =============================================================================
// Emoji Icons
// =============================================================================
namespace Emoji {
inline const QString FOLDER = QStringLiteral("📁");
inline const QString SUBCOLLECTION = QStringLiteral("📂");
inline const QString SEARCH = QStringLiteral("🔎");
inline const QString GLOBE = QStringLiteral("🌍");
} // namespace Emoji

// Legacy aliases
inline const QString FOLDER_EMOJI = Emoji::FOLDER;
inline const QString SUBCOLLECTION_EMOJI = Emoji::SUBCOLLECTION;
inline const QString SEARCH_EMOJI = Emoji::SEARCH;
inline const QString GLOBE_EMOJI = Emoji::GLOBE;

} // namespace UIConstants

#endif
