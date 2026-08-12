#include "launcherimportcontroller.h"

#include <algorithm>

#include <QApplication>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QtConcurrent/QtConcurrentRun>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QUrl>

#include "artworkutils.h"
#include "batchscraperunner.h"
#include "collection/collectionconfig.h"
#include "collection/typehelpers.h"
#include "extensionutils.h"
#include "flathubprovider.h"
#include "httpclient.h"
#include "idatabasemanager.h"
#include "launcherimportdialog.h"
#include "pathutils.h"
#include "steamstoreprovider.h"

namespace {
Q_LOGGING_CATEGORY(lcLauncherImport, "kartend.launcherimport")

void logSyncErrors(const QString &sourceId, const LauncherImportService::SyncResult &result) {
  for (const QString &error : result.errors) {
    qCWarning(lcLauncherImport).nospace() << "sync '" << sourceId << "': " << error;
  }
}
} // namespace

LauncherImportController::LauncherImportController(QObject *parent) : QObject(parent) {
  connect(&m_syncWatcher, &QFutureWatcherBase::finished, this,
          &LauncherImportController::onSyncFinished);
}

// Out-of-line so the QFutureWatcher member's destructor instantiates here. A
// still-running worker keeps going on the global pool after teardown — it
// only touches its snapshotted job list (stub/artwork dirs), never this
// object or the live collection list, so no wait is needed.
LauncherImportController::~LauncherImportController() = default;

void LauncherImportController::setContext(const LauncherImportControllerContext &context) {
  m_ctx = context;
}

