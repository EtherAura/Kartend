#ifndef COLLECTIONREMOVER_H
#define COLLECTIONREMOVER_H

#include <QList>
#include <QObject>
#include <QString>

class QWidget;
struct CollectionConfig;
struct SettingsModel;

/// Narrow surface CollectionRemover uses to call back into its
/// SettingsDialog host. Replaces the prior `friend class CollectionRemover`
/// declaration on SettingsDialog — only this interface is reachable from
/// the remover, so changes to the dialog's private internals can't
/// silently leak into the removal pipeline. Method names are chosen to
/// avoid colliding with SettingsDialog's existing field names so the
/// dialog can implement this interface without an internal rename pass.
class CollectionRemoverHost {
public:
  virtual ~CollectionRemoverHost() = default;

  // ── Selection ─────────────────────────────────────────────────────────
  /// Collection index for the currently-selected tree item, or -1 when no
  /// valid item is selected.
  [[nodiscard]] virtual int selectedCollectionIndex() const = 0;
  /// True when a tree item is selected AND maps to a valid collection index.
  [[nodiscard]] virtual bool hasSelection() const = 0;
  /// Mark the given index as the active collection in the tree widget. Pass
  /// -1 to clear. Updates the dialog's currentTreeItem and
  /// currentCollectionIndex as side effects.
  virtual void selectCollection(int index) = 0;
  /// Clear both currentTreeItem and currentCollectionIndex without touching
  /// the tree widget's selection — used after `clearCollectionUI()` when the
  /// last collection has just been removed.
  virtual void clearSelection() = 0;

  // ── Expansion ─────────────────────────────────────────────────────────
  /// Indices whose tree items report `isExpanded() == true`.
  [[nodiscard]] virtual QList<int> expandedCollectionIndices() const = 0;
  /// Re-expand the tree item at `index` if one exists.
  virtual void expandCollectionAtIndex(int index) = 0;

  // ── UI helpers (delegate to dialog's existing private members) ───────
  virtual void expandPathToCollection(int index) = 0;
  virtual void loadCollectionToUI(int index) = 0;
  virtual void propagateCollectionNameChange(const QString &oldName, const QString &newName) = 0;
  virtual void updateCollectionTreeWidget() = 0;
  virtual void clearCollectionUI() = 0;
  virtual void ensureRootCollectionExists() = 0;
  virtual void updateSaveButtonStyle() = 0;
  virtual void updateDeleteButtonState() = 0;

  /// QWidget parent for QMessageBox prompts (typically the dialog itself).
  [[nodiscard]] virtual QWidget *dialogWidget() = 0;

  /// Re-emits SettingsDialog::collectionSaved on the host's behalf.
  virtual void emitCollectionSaved(const QList<CollectionConfig> &collections) = 0;
};

/// Owns the SettingsDialog's collection-removal pipeline: the user-confirm
/// prompt, dropping the selected collection together with its whole
/// subtree, remapping every surviving collection's parent link onto the
/// shrunk index space (via CollectionUtils::applyCollectionRemoval), the
/// post-removal tree refresh, and re-selecting a sensible follow-on
/// collection.
///
/// Decoupling: the remover operates on a `SettingsModel` (data) plus a
/// `CollectionRemoverHost` (tree state + UI helpers). No friend access into
/// SettingsDialog — the dialog implements the host interface and hands
/// itself + the model in at construction.
class CollectionRemover : public QObject {
  Q_OBJECT

public:
  CollectionRemover(SettingsModel *model, CollectionRemoverHost *host, QObject *parent = nullptr);

  /// Run the full removal flow for whatever the host's selection points at.
  /// No-op when the precondition check fails. Pops the user-confirm dialog,
  /// removes the selection and its descendants, remaps the surviving
  /// collections' parent links, scrubs link references to the vanishing
  /// names, refreshes the tree widget, and selects a sensible follow-on
  /// collection. Re-emits SettingsDialog::collectionSaved through the host.
  void run();

private:
  /// Re-expand the tree rows that were expanded before the removal,
  /// translating each saved original index through @p oldToNew (entries
  /// that map to a removed row are skipped).
  void restoreExpandedStates(const QList<int> &expandedBefore, const QList<int> &oldToNew);
  /// Select a sensible follow-on collection after the removal. @p parentIdx
  /// is the deleted collection's parent already remapped to a post-removal
  /// index (-1 when it was a root); the first collection is used as the
  /// fallback.
  void selectTargetAfter(int parentIdx);

  SettingsModel *m_model;
  CollectionRemoverHost *m_host;
};

#endif // COLLECTIONREMOVER_H
