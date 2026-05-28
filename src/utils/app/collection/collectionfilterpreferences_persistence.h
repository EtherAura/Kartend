#ifndef KARTEND_UTILS_APP_COLLECTION_COLLECTIONFILTERPREFERENCES_PERSISTENCE_H
#define KARTEND_UTILS_APP_COLLECTION_COLLECTIONFILTERPREFERENCES_PERSISTENCE_H

#include <QSettings>

#include "collectionfilterpreferences.h"

namespace CollectionFilterPreferencesPersistence {

void load(QSettings &settings, CollectionFilterPreferences &filter);
void save(QSettings &settings, const CollectionFilterPreferences &filter);

} // namespace CollectionFilterPreferencesPersistence

#endif // KARTEND_UTILS_APP_COLLECTION_COLLECTIONFILTERPREFERENCES_PERSISTENCE_H
