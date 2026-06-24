#ifndef SCRAPERESULTSELECTIONMODEL_H
#define SCRAPERESULTSELECTIONMODEL_H

// Owns the scraper dialog's collection-tree + items-list selection state:
// which collections are checked, which item paths inside each are queued,
// the lazily DB-fetched item cache, and each item's owning-collection
// index (so a "shell" parent routes per item to its real owner). Drives
// the QTreeWidget / QListWidget population and check-cascade in the
// unified setup view. Decoupled from ScrapeResultDialog (Kartend-hhv2u):
// the host injects its view widgets via setView() and the collection
// list + app context via setContext(), so the selection/dedup logic is
// unit-testable with standalone widgets and a real SQLite DB. onScrapeClicked
// (still on the unified controller) reads the picks back through the const
// accessors below.

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QLabel;
class QListWidget;
class QListWidgetItem;
class QTreeWidget;
class QTreeWidgetItem;
QT_END_NAMESPACE

struct ApplicationContext;
struct CollectionConfig;

class ScrapeResultSelectionModel : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ScrapeResultSelectionModel)

public:
  explicit ScrapeResultSelectionModel(QObject *parent = nullptr);

  /// Inject the view widgets. Called once by
  /// ScrapeResultDialogUnified::buildUnifiedPanel after the widgets exist
  /// (they are created later than this model). Borrowed, not owned.
  void setView(QTreeWidget *collectionTree, QListWidget *itemsList, QLabel *itemsHeaderLabel);

  /// Inject the live collection list + application context. Called from
  /// ScrapeResultDialog::setScraperContext (the context arrives after
  /// construction). `collections` is borrowed (MainWindow-owned); `ctx`
  /// supplies the IDatabaseManager for the lazy per-collection item fetch.
  void setContext(QList<CollectionConfig> *collections, const ApplicationContext *ctx);

  /// Build the collection QTreeWidget from the injected collection list,
  /// mirroring the parent/child hierarchy.
  void populateCollectionTree();
  /// Render the items QListWidget for @p collectionIndex; fetches from
  /// the DB on first display (async) then caches for later clicks.
  void rebuildItemsList(int collectionIndex);
  /// Seed / clear a collection's inclusion set when its tree checkbox
  /// toggles. Newly-checked defaults to "every known item".
  void applyCollectionCheckState(int collectionIndex, bool checked);
  /// Bulk-toggle every enabled row in the currently-displayed items
  /// list (the Select-all / Select-none buttons). No-op when no
  /// collection is on screen.
  void setAllItemsChecked(bool checked);
  /// Right-click flow: pre-check exactly @p preCollectionIndex and, when
  /// @p preItemPath is non-empty, scope its inclusion set to that single
  /// item. Leaves every other collection unchecked.
  void preCheckSingleItem(int preCollectionIndex, const QString &preItemPath);
  /// Sum of every checked collection's inclusion-set size.
  [[nodiscard]] int totalCheckedItemCount() const;

  // ── Read-back accessors for onScrapeClicked (lives on the unified
  //    controller). The controller resolves checked rows to owning
  //    collections via these instead of touching the maps directly.
  [[nodiscard]] const QHash<int, QStringList> &itemSelectionByCollection() const {
    return m_itemSelectionByCollection;
  }
  [[nodiscard]] const QHash<int, QHash<QString, int>> &itemOwnerByCollection() const {
    return m_itemOwnerByCollection;
  }
  [[nodiscard]] int collectionIndexForRow(const QTreeWidgetItem *row) const;

public slots:
  // Slot bodies wired to the collection tree / items list widget signals
  // in buildUnifiedPanel.
  void onCollectionTreeCurrentChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous);
  void onCollectionCheckChanged(QTreeWidgetItem *item, int column);
  void onItemCheckChanged(QListWidgetItem *item);

private:
  // Injected view widgets (borrowed; owned by the host dialog). Null until
  // setView() — the model's methods run only after the unified panel is built.
  QTreeWidget *m_collectionTree = nullptr;
  QListWidget *m_itemsList = nullptr;
  QLabel *m_itemsHeaderLabel = nullptr;
  // Injected context (borrowed). Null until setContext().
  QList<CollectionConfig> *m_collections = nullptr;
  const ApplicationContext *m_ctx = nullptr;

  /// Per-collection-index inclusion sets. When a collection's tree
  /// checkbox is on, this list dictates which item paths to scrape
  /// from that collection (defaults to "all" when the collection is
  /// first checked). Persists across collection clicks so the user
  /// can freely switch between collections without losing per-item
  /// selections.
  QHash<int, QStringList> m_itemSelectionByCollection;
  /// All item paths per collection — populated lazily as the user
  /// clicks a collection in the tree (or via DB fetch). Cached so
  /// re-clicking a collection doesn't re-hit the database.
  QHash<int, QStringList> m_itemsCacheByCollection;
  /// Per viewed-collection: item path → the collection index that
  /// actually owns that item. For a plain collection every item maps
  /// to the collection itself; for a "shell" parent that displays its
  /// subcollections' items the entries point at the owning
  /// subcollection. onScrapeClicked() uses this to route each item's
  /// scraped artwork + metadata to its real owner instead of the
  /// parent. Populated from the itemsRangeLoaded fetch alongside
  /// m_itemsCacheByCollection.
  QHash<int, QHash<QString, int>> m_itemOwnerByCollection;
  /// Tree row → collection index map, populated when the tree is built.
  QHash<QTreeWidgetItem *, int> m_treeItemToCollectionIndex;
};

#endif // SCRAPERESULTSELECTIONMODEL_H
