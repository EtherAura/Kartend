#ifndef ISELECTIONOVERLAYSCROLL_H
#define ISELECTIONOVERLAYSCROLL_H

#include <QString>

/**
 * @brief Selection-overlay role of the scroll layer (Kartend-h1l8f).
 *
 * One of the six role interfaces IScrollManager unions: positioning and
 * visibility of the selection overlay, plus the pending path-based selection
 * restore used across sort changes. Matches the SelectionDisplayManager side
 * of the implementation split.
 *
 * Plain abstract class, not a QObject — ScrollManager derives QObject once,
 * through IScrollManager. Reached via ctx->scrollOverlay().
 */
class ISelectionOverlayScroll {
public:
  virtual ~ISelectionOverlayScroll() = default;

  virtual void updateSelectionForIndex(int selectedIndex) = 0;
  virtual void refreshSelectionOverlayState() = 0;
  virtual void setForceSelectionOverlayVisible(bool force) = 0;

  /// Set file path to restore selection to after items are loaded (for sort
  /// changes).
  virtual void setPendingSelectionRestoreByPath(const QString &filePath) = 0;
  /// Check if there's a pending selection restore by file path. Used to skip
  /// index-based selection restore during sort changes.
  [[nodiscard]] virtual bool hasPendingSelectionRestoreByPath() const = 0;
};

#endif // ISELECTIONOVERLAYSCROLL_H
