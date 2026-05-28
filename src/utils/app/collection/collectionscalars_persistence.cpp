#include "collectionscalars_persistence.h"

#include <QLoggingCategory>
#include <QStringList>

#include "extensionutils.h"
#include "helpers.h"
#include "settingskeys.h"

namespace keys = kartend::settings::keys;

// Mirrors the "kartend.settingsmanager" category in
// modules/data/settings/settingsmanager.h. utils-layer can't include that
// header (layering); re-declaring with the same string routes through the
// same Qt logging filter.
namespace {
Q_LOGGING_CATEGORY(lcSettingsManager, "kartend.settingsmanager")
} // namespace

namespace CollectionScalarsPersistence {

void load(QSettings &settings, CollectionConfig &config, bool &needsRewrite,
          const PathSanitizer &sanitize) {
  static const QStringList kPreservationBlocklist = {
      keys::kVideoDirectory,
      keys::kManualDirectory,
  };
  for (const QString &k : settings.childKeys()) {
    if (k.contains(QLatin1Char('/'))) continue;
    if (kPreservationBlocklist.contains(k)) continue;
    config.preservedKeys.insert(k, settings.value(k));
  }

  config.name = settings.value(keys::kName).toString();
  config.type = settings.value(keys::kType).toString().trimmed();

  const int additionalParentsCount = settings.beginReadArray(keys::kAdditionalParents);
  config.additionalParentNames.reserve(additionalParentsCount);
  for (int i = 0; i < additionalParentsCount; ++i) {
    settings.setArrayIndex(i);
    const QString parentName = settings.value(keys::kName).toString();
    if (!parentName.isEmpty()) {
      config.additionalParentNames.append(parentName);
    }
  }
  settings.endArray();

  config.mediaDirectory =
      sanitize(settings.value(keys::kMediaDirectory).toString(), QStringLiteral("mediaDirectory"));
  config.artworkDirectory = sanitize(settings.value(keys::kArtworkDirectory).toString(),
                                     QStringLiteral("artworkDirectory"));
  // videoDirectory / manualDirectory were collapsed into the single-root
  // artwork layout. Don't read them — save() removes stale values from the
  // INI so they decay on the next write.
  config.videoDirectory.clear();
  config.manualDirectory.clear();
  config.placeholderArtwork = sanitize(settings.value(keys::kPlaceholderArtwork).toString(),
                                       QStringLiteral("placeholderArtwork"));
  config.expandMode = settings.value(keys::kExpandMode, false).toBool();
  config.watchFilesystem = settings.value(keys::kWatchFilesystem, false).toBool();
  config.collectionIcon = settings.value(keys::kCollectionIcon).toString();

  QStringList rawList = settings.value(keys::kExtensions).toString().split(',', Qt::SkipEmptyParts);
  for (QString &extension : rawList) {
    extension = extension.trimmed();
  }
  QStringList normalized = ExtensionUtils::normalizeStoredExtensions(rawList);
  if (normalized != rawList) {
    needsRewrite = true;
  }
  config.extensions = normalized;

  // Dedup custom artwork type ids — sloppy hand-edits can repeat tokens; the
  // type id doubles as the artwork_type DB key, so a duplicate would wedge
  // the sidebar gallery's lookup.
  QStringList customArtTypes =
      settings.value(keys::kCustomArtworkTypes).toString().split(',', Qt::SkipEmptyParts);
  QStringList cleanedCustomTypes;
  cleanedCustomTypes.reserve(customArtTypes.size());
  for (QString &type : customArtTypes) {
    type = type.trimmed();
    if (!type.isEmpty() && !cleanedCustomTypes.contains(type)) {
      cleanedCustomTypes.append(type);
    }
  }
  if (cleanedCustomTypes != customArtTypes) {
    needsRewrite = true;
  }
  config.customArtworkTypes = cleanedCustomTypes;

  config.showAllSubcollectionItems =
      settings.value(keys::kShowAllSubcollectionItems, false).toBool();
  const QString rawAlignment = settings.value(keys::kHorizontalAlignment, "center").toString();
  bool alignmentFallback = false;
  config.horizontalAlignment = CollectionUtils::stringToAlignment(rawAlignment, &alignmentFallback);
  if (alignmentFallback) {
    qCWarning(lcSettingsManager).nospace()
        << "Collection '" << config.name << "': unknown " << keys::kHorizontalAlignment
        << " value '" << rawAlignment << "' — falling back to 'center'. Fix the INI to silence.";
  }
  const QString rawViewType = settings.value(keys::kViewType, "grid").toString();
  bool viewTypeFallback = false;
  config.viewType = CollectionUtils::stringToViewType(rawViewType, &viewTypeFallback);
  if (viewTypeFallback) {
    qCWarning(lcSettingsManager).nospace()
        << "Collection '" << config.name << "': unknown " << keys::kViewType << " value '"
        << rawViewType << "' — falling back to 'grid'. Fix the INI to silence.";
  }
  config.hideMissingArtwork = settings.value(keys::kHideMissingArtwork, false).toBool();
  config.hideTitles = settings.value(keys::kHideTitles, false).toBool();
  config.hideSubcollectionTitles = settings.value(keys::kHideSubcollectionTitles, false).toBool();
  config.customFontFamily = settings.value(keys::kCustomFontFamily).toString();
}

void save(QSettings &settings, const CollectionConfig &config, const QString &sectionName,
          const PathSanitizer &sanitize) {
  // Replay preserved keys first so known-field setValue() calls below win
  // over the stashed copy. Future-version keys this build doesn't recognise
  // survive a load-modify-save through this bag.
  for (auto it = config.preservedKeys.constBegin(); it != config.preservedKeys.constEnd(); ++it) {
    settings.setValue(it.key(), it.value());
  }
  settings.setValue(keys::kName, config.name);
  settings.setValue(keys::kType, config.type);
  settings.beginWriteArray(keys::kAdditionalParents, config.additionalParentNames.size());
  for (int i = 0; i < config.additionalParentNames.size(); ++i) {
    settings.setArrayIndex(i);
    settings.setValue(keys::kName, config.additionalParentNames[i]);
  }
  settings.endArray();
  settings.setValue(keys::kMediaDirectory,
                    sanitize(config.mediaDirectory, QStringLiteral("mediaDirectory")));
  settings.setValue(keys::kArtworkDirectory,
                    sanitize(config.artworkDirectory, QStringLiteral("artworkDirectory")));
  // Legacy keys — scrub from the INI so configvalidation warnings about
  // unplugged drives / path typos don't linger forever; consumers fall back
  // to {artwork}/video/ and {artwork}/manual/.
  settings.remove(keys::kVideoDirectory);
  settings.remove(keys::kManualDirectory);
  settings.setValue(keys::kPlaceholderArtwork,
                    sanitize(config.placeholderArtwork, QStringLiteral("placeholderArtwork")));
  settings.setValue(keys::kExpandMode, config.expandMode);
  settings.setValue(keys::kWatchFilesystem, config.watchFilesystem);
  settings.setValue(keys::kCollectionIcon, config.collectionIcon);
  settings.setValue(keys::kExtensions, config.extensions.join(", "));
  settings.setValue(keys::kCustomArtworkTypes, config.customArtworkTypes.join(", "));
  settings.setValue(keys::kShowAllSubcollectionItems, config.showAllSubcollectionItems);
  settings.setValue(keys::kHorizontalAlignment,
                    CollectionUtils::alignmentToString(config.horizontalAlignment));
  settings.setValue(keys::kViewType, CollectionUtils::viewTypeToString(config.viewType));
  settings.setValue(keys::kHideMissingArtwork, config.hideMissingArtwork);
  settings.setValue(keys::kHideTitles, config.hideTitles);
  settings.setValue(keys::kHideSubcollectionTitles, config.hideSubcollectionTitles);
  settings.setValue(keys::kCustomFontFamily, config.customFontFamily);
  Q_UNUSED(sectionName);
}

} // namespace CollectionScalarsPersistence