void LauncherImportController::runImportDialog() {
  QWidget *parent = m_ctx.getParentWindow ? m_ctx.getParentWindow() : nullptr;
  QList<CollectionConfig> *collections = m_ctx.getCollections ? m_ctx.getCollections() : nullptr;
  if (!collections || !m_ctx.appendCollectionAndPersist) {
    return;
  }

  // Matched on (source, KEY): a source that yields several collections has one
  // per slice, so "is this already imported?" is per-slice too. With an empty
  // key this is exactly the old behaviour (Kartend-ilkne).
  const auto indexForSlice = [collections](const QString &sourceId, const QString &sourceKey) {
    for (int i = 0; i < collections->size(); ++i) {
      if (collections->at(i).importSource == sourceId &&
          collections->at(i).importSourceKey == sourceKey) {
        return i;
      }
    }
    return -1;
  };
  const auto anyImportedForSource = [collections](const QString &sourceId) {
    return std::ranges::any_of(*collections, [&sourceId](const CollectionConfig &c) {
      return c.importSource == sourceId;
    });
  };

  const QList<LauncherImportService::SourceInfo> sources = LauncherImportService::detectSources();
  QList<LauncherImportSourceRow> rows;
  rows.reserve(sources.size());
  for (const LauncherImportService::SourceInfo &source : sources) {
    LauncherImportSourceRow row;
    row.id = source.id;
    row.displayName = source.displayName;
    row.available = source.available;
    row.gameCount = source.gameCount;
    row.ownedGameCount = source.ownedGameCount;
    row.recognizedGameCount = source.recognizedGameCount;
    row.alreadyImported = anyImportedForSource(source.id);
    rows.append(row);
  }

  // Offer the existing (non-playlist) collections as possible parents so an
  // imported collection can nest into the user's hierarchy instead of
  // landing at the root — same option set and uuid→index resolution as
  // createCollectionForDat.
  QList<QPair<QString, QString>> parentOptions;
  QHash<QString, int> uuidToIndex;
  for (int i = 0; i < collections->size(); ++i) {
    const CollectionConfig &existing = collections->at(i);
    if (existing.isPlaylist) {
      continue;
    }
    const QString uuid = CollectionUtils::computeCollectionUuid(existing);
    parentOptions.append({existing.name, uuid});
    uuidToIndex.insert(uuid, i);
  }

  LauncherImportDialog dialog(parent);
  dialog.setSources(rows);
  dialog.setParentCollectionOptions(parentOptions);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  const QStringList selected = dialog.selectedSourceIds();
  if (selected.isEmpty()) {
    return;
  }
  const int parentIndex = uuidToIndex.value(dialog.parentCollectionUuid(), -1);
  // ui-local choice → data-layer scope. Only Steam varies with it; the other
  // sources ignore the argument.
  const auto scope = [&dialog]() {
    switch (dialog.selectedScope()) {
    case LauncherImportScopeChoice::Owned:
      return LauncherImportService::ImportScope::Owned;
    case LauncherImportScopeChoice::AllRecognized:
      return LauncherImportService::ImportScope::AllRecognized;
    case LauncherImportScopeChoice::InstalledOnly:
      break;
    }
    return LauncherImportService::ImportScope::Installed;
  }();

  const auto nameTaken = [collections](const QString &name) {
    return std::ranges::any_of(*collections, [&name](const CollectionConfig &c) {
      return c.name.compare(name, Qt::CaseInsensitive) == 0;
    });
  };

  bool artworkTouched = false;
  QStringList summary;
  // Inline (not pooled) on purpose: the user just clicked Import and the
  // follow-up message box needs the counts; a few hundred stub writes plus
  // artwork copies finish in low seconds. The wait cursor marks the pause.
  QApplication::setOverrideCursor(Qt::WaitCursor);
  for (const QString &sourceId : selected) {
    // A source normally means one collection; ES-DE means one PER SYSTEM
    // (Kartend-ilkne). Expanding to a list of slice keys here — with a single
    // empty key for every other source — keeps one code path for both, so the
    // re-sync, naming, parenting and metadata passes below are written once.
    QStringList sliceKeys;
    for (const LauncherImportService::SourceSlice &slice :
         LauncherImportService::sourceSlices(sourceId)) {
      sliceKeys.append(slice.key);
    }
    if (sliceKeys.isEmpty()) {
      sliceKeys.append(QString());
    }

    for (const QString &sourceKey : sliceKeys) {
      const int existingIndex = indexForSlice(sourceId, sourceKey);
      if (existingIndex >= 0) {
        // Source already has a collection — selecting it means "re-sync now",
        // at the scope just chosen. Record the scope on the collection before
        // syncing: the sync prunes stubs the new listing doesn't carry, so the
        // persisted value has to agree with what is now on disk or the next
        // startup sync would widen or prune it right back (Kartend-el5st).
        const QString scopeText = LauncherImportService::scopeToString(scope);
        if ((*collections)[existingIndex].importScope != scopeText) {
          (*collections)[existingIndex].importScope = scopeText;
          if (m_ctx.persistCollections) {
            m_ctx.persistCollections();
          }
        }
        const CollectionConfig &existing = collections->at(existingIndex);
        const LauncherImportService::SyncResult result = LauncherImportService::syncSource(
            sourceId,
            PathUtils::expandPathWithoutExistenceCheck(existing.mediaDirectory, existing.name),
            PathUtils::expandPathWithoutExistenceCheck(existing.artworkDirectory, existing.name),
            scope, sourceKey);
        logSyncErrors(sourceId, result);
        artworkTouched = artworkTouched || result.artworkCopied > 0;
        const int metadataRows = applyMetadataForCollection(existing, result.syncedStubs);
        enrichFromStore(existing, result.syncedStubs);
        fetchRemoteCovers(existing, result.syncedStubs);
        if (result.changed() && m_ctx.refreshCollection) {
          m_ctx.refreshCollection(existingIndex);
        }
        summary.append(tr("%1: re-synced — %2 added or updated, %3 removed, %n game(s) total.",
                          nullptr, result.totalPresent())
                           .arg(existing.name)
                           .arg(result.written)
                           .arg(result.removed));
        if (metadataRows > 0) {
          summary.append(tr("%1: Steam metadata filled for %n item(s).", nullptr, metadataRows)
                             .arg(existing.name));
        }
        continue;
      }

      CollectionConfig config =
          LauncherImportService::makeCollectionConfig(sourceId, scope, sourceKey);
      // Unique display name: several hierarchy paths key collections by name,
      // and the uuid itself hashes name + media dir.
      const QString baseName = config.name;
      for (int n = 2; nameTaken(config.name); ++n) {
        config.name = baseName + QStringLiteral(" (%1)").arg(n);
      }
      // Apply the chosen parent, inheriting the layout/sidebar fields a
      // subcollection takes from its parent — the same set
      // SettingsDialog::addCollection and createCollectionForDat copy, so an
      // imported subcollection looks consistent with its siblings.
      if (parentIndex >= 0 && parentIndex < collections->size()) {
        const CollectionConfig &parentConfig = collections->at(parentIndex);
        config.parentCollectionIndex = parentIndex;
        config.isSubcollection = true;
        config.gridLayout = parentConfig.gridLayout;
        config.sidebar.sidebarMode = parentConfig.sidebar.sidebarMode;
        config.viewType = parentConfig.viewType;
        config.showAllSubcollectionItems = parentConfig.showAllSubcollectionItems;
        config.horizontalAlignment = parentConfig.horizontalAlignment;
        config.hideTitles = parentConfig.hideTitles;
        config.hideSubcollectionTitles = parentConfig.hideSubcollectionTitles;
      }
      // Sync before persisting so the collection's very first scan already
      // sees the stubs (and the fill-missing artwork).
      const LauncherImportService::SyncResult result = LauncherImportService::syncSource(
          sourceId, config.mediaDirectory, config.artworkDirectory, scope, sourceKey);
      logSyncErrors(sourceId, result);
      artworkTouched = artworkTouched || result.artworkCopied > 0;
      m_ctx.appendCollectionAndPersist(config, /*navigate=*/false);
      // Metadata after the sync (the stub dir exists now, so the canonical
      // uuid — which validates the media dir — resolves correctly).
      const int metadataRows = applyMetadataForCollection(config, result.syncedStubs);
      // …then the store pass for what only the web has (descriptions, media).
      // Async: the dialog closes immediately and the status bar reports.
      enrichFromStore(config, result.syncedStubs);
      fetchRemoteCovers(config, result.syncedStubs);
      // totalPresent, not written: a re-import over a surviving stub folder
      // writes nothing but the collection still gets every game.
      summary.append(
          tr("%1: %n game(s) imported.", nullptr, result.totalPresent()).arg(config.name));
      if (metadataRows > 0) {
        summary.append(tr("%1: Steam metadata filled for %n item(s).", nullptr, metadataRows)
                           .arg(config.name));
      }
    }
  }
  QApplication::restoreOverrideCursor();

  if (artworkTouched) {
    // The directory-listing cache may hold negative entries for the artwork
    // folders that were just filled.
    ArtworkUtils::clearDirectoryCache();
  }
  QMessageBox::information(parent, tr("Import from Launcher"), summary.join(QLatin1Char('\n')));
}

