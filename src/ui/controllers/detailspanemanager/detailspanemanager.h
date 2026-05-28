#ifndef DETAILSPANEMANAGER_H
#define DETAILSPANEMANAGER_H

#include "collectionutils.h"
#include "idetailspanemanager.h"
#include "setuputils.h"
#include <functional>
#include <optional>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QWidget>

class QHBoxLayout;
class QVBoxLayout;
class QScrollArea;
class DetailsPane;
class IDetailsPane;
class ItemWidget;
class SettingsManager;
class ArtworkManager;
class DatabaseManager;
#include "applicationcontext_fwd.h"
namespace TimerUtils {
class DebouncedTimer;
}

/// Input bundle for the ItemArtworkLinksDialog runner. The dialog needs
/// both the configured custom artwork types (so it can render the right
/// rows) and the auto-resolved on-disk paths (so it can show the "auto"
/// hint for rows the user hasn't manually overridden).
struct ItemArtworkLinksInput {
  QString itemTitle;
  QStringList standardTypes;
  QStringList customTypes;
  QHash<QString, QString> overrides;         // artworkType -> manualPath
  QHash<QString, QString> autoResolvedPaths; // artworkType -> resolved path
  QString browseStartDir;
};

/// Runs the modal ItemArtworkLinksDialog seeded with @p input. Returns
/// the edited overrides on accept, or nullopt on cancel. Kartend-n8kh:
/// supplied by the UI layer so DetailsPaneManager (media-layer) doesn't
/// #include the dialog header.
using ItemArtworkLinksDialogRunner =
    std::function<std::optional<QHash<QString, QString>>(const ItemArtworkLinksInput &)>;

/// Owner-supplied bridge to the same edit-metadata flow the right-click
/// context menu drives. Kartend-oewu: the DetailsPane's inline pencil
/// button fires editMetadataRequested → the manager looks up the current
/// selection state it already tracks (m_currentItemFilePath /
/// m_currentItemName) and invokes this runner. The closure body lives on
/// MainWindow because the editItemMetadata flow needs InteractionManager
/// + a refreshSidebarMetadataImmediate hop on success.
using EditMetadataForItemRunner =
    std::function<void(const QString &filePath, const QString &itemName)>;

struct DetailsPaneManagerSetup {
  const ApplicationContext *ctx = nullptr;

  IDetailsPane *sidebar = nullptr;
  QWidget *itemsPage = nullptr;
  QHBoxLayout *mainLayout = nullptr;
  /// outer vertical layout (`itemsPageLayout`) the details pane
  /// docks into for Top/Bottom Expand mode. Optional — without it, T/B Expand
  /// falls back to the L/R behavior so older callers keep compiling.
  QVBoxLayout *outerLayout = nullptr;
  /// the content widget that holds `mainLayout`. Used to locate
  /// the insertion index when docking the pane above (Top) or below (Bottom)
  /// it in `outerLayout`.
  QWidget *contentWidget = nullptr;
  QScrollArea *scrollArea = nullptr;
  QList<CollectionConfig> *collections = nullptr;

  /// Owner-supplied runner for the per-item artwork-links dialog
  /// (Kartend-n8kh). Null in headless contexts; the call site guards.
  ItemArtworkLinksDialogRunner runArtworkLinksDialog;

  /// Owner-supplied bridge to the right-click "Edit metadata…" flow
  /// (Kartend-oewu). Null in headless contexts.
  EditMetadataForItemRunner runEditMetadataForItem;

  SETUP_GETTER_DECL(IDetailsPane *, Sidebar)
  SETUP_GETTER_DECL(QWidget *, ItemsPage)
  SETUP_GETTER_DECL(QScrollArea *, ScrollArea)
  SETUP_GETTER_DECL(QList<CollectionConfig> *, Collections)
};

// QObject must be the first base; IDetailsPaneManager is a plain (non-QObject)
// role interface — single-QObject-base multiple inheritance.
class DetailsPaneManager : public QObject, public IDetailsPaneManager {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(DetailsPaneManager)

public:
  explicit DetailsPaneManager(QObject *parent = nullptr);
  void setupReferences(const DetailsPaneManagerSetup &setup);
  void setupSidebar();
  void toggleSidebar() override;
  /// Debounced sidebar refresh. Coalesces rapid selection changes
  /// (wheel/arrow storms) into a single update once the selection
  /// settles — the actual refresh runs ~METADATA_DEBOUNCE_MS after
  /// the last call. Deselect (empty path) bypasses the debounce so
  /// clears feel instant.
  void updateSidebarMetadata(ItemWidget *selectedItem) override;
  /// Path-based overload used when no ItemWidget is materialized for the
  /// selection (notably Cover Flow, which renders its own CoverFlowCards
  /// instead of ItemWidgets in the virtual grid). Populates
  /// m_currentItemContext so DetailPageManager and other consumers can
  /// resolve the same item without an ItemWidget pointer.
  void updateSidebarMetadata(const QString &filePath, const QString &itemName) override;
  /// Force an immediate sidebar refresh, bypassing the debounce. Use
  /// from explicit user actions where the latency would be noticed —
  /// post-edit (context-menu metadata changes) and tab switches. Uses
  /// the most recent pending args if a debounced update is queued,
  /// otherwise falls back to the currently-displayed item context.
  void refreshSidebarMetadataImmediate() override;
  void applySidebarStateForCollection(int collectionIndex) override;
  void updateSidebarLayout(int currentCollectionIndex) override;
  void positionSidebarOverlay();
  /// Recomputes the collection-level summary the sidebar shows when no
  /// item is selected. Cheap; safe to call after collection
  /// switches, scan completions, or settings saves.
  void refreshCollectionSummary();
  [[nodiscard]] bool isSidebarVisible() const override;
  [[nodiscard]] IDetailsPane *sidebarWidget() const override;
  void saveSidebarStateForCollection(int collectionIndex, bool visible);
  void saveSidebarStateForCollection(const QString &collectionName, bool visible);
  [[nodiscard]] int currentCollectionIndex() const { return m_currentCollectionIndex; }

