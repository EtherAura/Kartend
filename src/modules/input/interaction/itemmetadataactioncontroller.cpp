// Item-metadata mutation handlers extracted verbatim from
// interactionmanager_contextactions.cpp (Kartend-5lmt7). Each method is a
// discrete user action reachable from the right-click context menu (and, for
// editItemMetadata, from MainWindow's edit entry points via
// InteractionManager's delegating wrapper): modal curation edit, manual-path
// and launcher-override setters, pin / hide / continue-later toggles. All
// share the same shape — resolve the item's owning collection uuid, load the
// item_metadata row, mutate, stamp source="user", save, refresh the sidebar
// immediately.

#include "itemmetadataactioncontroller.h"

#include <algorithm>

#include "applicationcontext.h"
#include "collection/collectionconfig.h"
#include "collection/typehelpers.h"
#include "collection/validationhelpers.h"
#include "idatabasemanager.h"
#include "idetailspanemanager.h"
#include "pathutils.h"

IDatabaseManager *ItemMetadataActionController::databaseMgr() const {
  return m_ctx ? m_ctx->databaseManager() : nullptr;
}

IDetailsPaneManager *ItemMetadataActionController::detailsPaneMgr() const {
  return m_ctx ? m_ctx->detailsPaneManager() : nullptr;
}

void ItemMetadataActionController::setupReferences(const ItemMetadataActionControllerSetup &setup) {
  m_ctx = setup.ctx;
  m_collections = setup.collections;
  m_currentCollectionIndex = setup.currentCollectionIndex;
  m_runEditMetadataDialog = setup.runEditMetadataDialog;
}

QString ItemMetadataActionController::resolveOwningUuid(const QString &filePath) const {
  // The owning collection of the file may differ from the displayed one in
  // showAllSubcollectionItems mode; mirror DetailsPaneManager and prefer the
  // file's resolved collection so the metadata row's UUID matches across
  // navigation modes.
  if (!databaseMgr() || !m_collections || !m_currentCollectionIndex) {
    return {};
  }
  int owningIndex = databaseMgr()->getCollectionIndexForFile(filePath);
  if (owningIndex < 0) {
    owningIndex = *m_currentCollectionIndex;
  }
  if (!CollectionUtils::isValidIndex(owningIndex, m_collections)) {
    return {};
  }
  const CollectionConfig &owning = (*m_collections)[owningIndex];
  const QString expandedMediaDir =
      PathUtils::validateAndExpandPath(owning.mediaDirectory, owning.name);
  return CollectionUtils::computeCollectionUuid(owning.name, expandedMediaDir);
}

void ItemMetadataActionController::mutateMetadata(
    const QString &filePath, const std::function<void(ItemMetadataStore::ItemMetadata &)> &mutate) {
  const QString uuid = resolveOwningUuid(filePath);
  if (uuid.isEmpty()) {
    return;
  }
  ItemMetadataStore::ItemMetadata md = databaseMgr()->loadItemMetadata(uuid, filePath);
  md.collectionUuid = uuid;
  md.path = filePath;
  mutate(md);
  // User-driven edit: stamp the source so future scrapers know this row was
  // touched by the user (matches editItemMetadata behavior).
  md.source = QStringLiteral("user");
  if (databaseMgr()->saveItemMetadata(md) && detailsPaneMgr()) {
    // Refresh the sidebar so the new fields render immediately. Bypass the
    // debounce — the metadata the user edited is for the displayed item.
    detailsPaneMgr()->refreshSidebarMetadataImmediate();
  }
}

void ItemMetadataActionController::editItemMetadata(const QString &filePath,
                                                    const QString &itemName) {
  const QString uuid = resolveOwningUuid(filePath);
  if (uuid.isEmpty()) {
    return;
  }

  ItemMetadataStore::ItemMetadata metadata = databaseMgr()->loadItemMetadata(uuid, filePath);
  metadata.collectionUuid = uuid;
  metadata.path = filePath;

  if (!m_runEditMetadataDialog) {
    return;
  }

  EditMetadataPayload initial;
  initial.notes = metadata.notes;
  initial.tags = ItemMetadataStore::parseTags(metadata.tags);
  initial.rating = metadata.rating;
  initial.sourceUrl = metadata.sourceUrl;
  initial.customFields = ItemMetadataStore::parseCustomFields(metadata.customFields);

  auto edited = m_runEditMetadataDialog(itemName, initial);
  if (!edited.has_value()) {
    return;
  }

  metadata.notes = edited->notes;
  metadata.tags = ItemMetadataStore::serializeTags(edited->tags);
  metadata.rating = edited->rating;
  metadata.sourceUrl = edited->sourceUrl;
  metadata.customFields = ItemMetadataStore::serializeCustomFields(edited->customFields);
  // Mark the row as user-edited so future scraper integrations can decide
  // whether to overwrite. Existing rows from a scraper keep their source
  // until the user touches them via this dialog.
  metadata.source = QStringLiteral("user");
  if (!databaseMgr()->saveItemMetadata(metadata)) {
    return;
  }

  // Refresh the sidebar so the new fields render immediately. Bypass
  // the debounce — the user is staring at the dialog they just dismissed
  // and the metadata they edited is for the currently-displayed item.
  if (detailsPaneMgr()) {
    detailsPaneMgr()->refreshSidebarMetadataImmediate();
  }
}

void ItemMetadataActionController::setItemManualPath(const QString &filePath,
                                                     const QString &manualPath) {
  mutateMetadata(
      filePath, [&manualPath](ItemMetadataStore::ItemMetadata &md) { md.manualPath = manualPath; });
}

void ItemMetadataActionController::setItemLauncherOverride(const QString &filePath,
                                                           int launcherIndex) {
  // Negative indices mean "clear the override". Clamp incoming overrides into
  // the visible launcher range so a stale UI pick can never pin past the end
  // — which needs the owning collection, so resolve it before the shared
  // mutate tail (mutateMetadata re-resolves the uuid; the double resolution
  // is two cached-index lookups, not worth widening the helper's signature).
  if (!databaseMgr() || !m_collections || !m_currentCollectionIndex) {
    return;
  }
  int owningIndex = databaseMgr()->getCollectionIndexForFile(filePath);
  if (owningIndex < 0) {
    owningIndex = *m_currentCollectionIndex;
  }
  if (!CollectionUtils::isValidIndex(owningIndex, m_collections)) {
    return;
  }
  const CollectionConfig &owning = (*m_collections)[owningIndex];
  const int clamped =
      launcherIndex < 0 ? -1 : std::clamp(launcherIndex, 0, owning.launcher.launcherCount() - 1);
  mutateMetadata(filePath,
                 [clamped](ItemMetadataStore::ItemMetadata &md) { md.launcherIndex = clamped; });
}

void ItemMetadataActionController::toggleItemPinned(const QString &filePath) {
  mutateMetadata(filePath, [](ItemMetadataStore::ItemMetadata &md) { md.isPinned = !md.isPinned; });
}

void ItemMetadataActionController::toggleItemHidden(const QString &filePath) {
  mutateMetadata(filePath, [](ItemMetadataStore::ItemMetadata &md) { md.isHidden = !md.isHidden; });
}

void ItemMetadataActionController::toggleItemContinueLater(const QString &filePath) {
  mutateMetadata(filePath,
                 [](ItemMetadataStore::ItemMetadata &md) { md.continueLater = !md.continueLater; });
}
