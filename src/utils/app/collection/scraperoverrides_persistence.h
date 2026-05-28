#ifndef KARTEND_UTILS_APP_COLLECTION_SCRAPEROVERRIDES_PERSISTENCE_H
#define KARTEND_UTILS_APP_COLLECTION_SCRAPEROVERRIDES_PERSISTENCE_H

#include <QSettings>

#include "scraperoverrides.h"

namespace ScraperOverridesPersistence {

void load(QSettings &settings, ScraperOverrides &overrides);
void save(QSettings &settings, const ScraperOverrides &overrides);

} // namespace ScraperOverridesPersistence

#endif // KARTEND_UTILS_APP_COLLECTION_SCRAPEROVERRIDES_PERSISTENCE_H
