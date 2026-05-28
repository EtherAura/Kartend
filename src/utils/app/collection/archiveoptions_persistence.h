#ifndef KARTEND_UTILS_APP_COLLECTION_ARCHIVEOPTIONS_PERSISTENCE_H
#define KARTEND_UTILS_APP_COLLECTION_ARCHIVEOPTIONS_PERSISTENCE_H

#include <QSettings>

#include "archiveoptions.h"

namespace ArchiveOptionsPersistence {

void load(QSettings &settings, ArchiveOptions &opts);
void save(QSettings &settings, const ArchiveOptions &opts);

} // namespace ArchiveOptionsPersistence

#endif // KARTEND_UTILS_APP_COLLECTION_ARCHIVEOPTIONS_PERSISTENCE_H
