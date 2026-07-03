// Per-collection library tool flows, moved from mainwindow_dialogs.cpp — see
// the header for the charter. The flow bodies are the same code that lived on
// MainWindow; only the dependency access changed (member reads became ctx
// closure calls) and the shared safeReloadCollection epilogue collapsed into
// reloadActiveCollection().

#include "librarytoolscontroller.h"

#include <atomic>
#include <memory>

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>

#include "artworkcandidates.h"
#include "artworkwizarddialog.h"
#include "bulkedit.h"
#include "bulkeditdialog.h"
#include "bulkeditservice.h"
#include "collection/collectionconfig.h"
#include "collection/typehelpers.h"
#include "collection/validationhelpers.h"
#include "collectionhealth.h"
#include "collectionhealthdialog.h"
#include "idatabasemanager.h"
#include "interactionmanager.h"
#include "itemartwork.h"
#include "itemmetadataactioncontroller.h"
#include "kartprogressdialog.h"
#include "launchmanager.h"
#include "metadataqueue.h"
#include "metadatareviewdialog.h"
#include "navigationmanager.h"
#include "pathutils.h"
#include "variantgrouping.h"
#include "variantgroupingdialog.h"

LibraryToolsController::LibraryToolsController(QObject *parent) : QObject(parent) {}

LibraryToolsController::~LibraryToolsController() = default;

void LibraryToolsController::setContext(const LibraryToolsControllerContext &context) {
  m_ctx = context;
}

QWidget *LibraryToolsController::parentWindow() const {
  return m_ctx.getParentWindow ? m_ctx.getParentWindow() : nullptr;
}

void LibraryToolsController::showInfo(const QString &title, const QString &text) {
  if (m_ctx.dialogs.info) {
    m_ctx.dialogs.info(title, text);
    return;
  }
  QMessageBox::information(parentWindow(), title, text);
}

void LibraryToolsController::showWarning(const QString &title, const QString &text) {
  if (m_ctx.dialogs.warn) {
    m_ctx.dialogs.warn(title, text);
    return;
  }
  QMessageBox::warning(parentWindow(), title, text);
}

void LibraryToolsController::reloadActiveCollection() {
  NavigationManager *nav = m_ctx.getNavigationManager ? m_ctx.getNavigationManager() : nullptr;
  if (nav && m_ctx.getCurrentCollectionIndex) {
    nav->safeReloadCollection(m_ctx.getCurrentCollectionIndex());
  }
}

void LibraryToolsController::withActiveCollectionItems(
    const QString &title, const QString &openMessage,
    const std::function<void(const CollectionConfig &, const QString &, IDatabaseManager *)> &fn) {
  QList<CollectionConfig> *collections = m_ctx.getCollections ? m_ctx.getCollections() : nullptr;
  const int index = m_ctx.getCurrentCollectionIndex ? m_ctx.getCurrentCollectionIndex() : -1;
  if (!collections || index < 0 || index >= collections->size()) {
    showInfo(title, openMessage);
    return;
  }
  const CollectionConfig &cfg = collections->at(index);
  // Resolve the uuid the same way every other per-item code path does so
  // the item enumeration finds the rows the rest of the app sees.
  const QString uuid = CollectionUtils::computeCollectionUuid(cfg);
  if (uuid.isEmpty()) {
    showWarning(title, tr("Could not resolve this collection's identity. "
                          "Check the media directory in settings."));
    return;
  }
  IDatabaseManager *db = m_ctx.getDatabaseManager ? m_ctx.getDatabaseManager() : nullptr;
  if (!db) {
    return;
  }
  fn(cfg, uuid, db);
}

void LibraryToolsController::showCollectionHealthInteractive() {
  withActiveCollectionItems(
      tr("Collection health"), tr("Open a collection before running the health audit."),
      [this](const CollectionConfig &cfg, const QString &uuid, IDatabaseManager *db) {
        const auto rows = db->loadAllItemPathsForCollection(uuid);

        // Convert to CollectionHealth::ItemPath wire shape. Decoupling the
        // analyzer's input from IDatabaseManager keeps the analyzer
        // unit-testable without a real DB.
        QList<CollectionHealth::ItemPath> healthItems;
        healthItems.reserve(rows.size());
        for (const IDatabaseManager::ItemPathRow &row : rows) {
          healthItems.append({row.path, row.artworkPath});
        }

        // Launcher validator: defer to LaunchManager's existing path resolver
        // so what counts as "found" matches what the launch pipeline would
        // accept at run time.
        auto validator = [](const QString &path) {
          return LaunchManager::validateLauncherPath(path).isOk();
        };
        const CollectionHealth::Report report =
            CollectionHealth::analyze(cfg, healthItems, validator);

        CollectionHealthDialog dialog(parentWindow());
        dialog.setReport(cfg.name, report);
        dialog.exec();
      });
}

