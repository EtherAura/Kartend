#ifndef ISELECTIONCORE_H
#define ISELECTIONCORE_H

#include <QList>

/**
 * @brief Core selection-state role of the selection layer (Kartend-dl0uz.2).
 *
 * The read/step surface input handlers use to move the selection: the
 * current index, the bare setter + its explicit change notification, the
 * restore-in-progress query, the file-path resolution that follows a move,
 * and the subcollection lookup the path resolution needs.
 * ISelectionManager unions this role; the restore-lifecycle mutators and
 * click/hover helpers stay on ISelectionManager itself.
 *
 * Plain abstract class, not a QObject — SelectionManager derives QObject
 * directly. Reached via ctx->selectionCore().
 */
class ISelectionCore {
public:
  virtual ~ISelectionCore() = default;

  [[nodiscard]] virtual int currentSelectedIndex() const = 0;
  virtual void setSelectedIndex(int index) = 0;
  virtual void notifySelectionChanged() = 0;

  [[nodiscard]] virtual bool isRestoringSelection() const = 0;

  virtual void updateFilePathForSelection(int index, const QList<int> &subcollections) = 0;

  [[nodiscard]] virtual QList<int> getSubcollections(int parentIndex) const = 0;
};

#endif // ISELECTIONCORE_H