auto LauncherImportController::applyMetadataForCollection(
    const CollectionConfig &config, const QList<LauncherImportService::SyncedStub> &stubs) -> int {
  IDatabaseManager *db = m_ctx.getDatabaseManager ? m_ctx.getDatabaseManager() : nullptr;
  if (!db || stubs.isEmpty()) {
    return 0;
  }
  const QString uuid = CollectionUtils::computeCollectionUuid(config);
  const LauncherImportService::MetadataApplyResult metadata =
      LauncherImportService::applySteamMetadata(db->databaseFilePath(), uuid, stubs);
  for (const QString &error : metadata.errors) {
    qCWarning(lcLauncherImport).nospace() << "metadata '" << config.name << "': " << error;
  }
  // The writer used its own connection, bypassing the main-thread LRU.
  for (const QString &path : metadata.writtenPaths) {
    db->invalidateMetadataCacheItem(uuid, path);
  }
  return metadata.rowsWritten;
}

void LauncherImportController::enrichFromStore(
    const CollectionConfig &config, const QList<LauncherImportService::SyncedStub> &stubs) {
  // Steam stubs enrich from the storefront API; Flatpak stubs from Flathub's
  // AppStream API (Kartend-2bzbu) — these apps are on neither ScreenScraper
  // nor the Steam store, so without this pass their details pane shows File
  // Information and nothing else. Lutris has no equivalent store endpoint.
  const bool storeBacked =
      config.importSource == QLatin1String(LauncherImportService::kSourceSteam) ||
      config.importSource == QLatin1String(LauncherImportService::kSourceFlatpak);
  if (!storeBacked || stubs.isEmpty()) {
    return;
  }
  PendingEnrichment pending;
  pending.sourceId = config.importSource;
  pending.collectionName = config.name;
  pending.collectionUuid = CollectionUtils::computeCollectionUuid(config);
  // The sync just created this directory, so the existence-checking
  // expander resolves it (an empty artwork dir would make every media write
  // a silent no-op).
  pending.artworkDir = PathUtils::validateAndExpandPath(config.artworkDirectory, config.name);
  // Only items the store pass hasn't landed yet. Two reasons: a re-sync must
  // be able to resume an enrichment that was cut short (the whole point of
  // calling this from the sync path), and re-importing a large collection
  // shouldn't re-request hundreds of pages whose text is already stored.
  // FillMissing would discard the duplicate writes anyway — this saves the
  // requests, which is what the runner is actually rate-limited on.
  IDatabaseManager *db = m_ctx.getDatabaseManager ? m_ctx.getDatabaseManager() : nullptr;
  pending.paths = LauncherImportService::stubsMissingDescription(
      db != nullptr ? db->databaseFilePath() : QString(), pending.collectionUuid, stubs);
  if (pending.paths.isEmpty()) {
    return; // every game already has its store text
  }
  if (pending.artworkDir.isEmpty() || pending.collectionUuid.isEmpty()) {
    qCWarning(lcLauncherImport) << "skipping store enrichment: unresolved artwork dir for"
                                << config.name;
    return;
  }
  m_enrichQueue.append(pending);
  startNextEnrichment();
}

