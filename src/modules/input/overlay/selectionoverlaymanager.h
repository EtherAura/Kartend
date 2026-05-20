#ifndef SELECTIONOVERLAYMANAGER_H
#define SELECTIONOVERLAYMANAGER_H

#include <QObject>
#include <QPointer>
#include <QPropertyAnimation>
#include <QRect>
#include <QWidget>
class ItemWidget;
class InteractionStateHolder;
class OverlayLayerManager;

// Manages the selection overlay widget and its glide animation
// The overlay provides a visual selection indicator that animates between items
class SelectionOverlayManager : public QObject {
  Q_OBJECT
public:
  explicit SelectionOverlayManager(QObject *parent = nullptr);
  ~SelectionOverlayManager() override;

  // Set the parent widget for the overlay
  void setParentWidget(QWidget *parent);

  // Set the grid container for GlideAnimating property
  void setGridContainer(QWidget *gridContainer);

  // Set the interaction state holder
  void setInteractionState(InteractionStateHolder *state);

  /// Wire the centralized z-order coordinator. The owned overlay widget is
  /// registered with the manager at the SelectionHighlight layer the first
  /// time it's created (ensureOverlay()). Subsequent show / animate calls
  /// route their raise() through the manager so the selection highlight
  /// restacks deterministically against every other overlay.
  void setLayerManager(OverlayLayerManager *manager);

  // Force overlay visibility (e.g., during click-hold scrolling)
  void setForceVisible(bool force);
  [[nodiscard]] bool isForceVisible() const { return m_forceVisible; }

  // Check if overlay should remain visible
  [[nodiscard]] bool shouldKeepVisible() const;

  // Show overlay at a specific widget
  void showAtWidget(ItemWidget *widget);

  // Show overlay at a specific rect
  void showAtRect(const QRect &rect);

  // Hide the overlay
  void hide();

  // Raise the overlay above other widgets
  void raise();

  // Check if overlay is visible
  [[nodiscard]] bool isVisible() const;

  // Animate overlay from current position to target rect
  void animateTo(const QRect &targetRect, const QRect &startRect = QRect());

  // Stop any running animation
  void stopAnimation();

  // Check if animation is running
  [[nodiscard]] bool isAnimating() const;

  // Get current overlay geometry
  [[nodiscard]] QRect geometry() const;

  // Set overlay geometry directly
  void setGeometry(const QRect &rect);

  // Calculate overlay rect for a widget
  [[nodiscard]] static QRect overlayRectForWidget(ItemWidget *widget);

  // Calculate overlay rect for grid position
  [[nodiscard]] static QRect overlayRectForPosition(const QPoint &pos, int itemWidth,
                                                    int itemHeight);

signals:
  // Emitted when glide animation finishes
  void animationFinished();

private:
  void ensureOverlay();
  void ensureAnimation();
  void updateOverlayStyle();

  QWidget *m_parentWidget = nullptr;
  QWidget *m_gridContainer = nullptr;
  InteractionStateHolder *m_state = nullptr;
  QPointer<QWidget> m_overlay;
  QPointer<QPropertyAnimation> m_animation;
  bool m_forceVisible = false;
  bool m_restartingAnim = false;
  OverlayLayerManager *m_layerManager = nullptr;
};

#endif
