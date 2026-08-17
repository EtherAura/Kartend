#ifndef GAMEPADHELPERS_H
#define GAMEPADHELPERS_H

#include <QString>

// Pure helpers extracted from GamepadManager. The manager owns Qt6::Gamepad
// or SDL2 backend lifecycle, a QTimer poll loop, and KeyboardManager-driven
// repeat dispatch; the helpers cover the input math in between — the
// axis-to-direction hysteresis, the vertical-priority direction combine,
// the SDL axis normalization, and the configurable button-to-action
// resolution. Pure logic so the test target only needs Qt6::Core.
namespace GamepadHelpers {

enum class Direction { None, Left, Right, Up, Down };

// Hysteresis-resolved digital direction bits derived from an analog stick.
// The thresholds asymmetry (on > off) prevents jitter at the deadzone:
// once active in a direction, the axis has to fall below the lower "off"
// threshold to release.
struct AxisDirectionState {
  bool left = false;
  bool right = false;
  bool up = false;
  bool down = false;
};

// Apply axis-to-direction hysteresis. previousDirection lets us widen the
// release threshold for whichever direction is currently active. When
// useStick is false (the user disabled stick input), all four bits are
// false regardless of axis values.
[[nodiscard]] AxisDirectionState axisToDirections(double axisX, double axisY,
                                                  Direction previousDirection, double onThreshold,
                                                  double offThreshold, bool useStick);

// Combine d-pad bits with stick-derived bits to pick a single direction.
// Vertical wins when both axes are pressed — matches the grid-nav feel
// the rest of Kartend uses. useDpad gates the d-pad bits identically to
// useStick gating the stick bits.
[[nodiscard]] Direction combineToDirection(bool dpadLeft, bool dpadRight, bool dpadUp,
                                           bool dpadDown, bool useDpad,
                                           const AxisDirectionState &stick);

// Movement intent the manager translates to a requestSelectionMove
// signal. delta=0 means "no movement"; vertical=true means the move is
// along the Y axis.
struct MovementIntent {
  int delta = 0;
  bool vertical = false;
};
[[nodiscard]] MovementIntent movementFor(Direction direction);

// Normalize an SDL Sint16 axis (range [-32768, 32767]) to [-1.0, 1.0].
// The negative max has a larger magnitude than the positive max, so the
// helper divides by 32768.0 for negative values and 32767.0 for
// positive values so both extremes reach exactly -1.0 / +1.0.
[[nodiscard]] double normalizeSdlAxis(int rawSint16);

// Action the manager should emit for a pressed button. None means
// the press isn't bound to any of the four actions (silently ignored).
// Comparisons are case-insensitive and an empty configured binding is
// treated as "unbound" — it doesn't match any incoming button.
enum class ButtonAction { None, Confirm, Back, ToggleSidebar, ToggleCollectionTree };

[[nodiscard]] ButtonAction resolveButtonAction(const QString &buttonName,
                                               const QString &confirmBinding,
                                               const QString &backBinding,
                                               const QString &toggleSidebarBinding,
                                               const QString &toggleCollectionTreeBinding);

} // namespace GamepadHelpers

#endif // GAMEPADHELPERS_H