namespace {
// A cover is a JPEG/PNG of a few hundred KB; anything wildly larger is not
// artwork and should not be written into the user's library. The cap is the
// HttpClient's own guard, applied before the body is buffered.
constexpr qint64 kMaxCoverBytes = 12LL * 1024 * 1024;
} // namespace

void LauncherImportController::fetchRemoteCovers(
    const CollectionConfig &config, const QList<LauncherImportService::SyncedStub> &stubs) {
  // syncEntries already applied the fill-missing rule, so anything carrying a
  // pendingCoverUrl genuinely has an empty cover slot. Re-deciding that here
  // would duplicate the rule in a second place.
  QList<LauncherImportService::SyncedStub> wanted;
  for (const LauncherImportService::SyncedStub &stub : stubs) {
    if (!stub.pendingCoverUrl.isEmpty()) {
      wanted.append(stub);
    }
  }
  if (wanted.isEmpty()) {
    return;
  }
  const QString artworkDir = PathUtils::validateAndExpandPath(config.artworkDirectory, config.name);
  if (artworkDir.isEmpty()) {
    qCWarning(lcLauncherImport) << "skipping cover fetch: unresolved artwork dir for"
                                << config.name;
    return;
  }
  const QString frontDir = artworkDir + QStringLiteral("/front");
  if (!QDir().mkpath(frontDir)) {
    qCWarning(lcLauncherImport) << "skipping cover fetch: cannot create" << frontDir;
    return;
  }

  if (m_ctx.showStatusMessage) {
    m_ctx.showStatusMessage(
        tr("%1: fetching cover art for %n game(s)…", nullptr, static_cast<int>(wanted.size()))
            .arg(config.name));
  }

  // Shared counter so the completion line reports once, after the last reply,
  // without keeping any per-request state on the controller. Captured by
  // value into each callback; QPointer guards the controller outliving them.
  auto remaining = std::make_shared<int>(static_cast<int>(wanted.size()));
  auto written = std::make_shared<int>(0);
  QPointer<LauncherImportController> self(this);
  const QString collectionName = config.name;
  const QString sourceId = config.importSource;

  for (const LauncherImportService::SyncedStub &stub : wanted) {
    const QString baseName = QFileInfo(stub.path).completeBaseName();
    const QUrl url(stub.pendingCoverUrl);
    if (!url.isValid() || url.scheme().startsWith(QLatin1String("http")) == false) {
      // A launcher database is user-writable, so treat its URLs as data:
      // anything that is not plain http(s) is dropped rather than handed to
      // the network stack.
      qCWarning(lcLauncherImport) << "cover url rejected for" << baseName << url.scheme();
      --*remaining;
      continue;
    }
    Scraper::HttpClient::instance()->get(
        url, {},
        [self, frontDir, baseName, url, remaining, written, collectionName,
         sourceId](ErrorUtils::Result<QByteArray> response) {
          --*remaining;
          if (!response.isError() && !response.value().isEmpty()) {
            // Extension from the bytes, not the URL: CDNs serve images from
            // extension-less paths, and a name that lies about its content is
            // the Kartend-aiws7 defect. Shared with the scraper's writer.
            const QString extension =
                ExtensionUtils::imageExtensionForBytes(url.path(), response.value());
            QFile file(frontDir + QLatin1Char('/') + baseName + QLatin1Char('.') + extension);
            if (file.open(QIODevice::WriteOnly) &&
                file.write(response.value()) == response.value().size()) {
              ++*written;
            } else {
              qCWarning(lcLauncherImport) << "cover write failed for" << baseName;
            }
          } else if (response.isError()) {
            qCWarning(lcLauncherImport)
                << "cover fetch failed for" << baseName << response.error().message;
          }
          if (*remaining > 0 || self.isNull()) {
            return;
          }
          // Last reply in: refresh the grid once rather than per cover, and
          // only when something actually landed.
          if (*written > 0) {
            // The directory-listing cache holds negative entries for the
            // artwork folder that was empty a moment ago.
            ArtworkUtils::clearDirectoryCache();
            if (self->m_ctx.showStatusMessage) {
              self->m_ctx.showStatusMessage(
                  tr("%1: cover art added for %n game(s).", nullptr, *written).arg(collectionName));
            }
            // indexForSource is a local lambda in runImportDialog, and the
            // list may have been re-ordered while the fetch was in flight, so
            // resolve by source id at delivery time.
            QList<CollectionConfig> *collections =
                self->m_ctx.getCollections ? self->m_ctx.getCollections() : nullptr;
            if (collections && self->m_ctx.refreshCollection) {
              for (int i = 0; i < collections->size(); ++i) {
                if (collections->at(i).importSource == sourceId) {
                  self->m_ctx.refreshCollection(i);
                  break;
                }
              }
            }
          }
        },
        /*maxResponseBytes=*/kMaxCoverBytes,
        // Covers are images; a launcher database pointing at anything else is
        // refused before a byte is written.
        /*expectedContentTypePrefix=*/QStringLiteral("image/"));
  }
}

