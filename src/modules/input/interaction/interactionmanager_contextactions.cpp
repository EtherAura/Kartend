// Sibling translation unit for InteractionManager.
// Context-menu *action handlers* — the slot-side of the right-click menu
// items showContextMenu() builds. These are split out of
// interactionmanager_contextmenu.cpp so that file can stay focused on the
// QMenu / action assembly. The item-metadata mutation cluster (edit dialog,
// manual path, launcher override, pin/hide/continue-later) lives on
// ItemMetadataActionController and the playlist actions on
// PlaylistMenuController (Kartend-5lmt7); callers reach both controllers
// directly via the itemMetadataActions() / playlistMenu() accessors — the
// one-line delegates that used to live here were deleted (Kartend-i5ai0).
// What remains is the launch-command preview, which stays on the facade
// because it composes LaunchManager + the owner-supplied dialog runner.

#include "interactionmanager.h"

#include "collection/collectionconfig.h"
#include "collection/launcherpreset.h"
#include "collection/typehelpers.h"
#include "collection/validationhelpers.h"
#include "idatabasemanager.h"
#include "itemmetadata.h"
#include "launchmanager.h"
#include "pathutils.h"

#include <algorithm>
#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcInteractionManager)

void InteractionManager::previewLaunchCommand(const QString &filePath, const QString &itemName) {
  if (!m_runLaunchPreviewDialog || !m_collections || !m_currentCollectionIndex) {
    return;
  }
  if (!databaseMgr()) {
    return;
  }
  // Resolve the owning collection the same way every other per-item action
  // does — the displayed collection may not own the file (e.g.
  // showAllSubcollectionItems mode).
  int owningIndex = databaseMgr()->getCollectionIndexForFile(filePath);
  if (owningIndex < 0) {
    owningIndex = *m_currentCollectionIndex;
  }
  if (!CollectionUtils::isValidIndex(owningIndex, m_collections)) {
    return;
  }
  const CollectionConfig &owning = (*m_collections)[owningIndex];

  // Pick the launcher index the same way launchItem would: per-item
  // override wins, then default-launcher, then 0. We don't pop the chooser
  // dialog here — the preview is meant to be a fast read-only surface, so
  // the user gets to see what would happen with the most likely launcher.
  // If they want a different one, they can pick via "Always launch with…".
  int launcherIndex = -1;
  if (owning.launcher.launcherCount() > 0) {
    const QString expandedMediaDir =
        PathUtils::validateAndExpandPath(owning.mediaDirectory, owning.name);
    const QString uuid = CollectionUtils::computeCollectionUuid(owning.name, expandedMediaDir);
    if (!uuid.isEmpty()) {
      const auto md = databaseMgr()->loadItemMetadata(uuid, filePath);
      if (md.launcherIndex >= 0 && md.launcherIndex < owning.launcher.launcherCount()) {
        launcherIndex = md.launcherIndex;
      }
    }
    if (launcherIndex < 0) {
      launcherIndex =
          std::clamp(owning.launcher.defaultLauncherIndex, 0, owning.launcher.launcherCount() - 1);
    }
  } else {
    launcherIndex = 0;
  }

  const LauncherConfig launcher = LauncherUtils::resolvePreset(
      owning.launcher.launcherAt(launcherIndex),
      m_generalSettings ? m_generalSettings->launchers.launcherPresets : QList<LauncherPreset>{});
  const QString launcherName = launcher.name.trimmed().isEmpty()
                                   ? owning.launcher.launcherDisplayName(launcherIndex)
                                   : launcher.name.trimmed();

  const auto preview = LaunchManager::previewLaunchCommand(owning, launcher, filePath);
  m_runLaunchPreviewDialog(itemName, launcherName, filePath, preview);
}