void LibraryToolsController::showVariantGroupingInteractive() {
  withActiveCollectionItems(
      tr("Duplicates and variants"), tr("Open a collection before scanning for variants."),
      [this](const CollectionConfig &cfg, const QString &uuid, IDatabaseManager *db) {
        const auto rows = db->loadAllItemPathsForCollection(uuid);
        QStringList paths;
        paths.reserve(rows.size());
        for (const IDatabaseManager::ItemPathRow &row : rows) {
          paths.append(row.path);
        }
        const auto groups = VariantGrouping::groupByBaseName(paths);
        if (groups.isEmpty()) {
          QMessageBox::information(parentWindow(), tr("Duplicates and variants"),
                                   tr("No same-name variants were detected in '%1' — every item "
                                      "has a unique basename.")
                                       .arg(cfg.name));
          return;
        }

        VariantGroupingDialog dialog(groups, parentWindow());
        dialog.setLaunchHandler([this](const QString &path) {
          if (path.isEmpty()) return;
          // Defer to InteractionManager so launcher selection, kart-archive
          // unpacking, and play-count bumps stay in one place. Resolve the
          // owning collection via the DB instead of trusting the currently-viewed
          // one — a user can navigate elsewhere while the dialog is up.
          IDatabaseManager *handlerDb =
              m_ctx.getDatabaseManager ? m_ctx.getDatabaseManager() : nullptr;
          InteractionManager *im =
              m_ctx.getInteractionManager ? m_ctx.getInteractionManager() : nullptr;
          if (!handlerDb || !im) return;
          const int owningIndex = handlerDb->getCollectionIndexForFile(path);
          if (!CollectionUtils::isValidIndex(owningIndex, m_ctx.getCollections())) return;
          im->launchItemWithCollection(path, owningIndex);
        });
        dialog.setSelectHandler([this](const QString &path) {
          if (m_ctx.navigateToItem) {
            m_ctx.navigateToItem(path);
          }
        });
        dialog.exec();
      });
}

