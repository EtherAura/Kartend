#include "collectiontreesettings_persistence.h"

#include <QLoggingCategory>

#include "settingskeys.h"

namespace keys = kartend::settings::keys;

// Mirrors the "kartend.settingsmanager" category declared in
// modules/data/settings/settingsmanager.h — utils-layer can't include that
// header (layering); re-declaring with the same string routes through the
// same Qt logging filter (same reasoning as sidebarappearance_persistence).
namespace {
Q_LOGGING_CATEGORY(lcSettingsManager, "kartend.settingsmanager")

QString positionToString(DetailsPanePosition position) {
  return position == DetailsPanePosition::Right ? QStringLiteral("right") : QStringLiteral("left");
}
} // namespace

namespace CollectionTreeSettingsPersistence {

void load(QSettings &settings, CollectionTreeSettings &tree, const QString &collectionName) {
  tree.treeVisible = settings.value(keys::kCollectionTreeVisible, true).toBool();
  const QString rawPosition =
      settings.value(keys::kCollectionTreePosition, QStringLiteral("left")).toString();
  if (rawPosition == QLatin1String("right")) {
    tree.treePosition = DetailsPanePosition::Right;
  } else {
    tree.treePosition = DetailsPanePosition::Left;
    if (rawPosition != QLatin1String("left")) {
      qCWarning(lcSettingsManager).nospace()
          << "Collection '" << collectionName << "': unknown " << keys::kCollectionTreePosition
          << " value '" << rawPosition
          << "' — the tree panel docks left or right only, falling back to 'left'. Fix the INI "
             "to silence.";
    }
  }
}

void save(QSettings &settings, const CollectionTreeSettings &tree) {
  settings.setValue(keys::kCollectionTreeVisible, tree.treeVisible);
  settings.setValue(keys::kCollectionTreePosition, positionToString(tree.treePosition));
}

} // namespace CollectionTreeSettingsPersistence
