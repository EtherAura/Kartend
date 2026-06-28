#ifndef KARTEND_TESTS_FAKESCROLLMANAGER_H
#define KARTEND_TESTS_FAKESCROLLMANAGER_H

#include "gridlayoutcalculator.h"
#include "iscrollmanager.h"
#include "itemwidget.h"

#include <QHash>
#include <QString>
#include <QStringList>

namespace KartendTest {

/// Shared IScrollManager fake for input-handler tests.
///
/// Promoted from the former file-local copy in test_mousemanager.cpp so the
/// wheel / event-manager / hover handler tests can drive the same ~63-method
/// IScrollManager surface without each re-typing it. Only the handful of
/// methods the handlers actually read are configurable members; the defaults
/// reproduce the original inert behavior (indexForWidget -> -1 unless found in
/// the activeWidgets map, getFilteredIndex -> identity, the counts -> 0,
/// sidebarShrinkingActive -> false, isArtworkPreviewVisible -> false), so the
/// existing MouseManager test keeps passing unchanged.
class FakeScrollManager : public IScrollManager {
public:
  int totalItems = 0;
  QHash<int, ItemWidget *> activeWidgets;
  GridMetrics metrics;
  int subcollectionCount = 0;
  int virtualFolderCount = 0;
  /// When >= 0, getFilteredIndex(visualIndex) returns this fixed value;
  /// otherwise it is the identity (the original fake's behavior).
  int filteredIndexOverride = -1;

  [[nodiscard]] int getTotalItems() const override { return totalItems; }
  [[nodiscard]] const QHash<int, ItemWidget *> &getActiveWidgets() const override {
    return activeWidgets;
  }
  [[nodiscard]] const GridMetrics &getMetrics() const override { return metrics; }
  [[nodiscard]] int getSubcollectionCount() const override { return subcollectionCount; }
  [[nodiscard]] int getVirtualFolderCount() const override { return virtualFolderCount; }
  [[nodiscard]] int getFilteredIndex(int visualIndex) const override {
    return filteredIndexOverride >= 0 ? filteredIndexOverride : visualIndex;
  }
  /// Reverse lookup over the active-widget map (matches the real manager's
  /// O(1) reverse index). Defaults to -1 when the widget isn't present.
  [[nodiscard]] int indexForWidget(ItemWidget *widget) const override {
    for (auto it = activeWidgets.constBegin(); it != activeWidgets.constEnd(); ++it) {
      if (it.value() == widget) {
        return it.key();
      }
    }
    return -1;
  }

  // --- inert stubs below ---
  void setupVirtualScrolling(int, const CollectionContext &) override {}
  void updateMediaItemCount(int) override {}
  void receiveItemsRange(int, const QStringList &, const QHash<QString, QString> &,
                         const QHash<QString, QString> &) override {}
  void cleanup() override {}
  void updateGridWidth(int) override {}
  void updateHorizontalGridHeight(int) override {}
  void setSidebarShrinkingActive(bool) override {}
  [[nodiscard]] bool sidebarShrinkingActive() const override { return false; }
  void updateViewType(ViewType) override {}
  void updateVirtualView() override {}
  [[nodiscard]] int getEffectiveHorizontalSpacing() const override { return 0; }
  [[nodiscard]] QString getSubcollectionName(int) const override { return {}; }
  void updateSelectionForIndex(int) override {}
  void refreshSelectionOverlayState() override {}
  void setForceSelectionOverlayVisible(bool) override {}
  void applyFilter(const QString &) override {}
  void cleanupActiveWidgets() override {}
  void clearFilter() override {}
  void showSearchLoadingOverlay() override {}
  void hideSearchLoadingOverlay() override {}
  void savePreSearchState() override {}
  void restorePreSearchState() override {}
  [[nodiscard]] bool hasPreSearchState() const override { return false; }
  void recreateLayout() override {}
  void setPendingSelectionRestoreByPath(const QString &) override {}
  [[nodiscard]] bool hasPendingSelectionRestoreByPath() const override { return false; }
  [[nodiscard]] bool isArtworkPreviewVisible() const override { return false; }
  bool hideArtworkPreview() override { return false; }
  bool showMediaPreview(const QString &, const QString &, const QString &) override {
    return false;
  }
  void centerHorizontalScrollbar(int, const QList<CollectionConfig> &) override {}
  void recenterVirtualContainer() override {}
  void handleLayoutChange() override {}
  [[nodiscard]] int getCurrentGridWidth() const override { return 0; }
  void updateContextForSubcollection(int) override {}
  void applySubcollectionFilter(int) override {}
  void recalculateContainerMetrics() override {}
  void forceVirtualViewUpdate() override {}
  void preCalculateLayout() override {}
  void injectCachedItems(int, const QStringList &, const QHash<QString, QString> &,
                         const QHash<QString, QString> &) override {}
  [[nodiscard]] bool getCurrentViewportForCache(int &, int &, QStringList &,
                                                QHash<QString, QString> &,
                                                QHash<QString, QString> &) const override {
    return false;
  }
  [[nodiscard]] const QStringList &getFilePaths() const override { return m_filePaths; }
  [[nodiscard]] const QHash<QString, QString> &getFileNames() const override { return m_fileNames; }
  [[nodiscard]] QString filePathForVisualIndex(int) const override { return {}; }
  [[nodiscard]] QString virtualFolderPathForVisualIndex(int) const override { return {}; }
  void primeLayoutFor(const CollectionConfig &) override {}
  void setInitialScrollIndex(int) override {}
  [[nodiscard]] int subcollectionIndexFromActual(int) const override { return -1; }

private:
  QStringList m_filePaths;
  QHash<QString, QString> m_fileNames;
};

} // namespace KartendTest

#endif // KARTEND_TESTS_FAKESCROLLMANAGER_H
