#ifndef ITEMMETADATAACTIONCONTROLLER_H
#define ITEMMETADATAACTIONCONTROLLER_H

#include <functional>
#include <optional>

#include <QString>

#include "applicationcontext_fwd.h"
// EditMetadataPayload — the edit-dialog value object the runner closure
// round-trips. Full definition needed for the std::function signature.
#include "itemmetadata.h"

class IDatabaseManager;
class IDetailsPaneManager;
struct CollectionConfig;
template <typename T> class QList;

/**
 * @brief Setup struct for ItemMetadataActionController dependencies.
 */
struct ItemMetadataActionControllerSetup {
  const ApplicationContext *ctx = nullptr;

  // Sibling managers come from ctx; only non-manager fields stay here.
  QList<CollectionConfig> *collections = nullptr;
  int *currentCollectionIndex = nullptr;

  /// Owner-supplied modal edit dialog (Kartend-n8kh): same signature as
  /// InteractionManager's EditMetadataDialogRunner, restated here so this
  /// header doesn't include interactionmanager.h (which forward-declares
  /// this class). Null in headless contexts; editItemMetadata guards.
  std::function<std::optional<EditMetadataPayload>(const QString &itemTitle,
                                                   const EditMetadataPayload &initial)>
      runEditMetadataDialog;
};

/**
 * @brief Item-metadata mutations reachable from the right-click context menu.
 *
 * Extracted from InteractionManager (Kartend-5lmt7): the curation actions —
 * edit-metadata dialog round-trip, manual-path / launcher-override setters,
 * pin / hide / continue-later toggles — lived on the input coordinator only
 * because the context menu is where the user reaches them. They mutate the
 * item_metadata store and refresh the sidebar; nothing about them is input
 * coordination. InteractionManager keeps one-line delegating wrappers, so
 * every existing caller (context-menu lambdas, MainWindow's edit-metadata
 * entry points) is unchanged.
 *
 * Not a QObject: the actions are imperative (dialog → DB write → sidebar
 * refresh) with no signals of their own. Borrowed state (collections /
 * current index) follows the same lifetime contract as InteractionManager's
 * own copies — owned by MainWindow, outliving both.
 */
class ItemMetadataActionController {
public:
  ItemMetadataActionController() = default;
  ItemMetadataActionController(const ItemMetadataActionController &) = delete;
  ItemMetadataActionController &operator=(const ItemMetadataActionController &) = delete;

  void setupReferences(const ItemMetadataActionControllerSetup &setup);

  /// Modal curation-edit round-trip: load the row, seed the owner-supplied
  /// dialog, persist the edited payload stamped source="user", refresh the
  /// sidebar immediately (the user is staring at the row they just edited).
  void editItemMetadata(const QString &filePath, const QString &itemName);
  /// Persist a manual (documentation) path for the item; empty clears it.
  void setItemManualPath(const QString &filePath, const QString &manualPath);
  /// Pin the item to a specific launcher index; negative clears the override.
  void setItemLauncherOverride(const QString &filePath, int launcherIndex);
  void toggleItemPinned(const QString &filePath);
  void toggleItemHidden(const QString &filePath);
  void toggleItemContinueLater(const QString &filePath);

private:
  // ctx is the single source of truth for sibling managers (same rule as
  // InteractionManager: never cache sibling-manager pointers as fields).
  const ApplicationContext *m_ctx = nullptr;
  [[nodiscard]] IDatabaseManager *databaseMgr() const;
  [[nodiscard]] IDetailsPaneManager *detailsPaneMgr() const;

  /// Resolves the (collection_uuid) half of the metadata key for a media
  /// item, preferring the item's owning collection (which may differ from
  /// the displayed one in showAllSubcollectionItems mode). Empty on failure
  /// so callers early-out instead of writing a row keyed by a stale
  /// collection.
  [[nodiscard]] QString resolveOwningUuid(const QString &filePath) const;

  /// Shared tail of the toggle actions: load the row, apply @p mutate,
  /// stamp source="user", save, refresh the sidebar.
  void mutateMetadata(const QString &filePath,
                      const std::function<void(ItemMetadataStore::ItemMetadata &)> &mutate);

  QList<CollectionConfig> *m_collections = nullptr;
  int *m_currentCollectionIndex = nullptr;
  std::function<std::optional<EditMetadataPayload>(const QString &, const EditMetadataPayload &)>
      m_runEditMetadataDialog;
};

#endif // ITEMMETADATAACTIONCONTROLLER_H
