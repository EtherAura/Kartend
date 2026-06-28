#ifndef KARTEND_TESTS_STUBSELECTIONMANAGER_H
#define KARTEND_TESTS_STUBSELECTIONMANAGER_H

#include "iselectionmanager.h"

#include <QList>
#include <QString>

namespace KartendTest {

/// Minimal ISelectionManager double for the input-handler tests.
///
/// Holds a settable current selection and records the mutating calls the
/// handlers make so tests can assert on them: setSelectedIndex (last value +
/// call count), notifySelectionChanged (flag + count), selectItemByHover (last
/// index + count), and isRestoringSelection (settable). Every other interface
/// method is inert.
class StubSelectionManager : public ISelectionManager {
public:
  // Configurable inputs.
  int index = -1;         ///< returned by currentSelectedIndex()
  bool restoring = false; ///< returned by isRestoringSelection()

  // Recorded outputs.
  int lastSetIndex = -1;
  int setSelectedIndexCalls = 0;
  bool notified = false;
  int notifyCalls = 0;
  int lastHoverIndex = -1;
  int selectItemByHoverCalls = 0;

  [[nodiscard]] int currentSelectedIndex() const override { return index; }
  void setSelectedIndex(int idx) override {
    lastSetIndex = idx;
    ++setSelectedIndexCalls;
    index = idx;
  }
  void notifySelectionChanged() override {
    notified = true;
    ++notifyCalls;
  }
  void selectItemByHover(int idx) override {
    lastHoverIndex = idx;
    ++selectItemByHoverCalls;
  }
  [[nodiscard]] bool isRestoringSelection() const override { return restoring; }

  // --- inert stubs below ---
  void cancelPendingSelectionRestore() override {}
  [[nodiscard]] int targetRestoreIndex() const override { return -1; }
  void setRestoringSelection(bool) override {}
  void setTargetRestoreIndex(int) override {}
  bool checkAndFinalizeRestore(int) override { return false; }
  void updateFilePathForSelection(int, const QList<int> &) override {}
  [[nodiscard]] QList<int> getSubcollections(int) const override { return {}; }
  void setLastSelectedRow(int) override {}
};

} // namespace KartendTest

#endif // KARTEND_TESTS_STUBSELECTIONMANAGER_H
