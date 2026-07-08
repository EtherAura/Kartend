#ifndef IVIEWPORTPOSITIONER_H
#define IVIEWPORTPOSITIONER_H

/**
 * @brief Positioning / coordinate-mapping role of the viewport layer
 * (Kartend-dl0uz.2).
 *
 * The slice scroll gestures use to keep the selection on screen: bringing an
 * index into view, the immediate positioning applied after a wrap teleport,
 * and the logical → widget scroll-Y mapping for scaled viewports.
 * IViewportManager unions this role with IViewportScrollState; the
 * centering / key-repeat surface stays on IViewportManager itself.
 *
 * Plain abstract class, not a QObject — ViewportManager derives QObject
 * directly. Reached via ctx->viewportPositioner().
 */
class IViewportPositioner {
public:
  virtual ~IViewportPositioner() = default;

  virtual void ensureItemVisible(int index, bool allowHorizontalScroll) = 0;
  virtual void applyImmediateViewportPositioningForSelection(int targetIndex) = 0;

  [[nodiscard]] virtual double getScrollScale() const = 0;
  [[nodiscard]] virtual int toWidgetScrollY(int logicalScrollY) const = 0;
};

#endif // IVIEWPORTPOSITIONER_H
