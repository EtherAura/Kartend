#ifndef SETTINGSTREEHOST_H
#define SETTINGSTREEHOST_H

#include <QString>

/// Kartend-mnymg: narrow callback surface the TreeManager controller uses to
/// drive its host SettingsDialog from the tree gesture handlers (selection
/// switch, rename, context-menu actions, drag-drop reparent) it now owns.
///
/// Mirrors the CollectionRemoverHost pattern: only these operations are
/// reachable from the controller, so the dialog's private internals can't leak
/// into the tree pipeline. Every method here is already implemented on
/// SettingsDialog (the dialog just adds this as a base); the controller borrows
/// the dialog's selection-state members by pointer (see
/// TreeManager::setSelectionState) rather than duplicating them, so the
/// dialog's ~70 existing currentCollectionIndex call sites stay untouched.
class SettingsTreeHost {
public:
  virtual ~SettingsTreeHost() = default;

  /// Prompt about unsaved edits before leaving the current collection. Returns
  /// true when it's safe to proceed (saved or discarded), false to abort.
  [[nodiscard]] virtual bool resolveUnsavedChanges(const QString &actionDescription,
                                                   bool refreshTreeAfterSave) = 0;
  /// Populate the form widgets from the collection at @p index.
  virtual void loadCollectionToUI(int index) = 0;
  /// Repaint the Save button's dirty/clean styling.
  virtual void updateSaveButtonStyle() = 0;
  /// Enable/disable the Delete button for the current selection.
  virtual void updateDeleteButtonState() = 0;
  /// Refresh the per-collection context header label.
  virtual void updateContextHeader() = 0;
  /// True when the form has edits not yet written to the working collection.
  [[nodiscard]] virtual bool hasUnsavedChanges() const = 0;
  /// Context-menu actions — collection mutation owned by the dialog.
  virtual void duplicateCollection() = 0;
  virtual void removeCollection() = 0;
  virtual void copySettingsFromOtherCollection() = 0;
};

#endif // SETTINGSTREEHOST_H
