#ifndef IMOUSEHOLDCONTROL_H
#define IMOUSEHOLDCONTROL_H

/**
 * @brief Button / click-hold role of the mouse layer (Kartend-dl0uz.2).
 *
 * The slice EventManager's press/release dispatch drives: left-button
 * tracking, the click-hold timer, the horizontal-hold candidate, and
 * stopping an active hold scroll. IMouseManager unions this role with
 * IMouseWheelState.
 *
 * Plain abstract class, not a QObject — MouseManager derives QObject
 * directly. Reached via ctx->mouseHold().
 */
class IMouseHoldControl {
public:
  virtual ~IMouseHoldControl() = default;

  [[nodiscard]] virtual bool isMouseHoldScrolling() const = 0;

  virtual void setLeftMouseDown(bool down) = 0;
  virtual void stopClickHoldTimer() = 0;
  virtual void clearHorizontalCandidate() = 0;
  virtual void stopMouseHoldScrolling() = 0;
};

#endif // IMOUSEHOLDCONTROL_H