void LauncherImportController::startNextEnrichment() {
  if (m_enrichRunner != nullptr || m_enrichQueue.isEmpty()) {
    return;
  }
  const ApplicationContext *ctx =
      m_ctx.getApplicationContext ? m_ctx.getApplicationContext() : nullptr;
  if (ctx == nullptr) {
    m_enrichQueue.clear();
    return;
  }
  const PendingEnrichment job = m_enrichQueue.takeFirst();
  const bool isSteam = job.sourceId == QLatin1String(LauncherImportService::kSourceSteam);
  const QString storeName = isSteam ? QStringLiteral("Steam") : QStringLiteral("Flathub");

  // FillMissing so a re-import or re-sync only fetches what isn't already
  // there — the local appinfo pass has usually filled the text fields, and
  // this run adds the description plus the media the local caches lack.
  // Flathub is metadata-only (Kartend-2bzbu): the import copies each app's
  // exported icon as the cover, and the provider declares no MediaFetch,
  // so the runner is told not to chase a primary cover either.
  std::shared_ptr<MetadataLookupProvider> provider;
  if (isSteam) {
    provider = std::make_shared<SteamStoreProvider>();
  } else {
    provider = std::make_shared<FlathubProvider>();
  }
  m_enrichRunner = new Scraper::BatchScrapeRunner(
      ctx, std::move(provider), job.collectionUuid, job.paths, job.artworkDir,
      /*fetchPrimaryCover=*/isSteam, Scraper::RescrapeMode::FillMissing, /*itemConcurrency=*/2,
      /*skipRecentDays=*/0, this);
  if (isSteam) {
    // The media set an imported Steam collection actually wants; the dialog's
    // defaults (cover + metadata only) are aimed at ROM scraping.
    m_enrichRunner->setMediaTypeFilter({QStringLiteral("front"), QStringLiteral("screenshot"),
                                        QStringLiteral("background"), QStringLiteral("video")});
  }

  // Say the size up front. The store APIs are paced at roughly a request or
  // two per second, so a wide-scope collection takes minutes — without
  // a number the user cannot tell a working fetch from a broken one, and the
  // honest reading of a silent status bar is "the import lost my data".
  // Seeded to 0, not -1: the first items start before any finishes, so
  // progress fires with done == 0 straight away. Treating 0 as "already
  // reported" lets the sentence below stand until something genuinely
  // completes, instead of being replaced by "0 of 123" within milliseconds.
  m_enrichReportedDone = 0;
  if (m_ctx.showStatusMessage) {
    m_ctx.showStatusMessage(tr("%1: fetching details for %n game(s) from %2 — "
                               "this can take a few minutes…",
                               nullptr, static_cast<int>(job.paths.size()))
                                .arg(job.collectionName, storeName));
  }
  // Per-item ticks. These do double duty: they show progress, and because the
  // status bar clears a message after ten seconds they are what keeps any
  // message on screen at all for the length of the run.
  connect(
      m_enrichRunner, &Scraper::BatchScrapeRunner::progress, this,
      [this, name = job.collectionName, storeName](int done, int total, const QString &) {
        // itemConcurrency > 1 means the same `done` arrives once per item
        // that starts; only speak when the number actually moves.
        if (done == m_enrichReportedDone) {
          return;
        }
        m_enrichReportedDone = done;
        if (m_ctx.showStatusMessage) {
          m_ctx.showStatusMessage(
              tr("%1: fetching %2 details… %3 of %4").arg(name, storeName).arg(done).arg(total));
        }
      });
  connect(m_enrichRunner, &Scraper::BatchScrapeRunner::finished, this,
          [this, name = job.collectionName,
           storeName](const Scraper::BatchScrapeRunner::Summary &summary) {
            if (m_ctx.showStatusMessage) {
              m_ctx.showStatusMessage(
                  tr("%1: %2 details fetched for %n item(s).", nullptr, summary.scraped)
                      .arg(name, storeName));
            }
            // Fresh media on disk — drop the negative directory-listing
            // entries so the grid/sidebar pick the files up.
            ArtworkUtils::clearDirectoryCache();
            // Refresh by name: the collection list may have been reordered
            // while the batch ran, so a snapshotted index could be stale.
            QList<CollectionConfig> *collections =
                m_ctx.getCollections ? m_ctx.getCollections() : nullptr;
            if (collections != nullptr && m_ctx.refreshCollection) {
              for (int i = 0; i < collections->size(); ++i) {
                if (collections->at(i).name == name) {
                  m_ctx.refreshCollection(i);
                  break;
                }
              }
            }
            m_enrichRunner->deleteLater();
            m_enrichRunner = nullptr;
            startNextEnrichment();
          });
  m_enrichRunner->start();
}

