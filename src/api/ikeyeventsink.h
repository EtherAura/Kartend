#ifndef IKEYEVENTSINK_H
#define IKEYEVENTSINK_H

#include <QtGlobal>

QT_BEGIN_NAMESPACE
class QKeyEvent;
QT_END_NAMESPACE

/**
 * @brief Key-event-delegation role of the keyboard layer (Kartend-dl0uz.2).
 *
 * The slice EventManager's event filter forwards into: raw key press /
 * release delivery. IKeyboardManager unions this role; the key-repeat,
 * wrap-navigation and continuous-scroll state surface stays on
 * IKeyboardManager itself.
 *
 * Plain abstract class, not a QObject — KeyboardManager derives QObject
 * directly. Reached via ctx->keyEventSink().
 */
class IKeyEventSink {
public:
  virtual ~IKeyEventSink() = default;

  [[nodiscard]] virtual bool handleKeyPress(QKeyEvent *event, bool searchBarFocused) = 0;
  [[nodiscard]] virtual bool handleKeyRelease(QKeyEvent *event) = 0;
};

#endif // IKEYEVENTSINK_H