void LibraryToolsController::bulkEditInteractive() {
  withActiveCollectionItems(
      tr("Bulk edit"), tr("Open a collection before bulk-editing items."),
      [this](const CollectionConfig &cfg, const QString &uuid, IDatabaseManager *db) {
        const auto rows = db->loadAllItemPathsForCollection(uuid);
        if (rows.isEmpty()) {
          QMessageBox::information(parentWindow(), tr("Bulk edit"),
                                   tr("This collection has no items to edit."));
          return;
        }

        BulkEditDialog dialog(parentWindow());
        dialog.setScope(cfg.name, rows.size());
        if (dialog.exec() != QDialog::Accepted) {
          return;
        }
        const auto choice = dialog.result();

        // Confirmation gate — describes the change in user-readable terms
        // before persistence. The dialog disables Apply when the input is
        // invalid (e.g. empty tag), but a final yes/no still surfaces here so
        // an accidental Enter on the picker isn't destructive.
        const QString actionDescription =
            BulkEdit::actionRequiresParameter(choice.action)
                ? tr("%1 \"%2\"").arg(BulkEdit::actionLabel(choice.action), choice.parameter)
                : BulkEdit::actionLabel(choice.action);
        const auto confirmation =
            QMessageBox::question(parentWindow(), tr("Bulk edit"),
                                  tr("%1 across %2 item(s) in \"%3\"?")
                                      .arg(actionDescription)
                                      .arg(rows.size())
                                      .arg(cfg.name),
                                  QMessageBox::Apply | QMessageBox::Cancel, QMessageBox::Cancel);
        if (confirmation != QMessageBox::Apply) {
          return;
        }

        // The metadata batch-load + per-row saves used to run inline here and
        // froze the window for seconds at 10k+ items. The pipeline now runs on
        // the thread pool against its own SQLite connection (QSqlDatabase is
        // thread-affine — see BulkEditService's header), with the row writes
        // batched into one transaction. The GUI thread keeps only the dialogs.
        const QString dbPath = db->databaseFilePath();
        if (dbPath.isEmpty()) {
          QMessageBox::warning(parentWindow(), tr("Bulk edit"),
                               tr("The database is not available; no changes were applied."));
          return;
        }

        QStringList paths;
        paths.reserve(rows.size());
        for (const IDatabaseManager::ItemPathRow &row : rows) {
          paths.append(row.path);
        }

        // Progress surface. Application-modal but shown non-blocking (show(),
        // not exec()) so the event loop keeps painting while the worker runs;
        // modality also stops a second bulk edit from starting mid-run. Both
        // the Cancel button and Esc flip the shared token; the worker rolls
        // back its transaction when it observes the flip (all-or-nothing).
        auto *progressDialog = new KartProgressDialog(tr("Bulk edit"), parentWindow());
        progressDialog->setAttribute(Qt::WA_DeleteOnClose);
        auto cancelToken = std::make_shared<std::atomic_bool>(false);
        const auto requestCancel = [cancelToken]() {
          cancelToken->store(true, std::memory_order_release);
        };
        connect(progressDialog, &KartProgressDialog::cancelRequested, this, requestCancel);
        connect(progressDialog, &QDialog::rejected, this, requestCancel);

        // Worker → GUI progress hop. The worker only copies the QPointer
        // value; it is resolved ON THE GUI THREAD inside the queued lambda
        // (same rationale as DatabaseManager::queueWorkerWrite's completion
        // hop — resolving on the worker would be a check-then-deref race).
        QPointer<KartProgressDialog> progressGuard(progressDialog);
        auto onProgress = [progressGuard](int done, int total) {
          QMetaObject::invokeMethod(
              qApp,
              [progressGuard, done, total]() {
                if (!progressGuard) {
                  return;
                }
                progressGuard->setFraction(total > 0 ? static_cast<double>(done) / total : 1.0);
                progressGuard->setEntryName(tr("%1 of %2 item(s)").arg(done).arg(total));
              },
              Qt::QueuedConnection);
        };

        // Same QFutureWatcher shape as StatisticsDialog::refresh. The watcher
        // is parented + context-bound to this controller (whose lifetime
        // matches the window's), so on teardown mid-run the continuation
        // never fires; the worker holds only value captures (no owner member
        // crosses the thread boundary).
        auto *watcher = new QFutureWatcher<BulkEditService::Outcome>(this);
        const int totalRows = static_cast<int>(rows.size());
        connect(watcher, &QFutureWatcherBase::finished, this,
                [this, watcher, progressGuard, uuid, totalRows]() {
                  const BulkEditService::Outcome outcome = watcher->result();
                  watcher->deleteLater();
                  if (progressGuard) {
                    progressGuard->close(); // WA_DeleteOnClose frees it
                  }
                  // The worker wrote via its own connection, bypassing the main
                  // connection's per-item metadata cache — drop exactly the rows
                  // it committed (the contract invalidateMetadataCacheItem exists
                  // for; BatchScrapeRunner does the same after its write phase).
                  if (auto *dbNow =
                          m_ctx.getDatabaseManager ? m_ctx.getDatabaseManager() : nullptr) {
                    for (const QString &path : outcome.writtenPaths) {
                      dbNow->invalidateMetadataCacheItem(uuid, path);
                    }
                  }
                  // Soft reload so the new state (tags, flags, ratings) surfaces
                  // in the sidebar without a collection switch. The grid rendering
                  // isn't affected by these flags yet, but the details pane
                  // already reads them.
                  if (outcome.written > 0) {
                    reloadActiveCollection();
                  }
                  if (outcome.canceled) {
                    QMessageBox::information(parentWindow(), tr("Bulk edit"),
                                             tr("Cancelled — no changes were applied."));
                  } else if (!outcome.ok) {
                    QMessageBox::warning(
                        parentWindow(), tr("Bulk edit"),
                        tr("The bulk edit could not be applied. No changes were saved."));
                  } else {
                    QMessageBox::information(
                        parentWindow(), tr("Bulk edit"),
                        tr("Applied to %1 of %2 item(s).").arg(outcome.written).arg(totalRows));
                  }
                });
        watcher->setFuture(
            QtConcurrent::run([dbPath, uuid, action = choice.action, parameter = choice.parameter,
                               paths, cancelToken, onProgress]() {
              return BulkEditService::run(dbPath, uuid, action, parameter, paths, cancelToken,
                                          onProgress);
            }));
        progressDialog->show();
      });
}

