// Sibling translation unit for InteractionManager.
// Context-menu *action handlers* — the slot-side of the right-click menu
// items showContextMenu() builds. These are split out of
// interactionmanager_contextmenu.cpp so that file can stay focused on the
// QMenu / action assembly. Each function below is a discrete user action:
// add-to-playlist, smart-playlist CRUD, playlist import/export, and the
// command-preview dialog. The item-metadata mutation cluster (edit dialog,
// manual path, launcher override, pin/hide/continue-later) moved to
// ItemMetadataActionController (Kartend-5lmt7); the one-line delegates for
// it live at the top of this file.

#include "interactionmanager.h"

#include "itemmetadataactioncontroller.h"

#include <QFileInfo>

#include "collection/collectionconfig.h"
#include "collection/launcherpreset.h"
#include "collection/typehelpers.h"
#include "collection/validationhelpers.h"
// The two dialogs (createsmartplaylistdialog.h / editmetadatadialog.h) are
// launched via owner-supplied closures so the input layer doesn't need the
// ui/ dialog headers. EditMetadataPayload's full definition lives in
// itemmetadata.h, included below.
#include "idatabasemanager.h"
#include "itemmetadata.h"
#include "launchmanager.h"
#include "pathutils.h"

#include <algorithm>
#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcInteractionManager)

// Kartend-5lmt7: the playlist action bodies moved to PlaylistMenuController
// (playlistmenucontroller.cpp). One-line delegates below keep the context-menu
// lambdas in interactionmanager_contextmenu.cpp unchanged.

void InteractionManager::addItemToNewPlaylist(const QString &srcUuid, const QString &filePath) {
  if (m_playlistMenu) {
    m_playlistMenu->addItemToNewPlaylist(srcUuid, filePath);
  }
}

void InteractionManager::addItemToPlaylist(const QString &playlistId, const QString &srcUuid,
                                           const QString &filePath) {
  if (m_playlistMenu) {
    m_playlistMenu->addItemToPlaylist(playlistId, srcUuid, filePath);
  }
}

void InteractionManager::createSmartPlaylistDialog() {
  if (m_playlistMenu) {
    m_playlistMenu->createSmartPlaylistDialog();
  }
}

void InteractionManager::editSmartPlaylistDialog(const QString &playlistId,
                                                 const QString &currentName) {
  if (m_playlistMenu) {
    m_playlistMenu->editSmartPlaylistDialog(playlistId, currentName);
  }
}

void InteractionManager::renamePlaylistDialog(const QString &playlistId,
                                              const QString &currentName) {
  if (m_playlistMenu) {
    m_playlistMenu->renamePlaylistDialog(playlistId, currentName);
  }
}

void InteractionManager::deletePlaylistConfirm(const QString &playlistId,
                                               const QString &currentName) {
  if (m_playlistMenu) {
    m_playlistMenu->deletePlaylistConfirm(playlistId, currentName);
  }
}

// Kartend-5lmt7: the item-metadata mutation bodies moved to
// ItemMetadataActionController (itemmetadataactioncontroller.cpp). The
// methods below stay as one-line delegates so every existing caller — the
// context-menu lambdas in interactionmanager_contextmenu.cpp and MainWindow's
// edit-metadata entry points — is unchanged.

void InteractionManager::editItemMetadata(const QString &filePath, const QString &itemName) {
  if (m_itemMetadataActions) {
    m_itemMetadataActions->editItemMetadata(filePath, itemName);
  }
}

void InteractionManager::setItemManualPath(const QString &filePath, const QString &manualPath) {
  if (m_itemMetadataActions) {
    m_itemMetadataActions->setItemManualPath(filePath, manualPath);
  }
}

void InteractionManager::setItemLauncherOverride(const QString &filePath, int launcherIndex) {
  if (m_itemMetadataActions) {
    m_itemMetadataActions->setItemLauncherOverride(filePath, launcherIndex);
  }
}

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

void InteractionManager::toggleItemPinned(const QString &filePath) {
  if (m_itemMetadataActions) {
    m_itemMetadataActions->toggleItemPinned(filePath);
  }
}

void InteractionManager::toggleItemHidden(const QString &filePath) {
  if (m_itemMetadataActions) {
    m_itemMetadataActions->toggleItemHidden(filePath);
  }
}

void InteractionManager::toggleItemContinueLater(const QString &filePath) {
  if (m_itemMetadataActions) {
    m_itemMetadataActions->toggleItemContinueLater(filePath);
  }
}

void InteractionManager::exportPlaylistToFile(const QString &playlistId, const QString &currentName,
                                              bool asJson) {
  if (m_playlistMenu) {
    m_playlistMenu->exportPlaylistToFile(playlistId, currentName, asJson);
  }
}

void InteractionManager::importPlaylistFromFile() {
  if (m_playlistMenu) {
    m_playlistMenu->importPlaylistFromFile();
  }
}
