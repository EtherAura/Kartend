#ifndef KARTEND_UTILS_APP_COLLECTION_LISTVIEWOPTIONS_PERSISTENCE_H
#define KARTEND_UTILS_APP_COLLECTION_LISTVIEWOPTIONS_PERSISTENCE_H

#include <QSettings>

#include "listviewoptions.h"

namespace ListViewOptionsPersistence {

void load(QSettings &settings, ListViewOptions &opts);
void save(QSettings &settings, const ListViewOptions &opts);

} // namespace ListViewOptionsPersistence

#endif // KARTEND_UTILS_APP_COLLECTION_LISTVIEWOPTIONS_PERSISTENCE_H
