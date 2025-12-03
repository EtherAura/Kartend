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

// =============================================================================
// Item Dimensions
// =============================================================================
namespace Item {
// Fixed dimensions used by ItemWidget when no collection-specific values are set
inline constexpr int DEFAULT_WIDTH = 220;
inline constexpr int DEFAULT_HEIGHT = 245;
// Validation bounds for collection configuration
inline constexpr int MIN_WIDTH = 100;
inline constexpr int MIN_HEIGHT = 100;
inline constexpr int MAX_WIDTH = 3840;
inline constexpr int MAX_HEIGHT = 2160;
// Font size bounds and default
inline constexpr int MIN_FONT_SIZE = 4;
inline constexpr int DEFAULT_FONT_SIZE = 8;
inline constexpr int MAX_FONT_SIZE = 72;
// Corner radius bounds and default
inline constexpr int MIN_CORNER_RADIUS = 0;
inline constexpr int DEFAULT_CORNER_RADIUS = 0;
inline constexpr int MAX_CORNER_RADIUS = 100;
} // namespace Item

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
inline constexpr int RESIZE_RECENTER_DELAY_MS = 150;
} // namespace Timing

// =============================================================================
// Animation
// =============================================================================
namespace Animation {
inline constexpr int CENTER_SCROLL_DURATION_MS = 1500;
inline constexpr int SMOOTH_SCROLL_MIN_DURATION_MS = 1500;
inline constexpr int SMOOTH_SCROLL_MAX_DURATION_MS = 1500;
inline constexpr int SMOOTH_SCROLL_WHEEL_DURATION_MS = 1500;
inline constexpr int HSCROLL_ANIM_DURATION_MS = 140;
inline constexpr int PULSE_DURATION_MS = 3000;
inline constexpr double PULSE_OPACITY_LOW = 0.25;
inline constexpr double PULSE_OPACITY_HIGH = 1.0;
inline constexpr int PULSE_INACTIVITY_DELAY_MS = 100;
inline constexpr double PULSE_KEYFRAME_MID_POS = 0.5;
} // namespace Animation

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
inline constexpr int WHEEL_ANGLE_STEP = 120;
inline constexpr int WHEEL_PIXEL_STEP = 120;
} // namespace Mouse

// =============================================================================
// Search
// =============================================================================
namespace Search {
inline constexpr int DEBOUNCE_DELAY_MS = 120;
inline constexpr int TYPING_DEBOUNCE_MS = 180;
inline constexpr int REFOCUS_DELAY_SHORT_MS = 180;
inline constexpr int REFOCUS_DELAY_LONG_MS = 400;
} // namespace Search

// =============================================================================
// Selection
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

// =============================================================================
// Viewport
// =============================================================================
namespace Viewport {
inline constexpr int MIN_WIDTH = 600;
inline constexpr int DEFAULT_WIDTH = 1200;
inline constexpr int SPACING_MIN = -100;
inline constexpr int SPACING_MAX = 50;
} // namespace Viewport

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

namespace Pool {
inline constexpr int MIN_SIZE = 20;
inline constexpr int MAX_SIZE = 100;
inline constexpr int BUFFER_MULTIPLIER = 2;
} // namespace Pool
} // namespace Widget

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

// =============================================================================
// Position Offsets
// =============================================================================
namespace Offset {
inline constexpr int SMALL = 5;
inline constexpr int MEDIUM = 10;
inline constexpr int LARGE = 20;
} // namespace Offset

// =============================================================================
// Collection Icons
// =============================================================================
namespace CollectionIcon {
inline constexpr int BOX_SIZE = 200;
inline constexpr int MAX_SIZE = 180;
inline constexpr int ITEM_SPACING = 5;
} // namespace CollectionIcon

// =============================================================================
// Colors and Theming
// =============================================================================
namespace Color {
inline constexpr int TITLE_TINT_SATURATION = 180;
inline constexpr int TITLE_TINT_LIGHTNESS = 50;  // Absolute lightness (0-255), lower = darker
inline constexpr int CHANNEL_MAX = 255;
inline constexpr int HEX_BASE = 16;
} // namespace Color

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

// =============================================================================
// Dialog Sizes
// =============================================================================
namespace Dialog {
inline constexpr int ABOUT_WIDTH = 400;
inline constexpr int ABOUT_HEIGHT = 200;
} // namespace Dialog

// =============================================================================
// Emoji Icons
// =============================================================================
namespace Emoji {
inline const QString FOLDER = QStringLiteral("📁");
inline const QString SUBCOLLECTION = QStringLiteral("📂");
inline const QString SEARCH = QStringLiteral("🔎");
inline const QString GLOBE = QStringLiteral("🌍");
} // namespace Emoji

} // namespace UIConstants

#endif
