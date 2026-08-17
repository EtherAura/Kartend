#ifndef ISELECTIONMANAGER_H
#define ISELECTIONMANAGER_H

#include "iselectioncore.h"
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
 * The core read/step slice (current index, bare setter + notify, restore
 * query, file-path resolution, subcollection lookup) lives on
 * ISelectionCore, which this interface unions (Kartend-dl0uz.2) —
 * input handlers that only move the selection take ctx->selectionCore().
 *
 * Plain abstract class, not a QObject — SelectionManager derives QObject
 * directly; see its multiple-inheritance declaration. Add a method here
 * only when a sibling genuinely needs to call it via ctx.
 */
class ISelectionManager : public ISelectionCore {
public:
  ~ISelectionManager() override = default;

  // Selection restore operations
  virtual void cancelPendingSelectionRestore() = 0;

  /// Persist @p idx as @p coll's remembered selection (settings + session
  /// store), deriving the title discriminator itself. Every user-driven
  /// selection move must both cancel pending restores AND persist its landing
  /// index — a canceled restore only protects until the next reload schedules
  /// a fresh one against the stale persisted value (Kartend-2sdjp: the
  /// cover-flow wheel snap-back; the keyboard path has always persisted via
  /// handleSuccessfulSelection).
  virtual void persistSelectionForIndex(int coll, int idx) = 0;
  [[nodiscard]] virtual int targetRestoreIndex() const = 0;
  virtual void setRestoringSelection(bool restoring) = 0;
  virtual void setTargetRestoreIndex(int index) = 0;
  virtual bool checkAndFinalizeRestore(int index) = 0;

  // Row tracking for click detection
  virtual void setLastSelectedRow(int row) = 0;

  // Hover selection
  virtual void selectItemByHover(int index) = 0;
};

#endif // ISELECTIONMANAGER_H
