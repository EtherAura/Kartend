#ifndef UICONSTANTS_H
#define UICONSTANTS_H

#include <QString>

namespace UIConstants {

constexpr int DEFAULT_GRID_WIDTH = 6;
constexpr int MIN_GRID_WIDTH = 1;
constexpr int MAX_GRID_WIDTH = 40;

constexpr int ITEM_WIDTH = 220;
constexpr int ITEM_HEIGHT = 270;
constexpr int GRID_SPACING = 20;
constexpr int GRID_MARGINS = 10;
constexpr int CONTAINER_OFFSET = -20;
constexpr int CONTAINER_LEFT_OFFSET = 20;
constexpr int CONTAINER_RIGHT_OFFSET = 16;

constexpr int PIXMAP_CACHE_KB = 1024 * 50;

constexpr int STARTUP_DELAY = 300;
constexpr int LAYOUT_UPDATE_DELAY = 100;
constexpr int VIEWPORT_UPDATE_DELAY = 100;
constexpr int SCROLL_THROTTLE_DELAY = 200;
constexpr int BUFFER_ROWS = 2;
constexpr int DEFAULT_VISIBLE_ROWS = 6;

constexpr int ARTWORK_BOX = 200;
constexpr int COLLECTION_ICON_BOX = 200;
constexpr int COLLECTION_ICON_MAX = 180;

constexpr int SIDEBAR_MIN_WIDTH = 150;
constexpr int SIDEBAR_MAX_WIDTH = 350;
constexpr int SIDEBAR_MARGIN = 0;
constexpr int SIDEBAR_SCROLLBAR_OFFSET = 80;

constexpr int ARTWORK_BATCH_HIGH = 10;
constexpr int ARTWORK_BATCH_LOW = 5;
constexpr int ARTWORK_IMMEDIATE_BATCH = 15;

constexpr int LAZY_LOAD_INTERVAL = 500;
constexpr int LAZY_LOAD_BATCH_SIZE = 10;
constexpr int MAX_MEMORY_CACHE_SIZE = 256;
constexpr int MIN_PIXMAP_SIZE = 200;

constexpr int PULSE_ANIMATION_DURATION = 3000;
constexpr double PULSE_OPACITY_LOW = 0.25;
constexpr double PULSE_OPACITY_HIGH = 1.0;
inline constexpr int PULSE_INACTIVITY_DELAY_MS = 100;

constexpr int TOOLTIP_DISPLAY_TIME = 2000;
constexpr int SHORT_TIMER_DELAY = 50;
constexpr int MEDIUM_TIMER_DELAY = 100;
constexpr int LONG_TIMER_DELAY = 200;
constexpr int ABOUT_DIALOG_WIDTH = 400;
constexpr int ABOUT_DIALOG_HEIGHT = 200;
// Named delays for specific UX flows
inline constexpr int SEARCH_DEBOUNCE_DELAY_MS =
    120; // was hardcoded in MainWindow
// Specific typing debounce used in InteractionManager when handling search
constexpr int SEARCH_TYPING_DEBOUNCE_MS = 180;
// Secondary refocus timing after clears while typing
constexpr int SEARCH_REFOCUS_DELAY_SHORT_MS = 180;
constexpr int SEARCH_REFOCUS_DELAY_LONG_MS = 400;
inline constexpr int SIDEBAR_METRICS_RECALC_DELAY_MS =
    200; // was hardcoded in MainWindow
inline constexpr int INITIAL_CENTER_SCROLL_DELAY_MS =
    50; // was hardcoded in UIManager

constexpr int MIN_VIEWPORT_WIDTH = 600;
constexpr int DEFAULT_VIEWPORT_WIDTH = 1200;
constexpr int FIXED_SIDEBAR_WIDTH = 300;
constexpr int SIDEBAR_MARGIN_OFFSET = 40;

constexpr int SPACING_MIN = -100;
constexpr int SPACING_MAX = 50;

constexpr int PATH_TRUNCATE_LENGTH = 50;
constexpr int METADATA_ARTWORK_SIZE = 200;
constexpr int FILE_SIZE_KB = 1024;

constexpr int POSITION_OFFSET_SMALL = 5;
constexpr int POSITION_OFFSET_MEDIUM = 10;
constexpr int POSITION_OFFSET_LARGE = 20;

constexpr int DEFAULT_ITEM_WIDTH = 220;
constexpr int DEFAULT_ITEM_HEIGHT = 245;
constexpr int MIN_ITEM_WIDTH = 100;
constexpr int MIN_ITEM_HEIGHT = 100;
constexpr int MAX_ITEM_WIDTH = 3840;
constexpr int MAX_ITEM_HEIGHT = 2160;
constexpr int MIN_FONT_SIZE = 4;
constexpr int DEFAULT_FONT_SIZE = 8;
constexpr int MAX_FONT_SIZE = 72;

constexpr int COLLECTION_ITEM_SPACING = 5;
constexpr int METADATA_VALUE_PADDING = 2;
constexpr int METADATA_VALUE_INDENT = 12;
constexpr int DEFAULT_ICON_SIZE = 60;
constexpr int BORDER_WIDTH_SELECTION = 4;
constexpr int BORDER_RADIUS = 5;

constexpr int WIDGET_MARGIN = 10;
constexpr int WIDGET_SPACING = 0;
constexpr int WIDGET_PADDING = 20;
constexpr int ICON_MARGIN = 5;

constexpr int LARGE_WIDGET_SIZE = 220;
constexpr int WIDGET_HEIGHT_BASE = 200;
constexpr int WIDGET_HEIGHT_EXTRA = 25;
constexpr int MIN_ICON_SIZE = 60;
constexpr int TRIANGLE_SIZE = 12;
constexpr int TRIANGLE_OFFSET = 10;
inline constexpr int HIGHLIGHT_DARKEN_FACTOR = 120; // used for triangle outline

constexpr int TOOLTIP_DELAY = 50;
constexpr int LAYOUT_DELAY = 100;
constexpr int VIEWPORT_DELAY = 300;
constexpr int SETTINGS_RETRY_BASE = 50;

constexpr int SECTION_SPACING = 12;
constexpr int LABEL_SPACING = 4;
constexpr int VALUE_SPACING = 2;
constexpr int VALUE_INDENT = 12;
constexpr int SEPARATOR_HEIGHT = 8;

constexpr int CONTAINER_HEIGHT_BUFFER = 25;

inline const QString FOLDER_EMOJI = QStringLiteral("📁");
inline const QString SUBCOLLECTION_EMOJI = QStringLiteral("📂");
inline const QString SEARCH_EMOJI = QStringLiteral("🔎");
inline const QString GLOBE_EMOJI = QStringLiteral("🌍");

constexpr int SILENT_LOAD_IDLE_TIME = 500;
constexpr int SILENT_LOAD_BATCH_SIZE = 30;
// Default burst size for continuous silent loading in ArtworkManager
constexpr int SILENT_LOAD_BATCH_SIZE_DEFAULT = 20;
constexpr int SILENT_LOAD_INTERVAL = 25;
// Delay before starting silent load after adding pending artwork when idle
constexpr int DEFER_SILENT_LOADING_DELAY_MS = 100;
// When user is active, throttle continuous silent load by this divisor
constexpr int SILENT_LOAD_THROTTLE_DIVISOR = 8;

// Persistent silent loading (low-frequency) batch sizes
constexpr int PERSISTENT_SILENT_BATCH_IDLE = 8;
constexpr int PERSISTENT_SILENT_BATCH_ACTIVE = 2;
// Interval between persistent silent load ticks
constexpr int PERSISTENT_SILENT_LOAD_INTERVAL_MS = 50;

constexpr int USER_IDLE_THRESHOLD_MS = 2000;
constexpr int SCROLL_SILENT_LOAD_TRIGGER_DELAY_MS = 1500;
constexpr int SCROLL_INACTIVITY_MS = 500;
constexpr int PERSISTENT_CACHE_SAVE_DEFER_MS = 2000;
// Interval for periodic cache saves (e.g., every 5 minutes)
constexpr int PERSISTENT_CACHE_SAVE_INTERVAL_MS = 300000;
// Check frequency for deciding whether to trigger a deferred cache save
constexpr int PERSISTENT_CACHE_CHECK_INTERVAL = 50;
// Minimum growth factor of on-disk cache size to trigger a deferred save
constexpr double PERSISTENT_CACHE_SAVE_GROWTH_FACTOR = 1.2;
// Quick-save small delay used after selection persistence
constexpr int PERSISTENT_CACHE_QUICK_SAVE_DELAY_MS = 200;
constexpr int ARTWORK_CACHE_FLUSH_DEBOUNCE_MS = 1000;
constexpr int SIDEBAR_LAYOUT_NOTIFY_DELAY_MS = 100;

constexpr int ARROW_KEY_BASE_INTERVAL_MS =
    260; // Base delay between repeated arrow moves
constexpr int CLICK_HOLD_HORIZONTAL_INTERVAL_MS =
    320; // Delay for horizontal click-hold (slower to allow animation)
constexpr int ARROW_KEY_VERTICAL_EXTRA_MS =
    120; // Extra delay for Up/Down (rows) vs Left/Right
constexpr int ARROW_KEY_ANIMATION_SETTLE_MS =
    200; // Time after last move to disable arrow-key scrolling mode

constexpr int SMOOTH_SCROLL_MIN_DURATION = 1500;
constexpr int SMOOTH_SCROLL_MAX_DURATION = 1500;
constexpr int SMOOTH_SCROLL_WHEEL_DURATION = 1500;

inline constexpr int CENTER_SCROLL_BASE_DURATION = 1500;
inline constexpr int CENTER_SCROLL_BASE_DURATION_REPEAT = 1500;
inline constexpr int CENTER_SCROLL_PER_ROW = 1500;
inline constexpr int CENTER_SCROLL_PER_ROW_REPEAT = 1500;
inline constexpr int CENTER_SCROLL_MIN_DURATION = 1500;
inline constexpr int CENTER_SCROLL_MIN_DURATION_REPEAT = 1500;
inline constexpr int CENTER_SCROLL_MAX_DURATION = 1500;
inline constexpr int CENTER_SCROLL_MAX_DURATION_REPEAT = 1500;

constexpr int TITLE_TINT_SATURATION = 100;
constexpr int TITLE_TINT_LIGHTNESS_OFFSET = 20;
constexpr int COLOR_CHANNEL_MAX = 255;
constexpr int HEX_BASE = 16;
constexpr double PULSE_KEYFRAME_MID_POS = 0.5;

// Idle delay after direct scrollbar press before disabling continuous scroll
constexpr int CONTINUOUS_SCROLL_IDLE_MS = 300;

// Suppress arrow-centering briefly during wheel scrolling
constexpr int WHEEL_SUPPRESS_ARROW_CENTER_MS = 420;

// Guard window to prevent double-processing of double-click positions
constexpr int DOUBLE_CLICK_POS_SUPPRESS_MS = 300;

// Delay before click-hold scrolling begins
constexpr int CLICK_HOLD_START_MS = 500;

// Default horizontal scroll animation duration when nudging visibility
constexpr int HSCROLL_ANIM_DURATION_MS = 140;

// Extra suppression time added after ARROW_KEY_ANIMATION_SETTLE_MS
constexpr int ARROW_CENTER_EXTRA_SUPPRESS_AFTER_RESTORE_MS = 500;

// Delay to clear suppress flag after immediate selection restore
constexpr int ARROW_CENTER_CLEAR_AFTER_RESTORE_MS = 300;
constexpr int ARROW_KEY_VIEW_UPDATE_INTERVAL_MS = 30;
constexpr int USER_SCROLL_IDLE_TIMER_MS = 240;
constexpr int USER_SCROLL_ACTIVE_CLEAR_DELAY_MS = 220;
constexpr int ARROW_CENTER_CLEAR_CHECK_DELAY_MS = 440;
constexpr int NAVIGATION_PROGRESS_CLEAR_EARLY_MS = 200;

static const int PLACEHOLDER_PRIMARY_DELTA_DARK = 300;
static const int PLACEHOLDER_PRIMARY_DELTA_LIGHT = -105;
static const int PLACEHOLDER_SECONDARY_DELTA_DARK = 55;
static const int PLACEHOLDER_SECONDARY_DELTA_LIGHT = -55;

static const int PLACEHOLDER_PRIMARY_ALPHA = 215;
static const int PLACEHOLDER_SECONDARY_ALPHA = 125;

static const int PLACEHOLDER_STEP_MIN = 5;
static const int PLACEHOLDER_STEP_MAX = 12;
static const int PLACEHOLDER_STEP_DIVISOR = 10;

static const int PLACEHOLDER_NOISE_AMPLITUDE = 3; // micro noise (+/-)
static const int PLACEHOLDER_NOISE_STRIDE = 4; // apply every N pixels both axes
static const quint32 PLACEHOLDER_NOISE_SEED = 0x51A3D7U;
static const int PLACEHOLDER_NOISE_MASK = 0x07; // matches previous behavior
static const int PLACEHOLDER_NOISE_BIAS = 3;    // subtract to center at 0

static const int PLACEHOLDER_GRADIENT_TOP_ALPHA = 28;
static const int PLACEHOLDER_GRADIENT_BOTTOM_ALPHA = 12;

static const int PLACEHOLDER_PRIMARY_TINT_NUM =
    3; // blend numerator for primary (primary*num + tint*(den-num))
static const int PLACEHOLDER_PRIMARY_TINT_DEN = 5;
static const int PLACEHOLDER_SECONDARY_TINT_NUM = 4;
static const int PLACEHOLDER_SECONDARY_TINT_DEN = 5;

// InteractionManager scrollbar recovery retry schedule
constexpr int SCROLLBAR_RECOVERY_ATTEMPT_1_MS = 90;
constexpr int SCROLLBAR_RECOVERY_ATTEMPT_2_MS = 240;
constexpr int SCROLLBAR_RECOVERY_ATTEMPT_3_MS = 460;

// Delay before recentering after stopping repeat navigation
constexpr int STOP_REPEAT_RECENTER_DELAY_MS = 10;

// Key repeat start delay (time before hold-repeat begins)
constexpr int KEY_REPEAT_START_DELAY_MS = 260;

// Selection restore schedule used when reloading collections
constexpr int SELECTION_RESTORE_STEPS = 6;
constexpr int SELECTION_RESTORE_STEP_DELAY_MS = 120;
constexpr int SELECTION_RESTORE_MAX_DELAY_MS = 900;
constexpr int SELECTION_RESTORE_EARLY_VERIFY_1_MS = 140;
constexpr int SELECTION_RESTORE_EARLY_VERIFY_2_MS = 320;

constexpr int NAVIGATION_PROGRESS_CLEAR_MS = 700;
constexpr int SUBCOLLECTION_SCROLL_CENTER_DELAY_MS = 600;
constexpr int DOUBLE_CLICK_SUPPRESS_AFTER_ENTER_MS = 700;
constexpr int DOUBLE_CLICK_SUPPRESS_CLEAR_DELAY_MS = 800;
constexpr int START_SILENT_LOAD_AFTER_ITEMS_MS = 150;
constexpr int FILTER_REAPPLY_DELAY_MS = 150;
constexpr int ARROW_CENTER_CLEAR_AFTER_SET_MS = 240;

} // namespace UIConstants

#endif