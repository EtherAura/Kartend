#ifndef DETAILSPANEMANAGER_H
#define DETAILSPANEMANAGER_H

#include "collectionutils.h"
#include "setuputils.h"
#include <QObject>
#include <QString>
#include <QWidget>

class QHBoxLayout;
class QVBoxLayout;
class QScrollArea;
class DetailsPane;
class ItemWidget;
class SettingsManager;
class ArtworkManager;
class DatabaseManager;
struct ApplicationContext;

struct DetailsPaneManagerSetup {
  const ApplicationContext *ctx = nullptr;

  DetailsPane *sidebar = nullptr;
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

  SETUP_GETTER_DECL(DetailsPane *, Sidebar)
  SETUP_GETTER_DECL(QWidget *, ItemsPage)
  SETUP_GETTER_DECL(QScrollArea *, ScrollArea)
  SETUP_GETTER_DECL(QList<CollectionConfig> *, Collections)
};

class DetailsPaneManager : public QObject {
  Q_OBJECT

public:
  explicit DetailsPaneManager(QObject *parent = nullptr);
  void setupReferences(const DetailsPaneManagerSetup &setup);
  void setupSidebar();
  void toggleSidebar();
  void updateSidebarMetadata(ItemWidget *selectedItem);
  /// Path-based overload used when no ItemWidget is materialized for the
  /// selection (notably Cover Flow, which renders its own CoverFlowCards
  /// instead of ItemWidgets in the virtual grid). Populates
  /// m_currentItemContext so DetailPageManager and other consumers can
  /// resolve the same item without an ItemWidget pointer.
  void updateSidebarMetadata(const QString &filePath, const QString &itemName);
  void applySidebarStateForCollection(int collectionIndex);
  void updateSidebarLayout(int currentCollectionIndex);
  void positionSidebarOverlay();
  /// Recomputes the collection-level summary the sidebar shows when no
  /// item is selected. Cheap; safe to call after collection
  /// switches, scan completions, or settings saves.
  void refreshCollectionSummary();
  [[nodiscard]] bool isSidebarVisible() const;
  [[nodiscard]] DetailsPane *sidebarWidget() const { return m_DetailsPane; }
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

  /// cached resolution context for the currently-displayed item.
  /// Populated in updateSidebarMetadata() so siblings (e.g. DetailPageManager)
  /// can render the same item without redoing the showAllSubcollectionItems-
  /// aware owner / artwork / video / manual directory lookup. `uuid` is empty
  /// when no item is selected, the lookup failed, or the user is showing the
  /// collection summary (no-selection state).
  struct ItemContext {
    QString filePath;
    QString itemName;
    QString uuid;         // collection_uuid for DB metadata lookup
    QString artworkDir;   // expanded, owner-aware artwork directory
    QString videoDir;     // expanded, owner-aware video directory
    QString manualDir;    // expanded, owner-aware manual directory
    int owningIndex = -1; // index in m_collections, -1 if unknown
    [[nodiscard]] bool isValid() const { return !filePath.isEmpty() && !uuid.isEmpty(); }
  };
  [[nodiscard]] const ItemContext &currentItemContext() const { return m_currentItemContext; }

signals:
  void sidebarVisibilityChanged(bool visible);
  void sidebarLayoutChanged();

private slots:
  /// Opens the per-item artwork-link editor dialog for the current
  /// selection. Persists the user's diff via DatabaseManager
  /// and refreshes the gallery so newly-set overrides appear immediately.
  void openArtworkLinksDialog();

private:
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
};

#endif
