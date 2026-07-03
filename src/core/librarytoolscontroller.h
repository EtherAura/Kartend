#ifndef LIBRARYTOOLSCONTROLLER_H
#define LIBRARYTOOLSCONTROLLER_H

// Controller extracted from mainwindow_dialogs.cpp so that partial keeps its
// thin-wrapper charter (docs/dev/mainwindow-partials.md). Owns the five
// per-collection library tools that share the withActiveCollectionItems()
// skeleton — validate the active collection, resolve its uuid, null-guard the
// database manager, then run the tool's dialog flow:
//   - showCollectionHealthInteractive()   (Tools → Collection Health)
//   - showVariantGroupingInteractive()    (Tools → Duplicates and Variants)
//   - bulkEditInteractive()               (Tools → Bulk Edit, including the
//     BulkEditService thread-pool pipeline behind its progress dialog)
//   - reviewMissingMetadataInteractive()  (Tools → Review Missing Metadata)
//   - artworkWizardInteractive()          (Tools → Artwork Wizard)
//
// MainWindow keeps one-line delegations so the menu / palette callbacks and
// its header surface stay unchanged; every dependency is borrowed through
// closure context fields (same shape as ScraperController / DatAuditController).

#include <functional>
#include <QObject>
#include <QString>

#include "dialogrunners.h"

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

class IDatabaseManager;
class InteractionManager;
class NavigationManager;
struct CollectionConfig;

template <typename T> class QList;

struct LibraryToolsControllerContext {
  /// Parent widget for the tool dialogs + message boxes — the live MainWindow.
  std::function<QWidget *()> getParentWindow;

  /// References to MainWindow's mutable state, borrowed via getters like the
  /// other controller contexts — the lists stay on MainWindow because many
  /// other code paths read them.
  std::function<QList<CollectionConfig> *()> getCollections;
  std::function<int()> getCurrentCollectionIndex;

  /// Manager accessors: item enumeration + persistence (database), the shared
  /// post-tool soft reload (navigation), and the variant dialog's launch /
  /// metadata-edit handlers (interaction).
  std::function<IDatabaseManager *()> getDatabaseManager;
  std::function<NavigationManager *()> getNavigationManager;
  std::function<InteractionManager *()> getInteractionManager;

  /// Select an item in the main view without launching it — the variant
  /// dialog's select handler. Kept a closure because navigateToItem() needs
  /// MainWindow's collection-switch + async visual-index plumbing.
  std::function<void(const QString &filePath)> navigateToItem;

  /// Owner-supplied message-box runners for the shared active-collection
  /// guard prompts. Unset runners fall back to direct QMessageBox
  /// construction (parented on getParentWindow()), so production behavior
  /// without wiring is unchanged; headless tests supply stubs and never see
  /// a modal.
  DialogRunners dialogs;
};

class LibraryToolsController : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(LibraryToolsController)
public:
  explicit LibraryToolsController(QObject *parent = nullptr);
  ~LibraryToolsController() override;

  void setContext(const LibraryToolsControllerContext &context);

public slots:
  /// Collection-health dashboard: run the CollectionHealth analyzer over the
  /// active collection's item list + launcher config and pop a read-only
  /// summary dialog.
  void showCollectionHealthInteractive();

  /// Same-basename variant detector + inspector dialog across the active
  /// collection's items table. Falls back to an informational toast when no
  /// groups are detected.
  void showVariantGroupingInteractive();

  /// Bulk-edit dialog scoped to all items in the active collection. On
  /// confirm, runs BulkEditService::run on the thread pool (own SQLite
  /// connection; load + applyAction + single-transaction writes) behind a
  /// cancellable KartProgressDialog, then invalidates the touched
  /// metadata-cache rows and refreshes the view when the watcher completes.
  void bulkEditInteractive();

  /// Missing-metadata review queue: items lacking title / description /
  /// genre / artwork are presented one at a time with Edit / Skip / Close
  /// controls.
  void reviewMissingMetadataInteractive();

  /// Artwork-assignment wizard for items with no artwork match on disk. Each
  /// item shows a ranked list of candidate images from the collection's
  /// artwork directory; the pick is persisted as a per-item override.
  void artworkWizardInteractive();

private:
  /// Shared skeleton of the tool flows: validates the current collection
  /// index, resolves its uuid, null-guards the database manager, and hands
  /// the config + uuid + db to @p fn (which loads the item rows and drives
  /// its own dialog). Shows @p openMessage when no collection is active, and
  /// a standard identity-resolution warning under @p title on uuid failure.
  void withActiveCollectionItems(
      const QString &title, const QString &openMessage,
      const std::function<void(const CollectionConfig &cfg, const QString &uuid,
                               IDatabaseManager *db)> &fn);

  /// Shared post-tool epilogue: soft-reload the active collection so edits
  /// (metadata, artwork, state flags) surface in the grid + sidebar without
  /// a collection switch. No-op without a navigation manager.
  void reloadActiveCollection();

  /// Dialog / message-box parent — the live MainWindow, or null pre-wiring.
  [[nodiscard]] QWidget *parentWindow() const;

  /// Guard prompts routed through the ctx runners, falling back to the stock
  /// Qt modal when a runner is unset.
  void showInfo(const QString &title, const QString &text);
  void showWarning(const QString &title, const QString &text);

  LibraryToolsControllerContext m_ctx;
};

#endif // LIBRARYTOOLSCONTROLLER_H
