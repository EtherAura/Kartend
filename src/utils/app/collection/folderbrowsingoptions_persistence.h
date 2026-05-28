#ifndef KARTEND_UTILS_APP_COLLECTION_FOLDERBROWSINGOPTIONS_PERSISTENCE_H
#define KARTEND_UTILS_APP_COLLECTION_FOLDERBROWSINGOPTIONS_PERSISTENCE_H

#include <QSettings>

#include "folderbrowsingoptions.h"

namespace FolderBrowsingOptionsPersistence {

void load(QSettings &settings, FolderBrowsingOptions &opts);
void save(QSettings &settings, const FolderBrowsingOptions &opts);

} // namespace FolderBrowsingOptionsPersistence

#endif // KARTEND_UTILS_APP_COLLECTION_FOLDERBROWSINGOPTIONS_PERSISTENCE_H
