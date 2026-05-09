#ifndef COLLECTIONREMOVER_H
#define COLLECTIONREMOVER_H

#include <QList>
#include <QObject>

class SettingsDialog;

/// Owns the SettingsDialog's collection-removal pipeline. The flow has
/// seven discrete steps that previously lived as private methods on the
/// dialog (validate / capture-expanded / perform / update-parents /
/// rebuild-indices / restore-expanded / select-target) plus the
/// orchestrator that strings them together with the user-confirm prompt
/// and post-removal tree refresh.
///
/// Coupling: takes the host SettingsDialog and reaches back into its
/// collections / m_workingCollections / currentTreeItem / tree-index
/// maps and several private helper methods (clearCollectionUI,
/// loadCollectionToUI, ensureRootCollectionExists,
/// updateCollectionTreeWidget, propagateCollectionNameChange,
/// expandPathToCollection, updateSaveButtonStyle, updateDeleteButtonState)
/// — friend-declared so the dialog's state stays encapsulated.
class CollectionRemover : public QObject {
  Q_OBJECT

public:
  explicit CollectionRemover(SettingsDialog *host);

  /// Run the full removal flow for whatever the dialog's currentTreeItem
  /// points at. No-op when the precondition check fails (no item selected,
  /// invalid index). Pops the user-confirm dialog, removes descendants
  /// first, scrubs link references to the vanishing names, refreshes the
  /// tree widget, and selects a sensible follow-on collection. Re-emits
  /// SettingsDialog::collectionSaved through the host.
  void run();

private:
  // Pipeline steps. Returning bool / value where the orchestrator needs
  // a result; returning void where the step purely mutates host state.
  [[nodiscard]] bool validatePreconditions();
  [[nodiscard]] QList<int> captureExpandedStates();
  void performRemovalAt(int index);
  void rebuildParentIndices();
  void restoreExpandedStates(const QList<int> &expandedBefore, int removedIndex);
  void selectTargetAfter(int parentIdx, int removedIndex);

  SettingsDialog *m_host;
};

#endif // COLLECTIONREMOVER_H
