// Unit coverage for ArrowNavigationHandler — the arrow-key grid-traversal
// orchestrator. The selection MATH itself (KeyboardManager::calculateNewSelection,
// SelectionManager::isNewRow) is already covered by test_keyboardmanager /
// test_selectionhelpers; this pins down the HANDLER's wiring: that it sources
// currentSelection + gridWidth from its injected callbacks, totalItems from the
// scroll-data source, the wrap flag from GeneralSettings, and emits
// requestFullSelectionUpdate with the computed index (Kartend audit 0ta4e).
#include <QHash>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>

#include "applicationcontext.h"
#include "arrownavigationhandler.h"
#include "collection/generalsettings.h"
#include "iscrolldatasource.h"
#include "keyboardmanager.h"

namespace {

/// Minimal IScrollDataSource: only getTotalItems() is consulted on the
/// handleArrowKeyNavigation path; the rest return inert values so the
/// interface compiles.
class StubScrollData : public IScrollDataSource {
public:
  int total = 0;
  [[nodiscard]] int getTotalItems() const override { return total; }
  [[nodiscard]] const QStringList &getFilePaths() const override { return m_paths; }
  [[nodiscard]] const QHash<QString, QString> &getFileNames() const override { return m_names; }
  [[nodiscard]] QString filePathForVisualIndex(int) const override { return {}; }
  [[nodiscard]] const QHash<int, ItemWidget *> &getActiveWidgets() const override {
    return m_widgets;
  }
  [[nodiscard]] int indexForWidget(ItemWidget *) const override { return -1; }
  [[nodiscard]] int getSubcollectionCount() const override { return 0; }
  [[nodiscard]] QString getSubcollectionName(int) const override { return {}; }
  [[nodiscard]] int getVirtualFolderCount() const override { return 0; }
  [[nodiscard]] QString virtualFolderPathForVisualIndex(int) const override { return {}; }
  [[nodiscard]] int subcollectionIndexFromActual(int) const override { return -1; }
  void updateContextForSubcollection(int) override {}
  void applySubcollectionFilter(int) override {}

private:
  QStringList m_paths;
  QHash<QString, QString> m_names;
  QHash<int, ItemWidget *> m_widgets;
};

/// Handler wired to the stub scroll-data + injected callbacks. A -1 collection
/// index is the "interactive view" sentinel (validationhelpers), so no
/// CollectionConfig list is needed; the other ctx managers stay null and the
/// handler's null-guards skip them.
struct Harness {
  StubScrollData scroll;
  ApplicationContext ctx;
  GeneralSettings settings;
  int viewIndex = -1;
  ArrowNavigationHandler handler;
  int offscreenQueriedSelection = -1;
  int offscreenQueriedGridWidth = -1;

  Harness(int totalItems, int selection, int gridWidth, bool wrap) {
    scroll.total = totalItems;
    ctx.managers.scrollData = &scroll;
    settings.input.wrapNavigation = wrap;

    ArrowNavigationHandlerSetup setup;
    setup.ctx = &ctx;
    setup.currentCollectionIndex = &viewIndex;
    setup.generalSettings = &settings;
    handler.setupReferences(setup);

    handler.setGetCurrentSelection([selection]() { return selection; });
    handler.setGetCurrentGridWidth([gridWidth]() { return gridWidth; });
    handler.setIsItemOffscreen([this](int sel, int gw) {
      offscreenQueriedSelection = sel;
      offscreenQueriedGridWidth = gw;
      return false;
    });
  }
};

/// Mirror of the math the handler delegates to, so assertions track the tested
/// algorithm rather than hard-coding a grid layout the math might revise.
int expectedSelection(int total, int sel, int dir, bool wrap, bool vertical, int gridWidth) {
  bool didWrap = false;
  return KeyboardManager::calculateNewSelection(total, sel, dir, wrap, vertical, gridWidth, didWrap,
                                                /*horizontalLayout=*/false);
}

} // namespace

class TestArrowNavigationHandler : public QObject {
  Q_OBJECT
private slots:
  void verticalMoveEmitsRowStrideSelection();
  void horizontalMoveEmitsAdjacentSelection();
  void offscreenCallbackQueriedWithCurrentSelection();
  void noScrollDataSourceIsNoOp();
};

void TestArrowNavigationHandler::verticalMoveEmitsRowStrideSelection() {
  Harness h(/*total=*/20, /*selection=*/5, /*gridWidth=*/4, /*wrap=*/false);
  QSignalSpy spy(&h.handler, &ArrowNavigationHandler::requestFullSelectionUpdate);

  h.handler.handleArrowKeyNavigation(/*direction=*/1, /*vertical=*/true);

  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.at(0).at(0).toInt(), expectedSelection(20, 5, 1, false, true, 4)); // a row down
  QVERIFY(spy.at(0).at(0).toInt() != 5);                                          // actually moved
}

void TestArrowNavigationHandler::horizontalMoveEmitsAdjacentSelection() {
  Harness h(20, 5, 4, false);
  QSignalSpy spy(&h.handler, &ArrowNavigationHandler::requestFullSelectionUpdate);

  h.handler.handleArrowKeyNavigation(1, /*vertical=*/false);

  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.at(0).at(0).toInt(), expectedSelection(20, 5, 1, false, false, 4));
}

void TestArrowNavigationHandler::offscreenCallbackQueriedWithCurrentSelection() {
  Harness h(20, 7, 4, false);
  h.handler.handleArrowKeyNavigation(1, true);
  // The offscreen probe runs BEFORE the move, at the current index/grid width.
  QCOMPARE(h.offscreenQueriedSelection, 7);
  QCOMPARE(h.offscreenQueriedGridWidth, 4);
}

void TestArrowNavigationHandler::noScrollDataSourceIsNoOp() {
  // No scroll-data source on the context → handleArrowKeyNavigation bails at
  // its first guard and emits nothing.
  ApplicationContext ctx;
  GeneralSettings settings;
  int viewIndex = -1;
  ArrowNavigationHandler handler;
  ArrowNavigationHandlerSetup setup;
  setup.ctx = &ctx;
  setup.currentCollectionIndex = &viewIndex;
  setup.generalSettings = &settings;
  handler.setupReferences(setup);
  handler.setGetCurrentSelection([]() { return 3; });
  handler.setGetCurrentGridWidth([]() { return 4; });

  QSignalSpy spy(&handler, &ArrowNavigationHandler::requestFullSelectionUpdate);
  handler.handleArrowKeyNavigation(1, true);
  QCOMPARE(spy.count(), 0);
}

QTEST_GUILESS_MAIN(TestArrowNavigationHandler)
#include "test_arrownavigationhandler.moc"