void LibraryToolsController::reviewMissingMetadataInteractive() {
  withActiveCollectionItems(
      tr("Review missing metadata"), tr("Open a collection before running the review."),
      [this](const CollectionConfig &cfg, const QString &uuid, IDatabaseManager *db) {
        const auto rows = db->loadAllItemPathsForCollection(uuid);
        if (rows.isEmpty()) {
          QMessageBox::information(parentWindow(), tr("Review missing metadata"),
                                   tr("This collection has no items to review."));
          return;
        }
        QList<MetadataQueue::InputRow> inputs;
        inputs.reserve(rows.size());
        QStringList metadataPaths;
        metadataPaths.reserve(rows.size());
        for (const IDatabaseManager::ItemPathRow &row : rows) {
          MetadataQueue::InputRow input;
          input.filePath = row.path;
          input.hasArtworkOnDisk = !row.artworkPath.trimmed().isEmpty();
          input.itemName = QFileInfo(row.path).completeBaseName();
          inputs.append(input);
          metadataPaths.append(row.path);
        }
        // Prefetch the whole collection's metadata in a few batched WHERE-IN
        // queries instead of letting build() issue one point read per row —
        // that N+1 stalled the GUI thread for seconds on 10k-item collections.
        // The batch loader keys every requested path (missing rows come back
        // as empty stubs), so value() never manufactures an unkeyed default.
        const auto prefetched = db->loadItemMetadataBatch(uuid, metadataPaths);
        const auto entries = MetadataQueue::build(
            uuid, inputs, [&prefetched](const QString & /*uuid*/, const QString &path) {
              return prefetched.value(path);
            });
        if (entries.isEmpty()) {
          QMessageBox::information(
              parentWindow(), tr("Review missing metadata"),
              tr("Every item in \"%1\" already has the core metadata fields.").arg(cfg.name));
          return;
        }

        // Edit closure: open the existing per-item editor through the
        // interaction manager (it owns the closure that pops EditMetadataDialog
        // and persists the result). Returns true when something changed.
        auto onEdit = [this](const QString &filePath, const QString &itemName) {
          auto *im = m_ctx.getInteractionManager ? m_ctx.getInteractionManager() : nullptr;
          if (im && im->itemMetadataActions()) {
            // editItemMetadata is fire-and-forget; we can't tell from its
            // signature whether the user actually saved. Conservative: assume
            // an edit attempt counts and let reevaluate filter the queue.
            im->itemMetadataActions()->editItemMetadata(filePath, itemName);
            return true;
          }
          return false;
        };
        auto onReload = [db](const QString &u, const QString &p) {
          return db->loadItemMetadata(u, p);
        };

        MetadataReviewDialog dialog(parentWindow());
        dialog.setQueue(entries, std::move(onEdit), std::move(onReload));
        dialog.exec();

        // Refresh the sidebar so any edits made during the review are visible
        // without a separate collection switch.
        reloadActiveCollection();
      });
}

void LibraryToolsController::artworkWizardInteractive() {
  withActiveCollectionItems(
      tr("Assign missing artwork"), tr("Open a collection before running the wizard."),
      [this](const CollectionConfig &cfg, const QString &uuid, IDatabaseManager *db) {
        const QString artworkDir = PathUtils::validateAndExpandPath(cfg.artworkDirectory, cfg.name);
        if (artworkDir.trimmed().isEmpty()) {
          QMessageBox::information(parentWindow(), tr("Assign missing artwork"),
                                   tr("This collection has no artwork directory configured."));
          return;
        }

        // Queue = items where items.artwork_path is empty / NULL. Pull the
        // path list, filter, and build the wizard's per-entry record.
        const auto rows = db->loadAllItemPathsForCollection(uuid);
        QList<ArtworkWizardDialog::Entry> queue;
        for (const IDatabaseManager::ItemPathRow &row : rows) {
          if (!row.artworkPath.trimmed().isEmpty()) {
            continue;
          }
          ArtworkWizardDialog::Entry entry;
          entry.filePath = row.path;
          entry.collectionUuid = uuid;
          entry.itemName = QFileInfo(row.path).completeBaseName();
          queue.append(entry);
        }
        if (queue.isEmpty()) {
          QMessageBox::information(parentWindow(), tr("Assign missing artwork"),
                                   tr("Every item in \"%1\" already has artwork.").arg(cfg.name));
          return;
        }

        // Snapshot the artwork directory listing once. The wizard ranks
        // candidates per item but re-walking the directory for every entry
        // would be wasteful — these directories typically contain hundreds of
        // files and the listing is stable for the wizard's lifetime.
        QStringList directoryFiles;
        {
          QDir dir(artworkDir);
          const QStringList entries = dir.entryList(QDir::Files | QDir::NoDotAndDotDot);
          directoryFiles.reserve(entries.size());
          for (const QString &name : entries) {
            directoryFiles.append(dir.absoluteFilePath(name));
          }
        }

        auto candidatesFor = [directoryFiles](const ArtworkWizardDialog::Entry &entry) {
          return ArtworkCandidates::rank(entry.itemName, directoryFiles);
        };
        auto onPick = [db](const ArtworkWizardDialog::Entry &entry, const QString &chosenFilePath) {
          ItemArtworkStore::ItemArtwork row;
          row.collectionUuid = entry.collectionUuid;
          row.path = entry.filePath;
          // "Front" is the cross-provider primary-cover slot used by the
          // sidebar gallery and tile renderer; saving here makes the picked
          // image surface as the item's main artwork immediately.
          row.artworkType = ItemArtworkStore::StandardTypes::Front;
          row.manualPath = chosenFilePath;
          return db->saveItemArtwork(row);
        };

        ArtworkWizardDialog dialog(parentWindow());
        dialog.setQueue(queue, std::move(candidatesFor), std::move(onPick));
        dialog.exec();

        // safeReloadCollection so newly-assigned artwork surfaces in the grid
        // and sidebar without requiring a collection switch.
        reloadActiveCollection();
      });
}
