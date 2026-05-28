#ifndef KARTEND_UTILS_APP_COLLECTION_GRIDLAYOUTPREFERENCES_PERSISTENCE_H
#define KARTEND_UTILS_APP_COLLECTION_GRIDLAYOUTPREFERENCES_PERSISTENCE_H

#include <QSettings>

#include "gridlayoutpreferences.h"

namespace GridLayoutPreferencesPersistence {

void load(QSettings &settings, GridLayoutPreferences &grid);
void save(QSettings &settings, const GridLayoutPreferences &grid);

} // namespace GridLayoutPreferencesPersistence

#endif // KARTEND_UTILS_APP_COLLECTION_GRIDLAYOUTPREFERENCES_PERSISTENCE_H
