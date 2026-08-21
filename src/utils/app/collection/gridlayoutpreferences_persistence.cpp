#include "gridlayoutpreferences_persistence.h"

#include "collection/enumstringhelpers.h"
#include "settingskeys.h"
#include "uiconstants/grid.h"
#include "uiconstants/item.h"

namespace keys = kartend::settings::keys;

namespace GridLayoutPreferencesPersistence {

void load(QSettings &settings, GridLayoutPreferences &grid) {
  grid.gridWidth = settings.value(keys::kGridWidth, 4).toInt();
  grid.horizontalGridHeight = settings.value(keys::kHorizontalGridHeight, 0).toInt();
  grid.gridWidthSidebarHidden = settings.value(keys::kGridWidthSidebarHidden, 0).toInt();
  grid.horizontalGridHeightSidebarHidden =
      settings.value(keys::kHorizontalGridHeightSidebarHidden, 0).toInt();
  grid.gridHeightSidebarHidden = settings.value(keys::kGridHeightSidebarHidden, 0).toInt();
  // Read as a STRING, not a bool: these keys were bools until 2026-08-19 and
  // stringToScrollbarMode accepts the legacy "true"/"false" spelling, so an
  // existing config migrates itself on first load with no schema bump.
  grid.horizontalScrollbarMode = CollectionUtils::stringToScrollbarMode(
      settings.value(keys::kHideHorizontalScrollbar).toString());
  grid.verticalScrollbarMode = CollectionUtils::stringToScrollbarMode(
      settings.value(keys::kHideVerticalScrollbar).toString());
  grid.horizontalSpacing =
      settings.value(keys::kHorizontalSpacing, UIConstants::Grid::SPACING).toInt();
  grid.verticalSpacing = settings.value(keys::kVerticalSpacing, 20).toInt();
  grid.itemWidth = settings.value(keys::kItemWidth, UIConstants::Item::DEFAULT_WIDTH).toInt();
  grid.itemHeight = settings.value(keys::kItemHeight, UIConstants::Item::DEFAULT_HEIGHT).toInt();
  grid.fontSize = settings.value(keys::kFontSize, UIConstants::Item::DEFAULT_FONT_SIZE).toInt();
  grid.cornerRadius =
      settings.value(keys::kCornerRadius, UIConstants::Item::DEFAULT_CORNER_RADIUS).toInt();
}

void save(QSettings &settings, const GridLayoutPreferences &grid) {
  settings.setValue(keys::kGridWidth, grid.gridWidth);
  settings.setValue(keys::kHorizontalGridHeight, grid.horizontalGridHeight);
  settings.setValue(keys::kGridWidthSidebarHidden, grid.gridWidthSidebarHidden);
  settings.setValue(keys::kHorizontalGridHeightSidebarHidden,
                    grid.horizontalGridHeightSidebarHidden);
  settings.setValue(keys::kGridHeightSidebarHidden, grid.gridHeightSidebarHidden);
  settings.setValue(keys::kHideHorizontalScrollbar,
                    CollectionUtils::scrollbarModeToString(grid.horizontalScrollbarMode));
  settings.setValue(keys::kHideVerticalScrollbar,
                    CollectionUtils::scrollbarModeToString(grid.verticalScrollbarMode));
  settings.setValue(keys::kHorizontalSpacing, grid.horizontalSpacing);
  settings.setValue(keys::kVerticalSpacing, grid.verticalSpacing);
  settings.setValue(keys::kItemWidth, grid.itemWidth);
  settings.setValue(keys::kItemHeight, grid.itemHeight);
  settings.setValue(keys::kFontSize, grid.fontSize);
  settings.setValue(keys::kCornerRadius, grid.cornerRadius);
}

} // namespace GridLayoutPreferencesPersistence
