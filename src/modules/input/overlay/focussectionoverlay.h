#ifndef FOCUSSECTIONOVERLAY_H
#define FOCUSSECTIONOVERLAY_H

#include <QPixmap>
#include <QPointer>
#include <QString>
#include <QWidget>

/// Transient HUD shown while the gamepad's section modifier (Select) is
/// HELD (user request 2026-08-18: "when the modifier is active, there
/// should be an indicator, and the unfocused areas should be desaturated
/// temporarily").
///
/// Implementation note: the desaturation is a SNAPSHOT, not a live effect.
/// activate() grabs the content widget once, greys the image, and paints
/// that; a QGraphicsColorizeEffect on the items grid would re-render
/// thousands of pooled item widgets every frame for a HUD that is only up
/// while a button is held. The focused section is cut out of the widget
/// MASK instead of being painted around, so the live section shows through
/// with no compositing subtleties — and the indicator pill is added back
/// into the mask so it stays visible even when the focused section sits
/// under it.
class FocusSectionOverlay : public QWidget {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(FocusSectionOverlay)

public:
  explicit FocusSectionOverlay(QWidget *parent = nullptr);
  ~FocusSectionOverlay() override;

  /// Grab @p content, desaturate it, and show the HUD with @p focused cut
  /// out and labelled @p label. Re-grabs on every activation so the frozen
  /// image always matches what the user was looking at.
  void activate(QWidget *content, QWidget *focused, const QString &label);
  /// Move the cut-out to a different section without re-grabbing — the
  /// underlying content has not changed while the modifier is held.
  void updateFocus(QWidget *focused, const QString &label);
  void deactivate();
  [[nodiscard]] bool isActive() const { return m_active; }

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  void rebuildMask();
  [[nodiscard]] QRect pillRect() const;

  bool m_active = false;
  QPixmap m_snapshot;
  QString m_label;
  QPointer<QWidget> m_focused;
};

#endif // FOCUSSECTIONOVERLAY_H