void LauncherImportController::startupSync() {
  startBackgroundSync(/*announce=*/false);
}

void LauncherImportController::syncLauncherCollections() {
  startBackgroundSync(/*announce=*/true);
}

void LauncherImportController::startBackgroundSync(bool announce) {
  if (m_syncWatcher.isRunning()) {
    if (announce && m_ctx.showStatusMessage) {
      m_ctx.showStatusMessage(tr("Launcher sync is already running."));
    }
    return;
  }
  const QList<SyncJob> jobs = snapshotJobs();
  if (jobs.isEmpty()) {
    if (announce && m_ctx.showStatusMessage) {
      m_ctx.showStatusMessage(
          tr("No launcher collections to sync — use File → Import → Import from Launcher first."));
    }
    return;
  }
  m_announceSync = announce;
  // Snapshot-in, results-out: the worker reads launcher manifests, rewrites
  // stubs, and writes Steam metadata through its own DB connection, all from
  // the copied job list; the live collection list and DatabaseManager stay
  // on the GUI thread (same contract as DatAuditController's
  // startupLibraryScan; metadata connection per BulkEditService's pattern).
  m_syncWatcher.setFuture(QtConcurrent::run([jobs]() {
    QList<SyncOutcome> outcomes;
    outcomes.reserve(jobs.size());
    for (const SyncJob &job : jobs) {
      SyncOutcome outcome;
      outcome.collectionIndex = job.collectionIndex;
      outcome.sourceId = job.sourceId;
      outcome.collectionUuid = job.collectionUuid;
      outcome.result = LauncherImportService::syncSource(job.sourceId, job.stubDir, job.artworkDir,
                                                         job.scope, job.sourceKey);
      // No-op for non-Steam sources (their targets carry no appid).
      outcome.metadata = LauncherImportService::applySteamMetadata(job.dbPath, job.collectionUuid,
                                                                   outcome.result.syncedStubs);
      outcomes.append(outcome);
    }
    return outcomes;
  }));
}