  /// external override that hides the sidebar without touching
  /// the persisted per-collection sidebarVisible flag. Cover flow uses this
  /// to take the full viewport while preserving the user's sidebar
  /// preference for grid/list views. Setting back to false re-runs layout
  /// from the persisted state. Toggling via toggleSidebar() also clears the
  /// override so a deliberate user toggle wins.
  void setExternallyHidden(bool hidden);
  [[nodiscard]] bool isExternallyHidden() const { return m_externallyHidden; }

  /// tracks whether a fullscreen overlay (artwork preview /
  /// expand-mode video) is currently visible. While true, the sidebar's
  /// raise() is skipped so the overlay can stay on top — without this flag,
  /// re-running updateSidebarLayout (e.g. after a window resize) would
  /// re-stack the sidebar above the active overlay.
  void setOverlayActive(bool active);

  /// ItemContext (the cached resolution context for the currently-displayed
  /// item) now lives in IDetailsPaneManager so the data contract travels
  /// with the interface; re-exported here for source compatibility.
  using ItemContext = IDetailsPaneManager::ItemContext;
  [[nodiscard]] const ItemContext &currentItemContext() const override {
    return m_currentItemContext;
  }

signals:
  void sidebarVisibilityChanged(bool visible);
  void sidebarLayoutChanged();

public slots:
  /// Kartend-2hzy: per-collection sidebarAppearanceChanged receiver. Fires
  /// when any save path (kart import, right-click edit, inline toolbar
  /// commit) actually mutates SidebarAppearance on a collection. Re-applies
  /// the persisted sidebar state + relayouts the pane when the diff hits
  /// the currently-viewed collection; no-op otherwise (next
  /// switchCollection picks it up via the normal apply path).
  void onSidebarAppearanceChanged(int collectionIndex, const SidebarAppearance &sidebar);

private slots:
  /// Opens the per-item artwork-link editor dialog for the current
  /// selection. Persists the user's diff via DatabaseManager
  /// and refreshes the gallery so newly-set overrides appear immediately.
  void openArtworkLinksDialog();

  /// Opens the EditMetadataDialog for the current selection (Kartend-oewu).
  /// Delegates the actual save to the runner injected through setup; the
  /// manager only contributes the (filePath, itemName) it already tracks.
  void openEditMetadataDialog();

private:
  /// The actual heavy work: 4 DB queries + filesystem probes that
  /// updateSidebarMetadata used to do inline. Routed through the
  /// debouncer; called directly only by refreshSidebarMetadataImmediate.
  void performSidebarMetadataUpdate(const QString &filePath, const QString &itemName);

  DetailsPane *m_DetailsPane;
  QWidget *m_itemsPage;
  QHBoxLayout *m_mainHorizontalLayout;
  /// outer vertical layout used for Top/Bottom Expand dock.
  /// nullptr when a caller doesn't supply one — T/B Expand falls back to
  /// Overlay-style absolute positioning in that case.
  QVBoxLayout *m_outerLayout = nullptr;
  /// the widget owning `m_mainHorizontalLayout`. Used to anchor
  /// the pane's insertion index in `m_outerLayout`.
  QWidget *m_mainContentWidget = nullptr;
  QScrollArea *m_itemScrollArea;
  // ctx is the single source of truth for sibling managers (SettingsManager,
  // ArtworkManager, DatabaseManager).
  const ApplicationContext *m_ctx = nullptr;
  QList<CollectionConfig> *m_collections = nullptr;
  /// Kartend-n8kh: artwork-links dialog runner copied from setup. Null
  /// in headless contexts; metadata callers guard before invoking.
  ItemArtworkLinksDialogRunner m_runArtworkLinksDialog;
  /// Kartend-oewu: edit-metadata bridge — see EditMetadataForItemRunner.
  EditMetadataForItemRunner m_runEditMetadataForItem;
  bool m_sidebarVisible = false;
  /// separate from m_sidebarVisible because this flag is
  /// driven by the active view type (cover flow auto-hides) rather than
  /// the user's per-collection preference. Effective visibility is the
  /// AND of (!m_externallyHidden) and m_sidebarVisible.
  bool m_externallyHidden = false;
  /// see setOverlayActive doc above.
  bool m_overlayActive = false;
  int m_currentCollectionIndex;

  // Snapshot of the currently-displayed item used by the artwork link
  // editor and the detail page. Captured in
  // updateSidebarMetadata so callers don't have to recompute owning
  // collection / UUID resolution.
  QString m_currentItemFilePath;
  QString m_currentItemName;
  QString m_currentItemUuid;
  int m_currentItemOwningIndex = -1;
  QString m_currentItemArtworkDir;
  ItemContext m_currentItemContext;

  /// Coalesces rapid updateSidebarMetadata calls during wheel/arrow
  /// storms into a single performSidebarMetadataUpdate once the
  /// selection settles. Constructed in setupReferences so we don't
  /// need a QObject parent at ctor time.
  TimerUtils::DebouncedTimer *m_metadataDebouncer = nullptr;
  QString m_pendingMetadataFilePath;
  QString m_pendingMetadataItemName;
};

#endif
