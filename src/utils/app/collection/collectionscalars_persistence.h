#ifndef KARTEND_UTILS_APP_COLLECTION_COLLECTIONSCALARS_PERSISTENCE_H
#define KARTEND_UTILS_APP_COLLECTION_COLLECTIONSCALARS_PERSISTENCE_H

// Kartend-1mud (stretch goal): persistence for the per-collection scalar
// fields that don't belong to a leaf sub-cluster (name, type, directories,
// extensions, customArtworkTypes, the misc booleans, customFontFamily,
// additionalParentNames, preservedKeys). The path-sanitizing closure is
// passed in by the caller so this TU stays free of ErrorUtils logging
// context.

#include <functional>
#include <QSettings>
#include <QString>

#include "collectionconfig.h"

namespace CollectionScalarsPersistence {

using PathSanitizer = std::function<QString(const QString &value, const QString &fieldId)>;

/// Reads the per-collection scalar fields, including the preservedKeys
/// pass-through bag and the normalizing extension/custom-artwork-types
/// post-processing. Sets `needsRewrite=true` when either normalization
/// actually changed the value, so the duplicates don't resurface on next
/// load. `videoDirectory` / `manualDirectory` are intentionally cleared:
/// they were collapsed into the single-root artwork layout and any stale
/// value gets scrubbed by save().
void load(QSettings &settings, CollectionConfig &config, bool &needsRewrite,
          const PathSanitizer &sanitize);

/// Writes the per-collection scalar fields. Caller is inside beginGroup()
/// for the collection. preservedKeys is replayed first so known-field
/// setValue() calls below overwrite stale duplicates while future-version
/// keys this build doesn't recognise survive the round-trip.
void save(QSettings &settings, const CollectionConfig &config, const QString &sectionName,
          const PathSanitizer &sanitize);

} // namespace CollectionScalarsPersistence

#endif // KARTEND_UTILS_APP_COLLECTION_COLLECTIONSCALARS_PERSISTENCE_H