void LauncherImportController::onSyncFinished() {
  const QList<SyncOutcome> outcomes = m_syncWatcher.result();
  QList<CollectionConfig> *collections = m_ctx.getCollections ? m_ctx.getCollections() : nullptr;

  IDatabaseManager *db = m_ctx.getDatabaseManager ? m_ctx.getDatabaseManager() : nullptr;
  int written = 0;
  int removed = 0;
  int artworkCopied = 0;
  int metadataRows = 0;
  int errorCount = 0;
  bool anyChanged = false;
  for (const SyncOutcome &outcome : outcomes) {
    written += outcome.result.written;
    removed += outcome.result.removed;
    artworkCopied += outcome.result.artworkCopied;
    metadataRows += outcome.metadata.rowsWritten;
    errorCount += static_cast<int>(outcome.result.errors.size() + outcome.metadata.errors.size());
    logSyncErrors(outcome.sourceId, outcome.result);
    for (const QString &error : outcome.metadata.errors) {
      qCWarning(lcLauncherImport).nospace() << "metadata '" << outcome.sourceId << "': " << error;
    }
    // The worker wrote metadata through its own connection — invalidate the
    // (main-thread-only) per-item LRU here so the sidebar re-reads fresh.
    if (db) {
      for (const QString &path : outcome.metadata.writtenPaths) {
        db->invalidateMetadataCacheItem(outcome.collectionUuid, path);
      }
    }
    // Resume store enrichment for anything still lacking its description.
    // Deliberately ahead of the changed() guard: a sync that writes no stubs
    // is exactly the case where an earlier enrichment was cut short, and
    // enrichFromStore is a no-op once every game has its text. Without this
    // the import dialog was the only thing that ever ran the network pass, so
    // whatever it did not finish stayed empty forever (Kartend-el5st
    // follow-up: a wide-scope import is hundreds of items, not a handful).
    if (collections && outcome.collectionIndex >= 0 &&
        outcome.collectionIndex < collections->size() &&
        collections->at(outcome.collectionIndex).importSource == outcome.sourceId) {
      enrichFromStore(collections->at(outcome.collectionIndex), outcome.result.syncedStubs);
      fetchRemoteCovers(collections->at(outcome.collectionIndex), outcome.result.syncedStubs);
    }
    if (!outcome.result.changed()) {
      continue;
    }
    anyChanged = true;
    // Re-validate the snapshotted index against the live list — collections
    // may have been removed or reordered while the worker ran. A stale hit
    // is harmless to skip: the changed folder mtime makes the next
    // navigation re-scan anyway.
    if (collections && m_ctx.refreshCollection && outcome.collectionIndex >= 0 &&
        outcome.collectionIndex < collections->size() &&
        collections->at(outcome.collectionIndex).importSource == outcome.sourceId) {
      m_ctx.refreshCollection(outcome.collectionIndex);
    }
  }

  if (artworkCopied > 0) {
    ArtworkUtils::clearDirectoryCache();
  }
  if (!m_ctx.showStatusMessage ||
      (!m_announceSync && !anyChanged && artworkCopied == 0 && metadataRows == 0)) {
    return;
  }
  QString message;
  if (!anyChanged && artworkCopied == 0 && metadataRows == 0) {
    message = tr("Launcher collections are up to date.");
  } else {
    message = tr("Launcher sync: %1 added or updated, %2 removed, %3 artwork files copied.")
                  .arg(written)
                  .arg(removed)
                  .arg(artworkCopied);
    if (metadataRows > 0) {
      message +=
          QLatin1Char(' ') + tr("Steam metadata filled for %n item(s).", nullptr, metadataRows);
    }
  }
  if (errorCount > 0) {
    message += QLatin1Char(' ') + tr("%n error(s) logged.", nullptr, errorCount);
  }
  m_ctx.showStatusMessage(message);
}

