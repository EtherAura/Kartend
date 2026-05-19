#ifndef ISELECTIONMANAGER_H
#define ISELECTIONMANAGER_H

#include <QList>
#include <QString>

/**
 * @brief Sibling-facing interface to grid selection state.
 *
 * Like INavigationManager, this is a deliberate role interface
 * (interface-segregation): the slice sibling managers reach for through
 * ApplicationContext, not SelectionManager's full surface (its many
 * signal-driven slots, click-processing helpers and static row math stay
 * on the concrete class, which InteractionManager owns directly as a
 * unique_ptr and connects signals against).
 *
 * Plain abstract class, not a QObject — SelectionManager derives QObject
 * directly; see its multiple-inheritance declaration. Add a method here
 * only when a sibling genuinely needs to call it via ctx.
 */
class ISelectionManager {
public:
  virtual ~ISelectionManager() = default;

  // Core selection state
  [[nodiscard]] virtual int currentSelectedIndex() const = 0;
  virtual void setSelectedIndex(int index) = 0;
  virtual void notifySelectionChanged() = 0;

  // Selection restore operations
  virtual void cancelPendingSelectionRestore() = 0;
  [[nodiscard]] virtual bool isRestoringSelection() const = 0;
  [[nodiscard]] virtual int targetRestoreIndex() const = 0;
  virtual void setRestoringSelection(bool restoring) = 0;
  virtual void setTargetRestoreIndex(int index) = 0;
  virtual bool checkAndFinalizeRestore(int index) = 0;

  // File path resolution for selection
  virtual void updateFilePathForSelection(int index, const QList<int> &subcollections) = 0;

  // Subcollections lookup
  [[nodiscard]] virtual QList<int> getSubcollections(int parentIndex) const = 0;

  // Row tracking for click detection
  virtual void setLastSelectedRow(int row) = 0;

  // Hover selection
  virtual void selectItemByHover(int index) = 0;
};

#endif // ISELECTIONMANAGER_H
