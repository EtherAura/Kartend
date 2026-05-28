#ifndef KARTEND_UTILS_APP_COLLECTION_COLLECTIONBACKGROUND_PERSISTENCE_H
#define KARTEND_UTILS_APP_COLLECTION_COLLECTIONBACKGROUND_PERSISTENCE_H

#include <functional>
#include <QSettings>
#include <QString>

#include "collectionbackground.h"

namespace CollectionBackgroundPersistence {

using PathSanitizer = std::function<QString(const QString &value, const QString &fieldId)>;

void load(QSettings &settings, CollectionBackground &bg, const QString &collectionName,
          const PathSanitizer &sanitize);
void save(QSettings &settings, const CollectionBackground &bg, const PathSanitizer &sanitize);

} // namespace CollectionBackgroundPersistence

#endif // KARTEND_UTILS_APP_COLLECTION_COLLECTIONBACKGROUND_PERSISTENCE_H