auto LauncherImportController::snapshotJobs() const -> QList<SyncJob> {
  QList<SyncJob> jobs;
  QList<CollectionConfig> *collections = m_ctx.getCollections ? m_ctx.getCollections() : nullptr;
  if (!collections) {
    return jobs;
  }
  for (int i = 0; i < collections->size(); ++i) {
    const CollectionConfig &config = collections->at(i);
    if (config.importSource.isEmpty() || config.isPlaylist) {
      continue;
    }
    SyncJob job;
    job.collectionIndex = i;
    job.sourceId = config.importSource;
    job.sourceKey = config.importSourceKey;
    job.scope = LauncherImportService::scopeFromString(config.importScope);
    job.stubDir = PathUtils::expandPathWithoutExistenceCheck(config.mediaDirectory, config.name);
    job.artworkDir =
        PathUtils::expandPathWithoutExistenceCheck(config.artworkDirectory, config.name);
    if (job.stubDir.isEmpty()) {
      continue;
    }
    // Captured on the GUI thread so the worker's metadata pass never touches
    // DatabaseManager: the canonical uuid pairing plus the db file path.
    job.collectionUuid = CollectionUtils::computeCollectionUuid(config);
    if (IDatabaseManager *db = m_ctx.getDatabaseManager ? m_ctx.getDatabaseManager() : nullptr) {
      job.dbPath = db->databaseFilePath();
    }
    jobs.append(job);
  }
  return jobs;
}
