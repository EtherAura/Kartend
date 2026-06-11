// Sibling translation unit for InteractionManager.
// Right-click context menu implementation.
// These remain InteractionManager members; this is a translation-unit split.
#include "interactionmanager.h"

// Kartend-sqoq0: QInputDialog / QMessageBox / QProgressDialog includes were
// dead (no remaining uses in this TU) — only the QFileDialog fallback for the
// manual-file picker stays.
#include <QApplication>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QPointer>
#include <QUrl>

#include "artworkutils.h"
#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "collection/typehelpers.h"
#include "collection/validationhelpers.h"
// The two dialogs (createsmartplaylistdialog.h / editmetadatadialog.h) are
// launched via owner-supplied closures (InteractionManagerSetup::run*) so
// the input layer doesn't need the ui/ dialog headers for the symbols. The
// runners themselves live in MainWindow. EditMetadataPayload's full
// definition lives in itemmetadata.h, included below.
#include "idatabasemanager.h"
#include "idetailspane.h"
#include "idetailspanemanager.h"
#include "imainwindow.h"
#include "inavigationmanager.h"
#include "iplaylistmanager.h"
#include "iselectionmanager.h"
#include "itemmetadata.h"
#include "itemwidget.h"
#include "launchmanager.h"
#include "metadatalookupprovider.h"
#include "metadataprovider.h"
#include "metadataproviderregistry.h"
#include "pathutils.h"
#include "scrapepersistence.h"
#include "scrollmanager.h"

#include <algorithm>
#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcInteractionManager)

