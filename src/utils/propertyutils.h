#ifndef PROPERTYUTILS_H
#define PROPERTYUTILS_H
namespace PropertyKeys {
inline constexpr const char* SuppressArtwork                 = "_suppress_artwork";
inline constexpr const char* AllowArtworkDuringSelection     = "_allow_artwork_during_selection";
inline constexpr const char* PreprimeGlide                   = "_preprime_glide";
inline constexpr const char* TrackedByArtwork                = "_tracked_by_artwork";
inline constexpr const char* DeferAllArtwork                 = "_defer_all_artwork";
inline constexpr const char* GlideAnimating                  = "_glide_animating";
inline constexpr const char* ArrowKeyScrolling               = "_arrow_key_scrolling";
inline constexpr const char* SuppressArrowCenter             = "_suppress_arrow_center";
inline constexpr const char* SuppressArrowCenterUntilMs      = "_suppress_arrow_center_until_ms";
inline constexpr const char* DeferCenterOnClick              = "_defer_center_on_click";
inline constexpr const char* DeferredCenterIndex             = "_deferred_center_index";
inline constexpr const char* SelectionSuppressed             = "_sel_suppressed";
inline constexpr const char* PendingSelectionIndex           = "_pending_sel_index";
inline constexpr const char* RowChangeFirstClickIndex        = "_row_change_first_click_index";
inline constexpr const char* RowChangeFirstClickMs           = "_row_change_first_click_ms";
inline constexpr const char* DoubleClickPending              = "_dc_pending";
inline constexpr const char* DoubleClickPendingIndex         = "_dc_pending_index";
inline constexpr const char* ClickDeferralActive             = "_click_deferral_active";
inline constexpr const char* ClickDeferralIndex              = "_click_deferral_index";
inline constexpr const char* ClickForceAnim                  = "_click_force_anim";
inline constexpr const char* SuppressInitialClickCenter      = "_suppress_initial_click_center";
inline constexpr const char* PendingInitialCenter            = "_pending_initial_center";
inline constexpr const char* ClickScroll                     = "_click_scroll";
inline constexpr const char* ClickContinuous                 = "_click_continuous";
inline constexpr const char* KeyContinuous                   = "_key_continuous";
inline constexpr const char* HorizHoldActive                 = "_horiz_hold_active";
inline constexpr const char* ArmFirstClickDelay              = "_arm_first_click_delay";
inline constexpr const char* LastUiActivityMs                = "_last_ui_activity_ms";
inline constexpr const char* HorizAnimActive                 = "_horiz_anim_active";
inline constexpr const char* HorizAnimGen                    = "_horiz_anim_gen";
inline constexpr const char* UserFreeScroll                  = "_user_free_scroll";
inline constexpr const char* ProgrammaticScroll              = "_programmatic_scroll";
inline constexpr const char* UserScrollActive                = "_user_scroll_active";
inline constexpr const char* ClickSeriesLastMs               = "_click_series_last_ms";
inline constexpr const char* SuppressDoubleClickUntilMs      = "_suppress_dc_until_ms";
inline constexpr const char* ClickHoldRowChange              = "_click_hold_row_change";
inline constexpr const char* ClickHoldAdvancing              = "_click_hold_advancing";
inline constexpr const char* StreamVertical                  = "_stream_vertical";
inline constexpr const char* HoldProgressPx                  = "_hold_progress_px";
inline constexpr const char* HoldVelocityPxPerMs             = "_hold_velocity_px_per_ms";
inline constexpr const char* StreamLastMs                    = "_stream_last_ms";
inline constexpr const char* DeferArtworkUpdate              = "_defer_artwork_update";
inline constexpr const char* ForcePlaceholder                = "_force_placeholder";
static constexpr const char* ClearedByEscape = "_cleared_by_escape";
static constexpr const char* SelectionRestoreToken = "_sel_restore_token";
static constexpr const char* SelectionRestorePending = "_sel_restore_pending";
}
#endif