void InteractionManager::showContextMenu(ItemWidget *widget, int visualIndex,
                                         const QPoint &globalPos) {
  if (!widget || visualIndex < 0) {
    return;
  }

  // Select the right-clicked item so the user sees which item is targeted
  selectItemByIndex(visualIndex, true);

  const bool isSubcollection = widget->isSubcollection();
  const bool isVirtualFolder = widget->isVirtualFolder();
  const bool isMediaItem = !isSubcollection && !isVirtualFolder;
  const QString filePath = widget->getFilePath();

  QMenu menu;

  // --- Launch action (only for media items with a file path) ---
  if (isMediaItem && !filePath.isEmpty()) {
    QAction *launchAction = menu.addAction(tr("Launch"));
    QObject::connect(launchAction, &QAction::triggered, this, [this, filePath]() {
      if (!databaseMgr() || !m_currentCollectionIndex) {
        return;
      }
      const int cIdx = databaseMgr()->getCollectionIndexForFile(filePath);
      const int ownerIdx = (cIdx >= 0 ? cIdx : *m_currentCollectionIndex);
      launchItemWithCollection(filePath, ownerIdx);
    });
  }

  // --- Open (enter) action for subcollections and virtual folders ---
  if (isSubcollection || isVirtualFolder) {
    QAction *openAction = menu.addAction(tr("Open"));
    QObject::connect(openAction, &QAction::triggered, this, [this]() {
      if (scrollMgr()) {
        const int totalItems = scrollMgr()->getTotalItems();
        processEnterOrReturnKey(totalItems);
      }
    });
  }

  menu.addSeparator();

  // --- Toggle sidebar (properties) action ---
  QAction *propertiesAction = menu.addAction(tr("Properties"));
  QObject::connect(propertiesAction, &QAction::triggered, this, [this]() {
    if (detailsPaneMgr()) {
      detailsPaneMgr()->toggleSidebar();
    }
  });

  // --- Refresh action (soft reload of current collection) ---
  QAction *refreshAction = menu.addAction(tr("Refresh"));
  QObject::connect(refreshAction, &QAction::triggered, this, [this]() {
    if (navMgr() && m_currentCollectionIndex && *m_currentCollectionIndex >= 0) {
      navMgr()->safeReloadCollection(*m_currentCollectionIndex);
    }
  });

  // --- Edit per-item metadata (media items only) ---
  if (isMediaItem && !filePath.isEmpty() && databaseMgr() && m_collections &&
      m_currentCollectionIndex) {
    menu.addSeparator();
    QAction *editMetadataAction = menu.addAction(tr("Edit metadata..."));
    const QString &itemName = widget->getItemName();
    QObject::connect(editMetadataAction, &QAction::triggered, this,
                     [this, filePath, itemName]() { editItemMetadata(filePath, itemName); });
    // Preview the launch command without spawning the launcher. Surfaces
    // resolved paths, archive-extraction behaviour, and any validation
    // warnings the dry-run picked up.
    if (m_runLaunchPreviewDialog) {
      QAction *previewAction = menu.addAction(tr("Preview launch command..."));
      QObject::connect(previewAction, &QAction::triggered, this,
                       [this, filePath, itemName]() { previewLaunchCommand(filePath, itemName); });
    }

    // --- Per-item state flag toggles (Pin / Hide / Continue later) ---
    // Read the current flags once so each menu label reflects the
    // current state ("Pin" vs "Unpin"). The owning collection's uuid is
    // resolved via the same fallback the editItemMetadata path uses, so
    // the toggle hits the same (collection_uuid, path) key the read
    // pipeline returns. uuid.isEmpty() means we can't resolve a stable
    // key — skip the flag group entirely rather than offering actions
    // that would silently no-op.
    int flagOwningIndex = databaseMgr()->getCollectionIndexForFile(filePath);
    if (flagOwningIndex < 0) {
      flagOwningIndex = *m_currentCollectionIndex;
    }
    QString flagUuid;
    bool isPinned = false;
    bool isHidden = false;
    bool continueLater = false;
    if (CollectionUtils::isValidIndex(flagOwningIndex, m_collections)) {
      const CollectionConfig &flagOwning = (*m_collections)[flagOwningIndex];
      const QString flagMediaDir =
          PathUtils::validateAndExpandPath(flagOwning.mediaDirectory, flagOwning.name);
      flagUuid = CollectionUtils::computeCollectionUuid(flagOwning.name, flagMediaDir);
      if (!flagUuid.isEmpty()) {
        const auto flagMeta = databaseMgr()->loadItemMetadata(flagUuid, filePath);
        isPinned = flagMeta.isPinned;
        isHidden = flagMeta.isHidden;
        continueLater = flagMeta.continueLater;
      }
    }
    if (!flagUuid.isEmpty()) {
      QAction *pinAction = menu.addAction(isPinned ? tr("Unpin") : tr("Pin to top"));
      QObject::connect(pinAction, &QAction::triggered, this,
                       [this, filePath]() { toggleItemPinned(filePath); });
      QAction *continueAction = menu.addAction(continueLater ? tr("Clear continue-later marker")
                                                             : tr("Mark as continue later"));
      QObject::connect(continueAction, &QAction::triggered, this,
                       [this, filePath]() { toggleItemContinueLater(filePath); });
      QAction *hideAction = menu.addAction(isHidden ? tr("Unhide") : tr("Hide"));
      QObject::connect(hideAction, &QAction::triggered, this,
                       [this, filePath]() { toggleItemHidden(filePath); });
    }

    // --- Look up online (Stage 1: URL providers only) ---
    // Submenu of metadata providers applicable to the current
    // collection's type — picks open the user's browser to a search
    // URL for the item's name. Future API providers will register on
    // the same MetadataProviderRegistry and surface here alongside
    // the URL ones.
    const CollectionConfig &activeCfg = (*m_collections)[*m_currentCollectionIndex];
    // Capture m_generalSettings into the registry call so API providers
    // can read user-supplied credentials via the live settings struct.
    // Collection accessor returns the active collection so SS can
    // resolve its per-collection systemeid override.
    const auto generalSettingsAccessor = [this]() -> const GeneralSettings * {
      return m_generalSettings;
    };
    const auto collectionAccessor = [this]() -> const CollectionConfig * {
      if (!m_collections || !m_currentCollectionIndex ||
          !CollectionUtils::isValidIndex(*m_currentCollectionIndex, m_collections)) {
        return nullptr;
      }
      return &(*m_collections)[*m_currentCollectionIndex];
    };
    const auto allProviders =
        MetadataProviderRegistry::builtIn(generalSettingsAccessor, collectionAccessor);
    const auto applicableProviders =
        MetadataProviderRegistry::forCategory(allProviders, activeCfg.type);
    // Search query = the item's display name when available, falling
    // back to the file basename — every provider's URL template wants
    // a human-readable query, not a filesystem path. Hoisted so both
    // the URL-search submenu and the API-scrape submenu below capture
    // the same value.
    QString queryText = itemName.trimmed();
    if (queryText.isEmpty()) {
      queryText = QFileInfo(filePath).completeBaseName();
    }
    if (!applicableProviders.isEmpty()) {
      QMenu *lookupMenu = menu.addMenu(tr("Look up online"));
      for (MetadataProvider *p : applicableProviders) {
        if (!p->capabilities().testFlag(MetadataProvider::Capability::WebSearch)) {
          continue;
        }
        const QUrl url = p->searchUrl(queryText);
        if (!url.isValid()) {
          continue;
        }
        QAction *action = lookupMenu->addAction(p->displayName());
        QObject::connect(action, &QAction::triggered, this,
                         [url]() { QDesktopServices::openUrl(url); });
      }
    }

    // --- Scrape with API provider (Stage 2: lookup + media download) ---
    // Surfaces only providers that implement MetadataLookupProvider
    // (i.e. have MetadataLookup capability). When more than one such
    // provider applies, group them in a submenu; for the common
    // single-provider case (currently MusicBrainz for audio
    // collections) attach the action to the menu directly so the
    // user can scrape in one click. Owning the picked provider via
    // the registry's unique_ptrs is a problem since this submenu
    // outlives the scope — we re-look up the provider by id when
    // the action fires.
    QList<MetadataProvider *> lookupProviders;
    for (MetadataProvider *p : applicableProviders) {
      if (p->capabilities().testFlag(MetadataProvider::Capability::MetadataLookup)) {
        lookupProviders.append(p);
      }
    }
    if (!lookupProviders.isEmpty()) {
      // Unified Scraper entry — opens the same dialog as File →
      // Scraper, pre-checked to the right-clicked item only. The
      // user's auto-accept / interactive choice + media-type picks
      // live inside the dialog now.
      QAction *scraperAction = menu.addAction(tr("Scraper…"));
      QObject::connect(scraperAction, &QAction::triggered, this, [this, filePath]() {
        // Reach the main window through its neutral IMainWindow role
        // interface — IMainWindow is a plain abstract base, so cross-cast
        // with dynamic_cast, not qobject_cast.
        auto *mw = dynamic_cast<IMainWindow *>(QApplication::activeWindow());
        if (!mw) {
          // Fall back to walking the parent chain — the active
          // window may be the menu's own QMenu while the click
          // is dispatching.
          for (QWidget *w = QApplication::focusWidget(); w; w = w->parentWidget()) {
            mw = dynamic_cast<IMainWindow *>(w);
            if (mw) break;
          }
        }
        if (!mw) return;
        int idx = -1;
        if (databaseMgr()) idx = databaseMgr()->getCollectionIndexForFile(filePath);
        if (idx < 0 && m_currentCollectionIndex) idx = *m_currentCollectionIndex;
        mw->openScraperDialog(idx, filePath);
      });
    }

    // --- Set / clear per-item manual override ---
    // Show "Set manual file..." always (lets the user point at any file).
    // Show "Clear manual override" only when an override is currently set,
    // mirroring how custom fields silently no-op when none exist.
    QAction *setManualAction = menu.addAction(tr("Set manual file..."));
    QObject::connect(setManualAction, &QAction::triggered, this, [this, filePath]() {
      const QString caption = tr("Select Manual File");
      const QString filter =
          tr("Manual Files (*.pdf *.epub *.cbr *.cbz *.djvu *.txt *.md *.html *.htm "
             "*.rtf *.doc *.docx *.odt *.png *.jpg *.jpeg);;All Files (*)");
      // Kartend-sqoq0: owner-supplied runner when wired; stock QFileDialog
      // fallback otherwise (headless tests stub the runner instead).
      const QString picked = m_dialogs.getOpenFileName
                                 ? m_dialogs.getOpenFileName(caption, QString(), filter)
                                 : QFileDialog::getOpenFileName(QApplication::activeWindow(),
                                                                caption, QString(), filter);
      if (picked.isEmpty()) {
        return;
      }
      setItemManualPath(filePath, picked);
    });

    // Only offer "Clear" when a manual_path override actually exists for the
    // selected item; querying the DB here keeps the menu honest about what
    // it can do (vs. always offering an action that may no-op).
    int owningIndex = databaseMgr()->getCollectionIndexForFile(filePath);
    if (owningIndex < 0) {
      owningIndex = *m_currentCollectionIndex;
    }
    if (CollectionUtils::isValidIndex(owningIndex, m_collections)) {
      const CollectionConfig &owning = (*m_collections)[owningIndex];
      const QString expandedMediaDir =
          PathUtils::validateAndExpandPath(owning.mediaDirectory, owning.name);
      const QString uuid = CollectionUtils::computeCollectionUuid(owning.name, expandedMediaDir);
      if (!uuid.isEmpty()) {
        const ItemMetadataStore::ItemMetadata md = databaseMgr()->loadItemMetadata(uuid, filePath);
        if (!md.manualPath.isEmpty()) {
          QAction *clearManualAction = menu.addAction(tr("Clear manual override"));
          QObject::connect(clearManualAction, &QAction::triggered, this,
                           [this, filePath]() { setItemManualPath(filePath, QString()); });
        }
      }

      // --- Per-item launcher override ---
      // Only meaningful when the owning collection has more than one
      // launcher — pinning a single-launcher collection to "launcher 0"
      // would be a no-op masquerading as a configuration choice.
      if (owning.launcher.launcherCount() > 1) {
        QAction *setLauncherAction = menu.addAction(tr("Always launch with..."));
        const int launcherCount = owning.launcher.launcherCount();
        const QString collectionName = owning.name;
        QStringList launcherNames;
        launcherNames.reserve(launcherCount);
        for (int i = 0; i < launcherCount; ++i) {
          launcherNames << owning.launcher.launcherDisplayName(i);
        }
        const int currentOverride =
            uuid.isEmpty() ? -1 : databaseMgr()->loadItemMetadata(uuid, filePath).launcherIndex;
        const int defaultIndex =
            currentOverride >= 0 && currentOverride < launcherCount
                ? currentOverride
                : std::clamp(owning.launcher.defaultLauncherIndex, 0, launcherCount - 1);
        QObject::connect(setLauncherAction, &QAction::triggered, this,
                         [this, filePath, collectionName, launcherNames, defaultIndex]() {
                           // The chooser dialog lives in the UI layer; route
                           // through LaunchManager's owner-injected callback.
                           if (!launchManager()) {
                             return;
                           }
                           const int chosen = launchManager()->promptLauncherChoice(
                               collectionName, launcherNames, defaultIndex);
                           if (chosen < 0) {
                             return; // User cancelled.
                           }
                           setItemLauncherOverride(filePath, chosen);
                         });
        if (currentOverride >= 0) {
          QAction *clearLauncherAction = menu.addAction(tr("Clear launcher override"));
          QObject::connect(clearLauncherAction, &QAction::triggered, this,
                           [this, filePath]() { setItemLauncherOverride(filePath, -1); });
        }
      }
    }
  }

  // ─── Playlist actions ──────────────────────────────────────
  // Two surfaces:
  //   (a) On any media item — "Add to playlist ▶ <list> | New playlist…"
  //       lets the user assemble playlists from anywhere in the library.
  //   (b) Inside a playlist — "Remove from playlist" so the chooser dialog
  //       isn't the only way out, plus rename/delete on the playlist itself.
  // The playlist itself is identified via the synthesized CollectionConfig at
  // m_currentCollectionIndex (isPlaylist=true) so the surrounding code keeps
  // treating playlists as ordinary virtual collections.
  if (playlistMgr() && m_collections && m_currentCollectionIndex && databaseMgr()) {
    const bool insideCollection =
        CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections);
    const bool insidePlaylist =
        insideCollection && (*m_collections)[*m_currentCollectionIndex].isPlaylist;

    if (isMediaItem && !filePath.isEmpty()) {
      // Resolve the source collection's uuid (using the file→collection map
      // built during the items range fetch). Add-to-playlist needs the uuid to
      // store a stable (uuid, path) reference that survives item id
      // renumbering across rescans.
      int owningIdx = databaseMgr()->getCollectionIndexForFile(filePath);
      if (owningIdx < 0 && !insidePlaylist) {
        owningIdx = *m_currentCollectionIndex;
      }
      QString srcUuid;
      if (CollectionUtils::isValidIndex(owningIdx, m_collections)) {
        const CollectionConfig &owning = (*m_collections)[owningIdx];
        const QString expandedMediaDir =
            PathUtils::validateAndExpandPath(owning.mediaDirectory, owning.name);
        srcUuid = CollectionUtils::computeCollectionUuid(owning.name, expandedMediaDir);
      }

      menu.addSeparator();

      // top-level favorites toggle. Faster than navigating into
      // the "Add to playlist" submenu and locating the favorites entry, since
      // starring is the most common per-item playlist action. The label flips
      // based on current membership so a single click is always meaningful.
      const QString favouritesId = playlistMgr()->ensureFavoritesPlaylist();
      if (!favouritesId.isEmpty() && !srcUuid.isEmpty()) {
        const bool alreadyFavorite = playlistMgr()->containsItem(favouritesId, srcUuid, filePath);
        QAction *favAction =
            menu.addAction(alreadyFavorite ? tr("Remove from Favorites") : tr("Add to Favorites"));
        QObject::connect(
            favAction, &QAction::triggered, this,
            [this, favouritesId, srcUuid, filePath, alreadyFavorite]() {
              if (!playlistMgr()) {
                return;
              }
              if (alreadyFavorite) {
                playlistMgr()->removeItem(favouritesId, srcUuid, filePath);
              } else {
                playlistMgr()->addItem(favouritesId, srcUuid, filePath);
              }
              // When the user is currently inside the favorites
              // playlist, the count just changed — reload so the
              // grid drops/appends the affected tile right away
              // rather than waiting for the next manual refresh.
              if (navMgr() && m_currentCollectionIndex &&
                  CollectionUtils::isValidIndex(m_currentCollectionIndex, m_collections) &&
                  (*m_collections)[*m_currentCollectionIndex].playlistId == favouritesId) {
                navMgr()->safeReloadCollection(*m_currentCollectionIndex);
              }
            });
      }

      QMenu *addToMenu = menu.addMenu(tr("Add to playlist"));

      QAction *newPlaylistAction = addToMenu->addAction(tr("New playlist…"));
      QObject::connect(newPlaylistAction, &QAction::triggered, this,
                       [this, srcUuid, filePath]() { addItemToNewPlaylist(srcUuid, filePath); });

      // Smart-playlist counterpart sits next to the static creator so the
      // discovery surface for "make a new playlist" stays one menu. The
      // right-clicked item is intentionally not added — smart playlists
      // populate from the filter, not from individual references.
      QAction *newSmartAction = addToMenu->addAction(tr("New smart playlist…"));
      QObject::connect(newSmartAction, &QAction::triggered, this,
                       [this]() { createSmartPlaylistDialog(); });

      // import a playlist from a JSON or M3U file. Lives next
      // to "New playlist…" rather than under a separate top-level entry so
      // the discovery surface for "create a playlist" is one place.
      QAction *importAction = addToMenu->addAction(tr("Import playlist from file…"));
      QObject::connect(importAction, &QAction::triggered, this,
                       [this]() { importPlaylistFromFile(); });

      const QList<PlaylistRow> playlists = playlistMgr()->loadAll();
      if (!playlists.isEmpty()) {
        addToMenu->addSeparator();
        // The chooser silently no-ops when the (uuid, path) pair is already in
        // the playlist (containsItem inside addItem) — keeps the menu honest
        // without requiring a separate "already added" disabled state.
        // Smart playlists are filtered out — manual adds are ignored by the
        // QueryManager smart-scope branch, so listing them as targets would
        // surface an action that silently does nothing.
        for (const PlaylistRow &row : playlists) {
          if (row.isSmart) {
            continue;
          }
          QString label = row.name.isEmpty() ? tr("(unnamed)") : row.name;
          QAction *action = addToMenu->addAction(label);
          const QString playlistId = row.id;
          QObject::connect(action, &QAction::triggered, this,
                           [this, playlistId, srcUuid, filePath]() {
                             addItemToPlaylist(playlistId, srcUuid, filePath);
                           });
        }
      }

      // Inside a static playlist, also offer the inverse action. Skipped
      // for smart playlists — removal would not stick (the next open
      // re-evaluates the filter and the item reappears).
      if (insidePlaylist && !(*m_collections)[*m_currentCollectionIndex].isSmartPlaylist) {
        const QString playlistId = (*m_collections)[*m_currentCollectionIndex].playlistId;
        QAction *removeAction = menu.addAction(tr("Remove from playlist"));
        QObject::connect(removeAction, &QAction::triggered, this,
                         [this, playlistId, srcUuid, filePath]() {
                           if (playlistMgr()) {
                             playlistMgr()->removeItem(playlistId, srcUuid, filePath);
                             // playlistsChanged → MainWindow re-syncs the
                             // CollectionConfigs; the safeReload below picks
                             // up the new (smaller) item count.
                             if (navMgr() && m_currentCollectionIndex) {
                               navMgr()->safeReloadCollection(*m_currentCollectionIndex);
                             }
                           }
                         });
      }
    }

    // Playlist-level actions when the user is currently viewing one. Kept off
    // the bottom of the menu so the per-item actions above remain the
    // primary affordance.
    if (insidePlaylist) {
      menu.addSeparator();
      const QString playlistId = (*m_collections)[*m_currentCollectionIndex].playlistId;
      const QString currentName = (*m_collections)[*m_currentCollectionIndex].name;
      // built-in playlists keep rename (so users can localize
      // the label) but hide delete — PlaylistManager refuses the call anyway,
      // and surfacing a button that always errors is worse UX than just
      // omitting it.
      const bool isReserved =
          !(*m_collections)[*m_currentCollectionIndex].playlistReservedKind.isEmpty();
      const bool isSmart = (*m_collections)[*m_currentCollectionIndex].isSmartPlaylist;

      QAction *renameAction = menu.addAction(tr("Rename playlist…"));
      QObject::connect(renameAction, &QAction::triggered, this, [this, playlistId, currentName]() {
        renamePlaylistDialog(playlistId, currentName);
      });

      // Smart-playlist edit action — sits next to rename so the two
      // metadata-edit affordances are adjacent. Hidden for static
      // playlists since there's nothing filter-shaped to edit.
      if (isSmart) {
        QAction *editFilterAction = menu.addAction(tr("Edit smart filter…"));
        QObject::connect(editFilterAction, &QAction::triggered, this,
                         [this, playlistId, currentName]() {
                           editSmartPlaylistDialog(playlistId, currentName);
                         });
      }

      // export the current playlist. The submenu houses both
      // formats so the menu stays scannable; M3U for cross-app interop, JSON
      // for lossless Kartend round-trip.
      QMenu *exportMenu = menu.addMenu(tr("Export playlist"));
      QAction *exportJsonAction = exportMenu->addAction(tr("As JSON…"));
      QObject::connect(exportJsonAction, &QAction::triggered, this,
                       [this, playlistId, currentName]() {
                         exportPlaylistToFile(playlistId, currentName, /*asJson=*/true);
                       });
      QAction *exportM3uAction = exportMenu->addAction(tr("As M3U…"));
      QObject::connect(exportM3uAction, &QAction::triggered, this,
                       [this, playlistId, currentName]() {
                         exportPlaylistToFile(playlistId, currentName, /*asJson=*/false);
                       });

      if (!isReserved) {
        QAction *deleteAction = menu.addAction(tr("Delete playlist…"));
        QObject::connect(
            deleteAction, &QAction::triggered, this,
            [this, playlistId, currentName]() { deletePlaylistConfirm(playlistId, currentName); });
      }
    }
  }

  menu.exec(globalPos);
}
