#include "kartmanager.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QtConcurrentRun>

#include "collectionutils.h"
#include "errorutils.h"
#include "kartdb.h"
// Kartend-a3ir: the previous #include of "kartmergedialog.h" was the last
// data->ui edge in src/. The interactive merge-conflict decision is now
// supplied by the owner (typically MainWindow) as a ConflictResolver
// closure in KartManagerSetup::mergeResolver — see runImport(). The
// closure builds the KartMergeDialog with the right parent and returns
// the user's choice; KartManager itself never knows the dialog type. The
// one-way progress dialog uses the kartProgress* signal family for the
// same reason (see the header).
#include "applicationcontext.h"
#include "iplaylistmanager.h"
#include "isettingsmanager.h"
#include "kartreader.h"
#include "kartwriter.h"
#include "pathutils.h"
#include "smartfilter.h"

namespace kart {

namespace {

ConflictResolver makeFixedChoiceResolver(MergeChoice choice) {
  return [choice](const QString &, const ItemMetadataStore::ItemMetadata &,
                  const ItemMetadataStore::ItemMetadata &) {
    ConflictResolution r;
    r.choice = choice;
    return r;
  };
}

} // namespace

// collectSuspiciousKartPaths now lives in kartsuspiciouspaths.cpp so the
// preflight unit tests can link it without pulling in the whole manager
// translation unit. The declaration stays in kartmanager.h.

KartManager::KartManager(QObject *parent) : QObject(parent) {}
KartManager::~KartManager() = default;

QSet<QString> KartManager::previouslyTrustedLauncherPaths() const {
  QSet<QString> out;
  if (!m_setup.getCollections) {
    return out;
  }
  QList<CollectionConfig> *collections = m_setup.getCollections();
  if (!collections) {
    return out;
  }
  for (const CollectionConfig &c : std::as_const(*collections)) {
    if (!c.launcher.launcherPath.isEmpty()) {
      out.insert(c.launcher.launcherPath);
    }
    for (const LauncherConfig &alt : std::as_const(c.launcher.additionalLaunchers)) {
      if (!alt.launcherPath.isEmpty()) {
        out.insert(alt.launcherPath);
      }
    }
  }
  return out;
}

void KartManager::setupReferences(const KartManagerSetup &setup) {
  m_setup = setup;
}

void KartManager::cancelActiveKartOperation() {
  // Whichever operation is in flight (at most one) gets its cooperative
  // cancel flag set. The progress dialog's cancelRequested signal lands here,
  // so the dialog type stays entirely on the owner's side.
  if (m_activeReader) {
    m_activeReader->cancel();
  }
  if (m_activeWriter) {
    m_activeWriter->cancel();
  }
}

ErrorUtils::Result<KartReader::ExtractResult> KartManager::extractKart(const QString &kartPath,
                                                                       const QString &destDir) {
  KartReader::Extractor extractor;
  return extractor.extractTo(kartPath, destDir);
}

ErrorUtils::Result<QString> KartManager::finalizeImport(const KartReader::ExtractResult &result,
                                                        bool registerCollection,
                                                        const ConflictResolver &resolver) {
  CollectionConfig cfg = result.manifest.collectionConfig;
  if (cfg.name.isEmpty()) {
    cfg.name = result.manifest.name;
  }
  // Reject manifests whose collection name would inject a traversal segment
  // into '%collection%' substitution at launch time. The launch-side check
  // in LaunchManager::buildLaunchCommand catches this too, but failing here
  // surfaces the error with the import flow instead of silently importing a
  // collection whose first launch always fails.
  auto nameValidation = PathUtils::validateCollectionNameForSubstitution(cfg.name);
  if (nameValidation.isError()) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                           "Kart manifest carries an unsafe collection name",
                                           "KartManager::finalizeImport")
        .withDetails(QString("Collection '%1' contains '/', '\\', or '..' — "
                             "those would inject a path traversal into the "
                             "launcher arguments at launch time.")
                         .arg(cfg.name));
  }
  cfg.mediaDirectory = QDir(result.destDir).filePath("media");
  cfg.artworkDirectory = QDir(result.destDir).filePath("artwork");
  cfg.videoDirectory = QDir(result.destDir).filePath("video");
  cfg.manualDirectory = QDir(result.destDir).filePath("manual");
  cfg.parentCollectionIndex = -1;
  cfg.isSubcollection = false;

  // Kartend-efhg: log every imported path that falls outside the safe
  // allowlist so a malicious .kart that points launcherPath at /bin/sh
  // (or sneaks in a collectionIcon pointing at /etc) leaves an audit
  // trail before any later Launch click executes it.
  //
  // Kartend-s6mj: interactive callers ask the user via the
  // suspiciousPathConfirmer before getting here (runImport applies the
  // gate). Headless callers still hit finalizeImport directly, so we keep
  // the audit-log here as the floor.
  const auto suspicious = collectSuspiciousKartPaths(cfg, previouslyTrustedLauncherPaths());
  for (const auto &[field, path] : suspicious) {
    ErrorUtils::logError(ErrorUtils::ErrorContext::warning(
                             ErrorUtils::ErrorCode::InvalidFilePath,
                             "Imported .kart references a path outside the safe-prefix allowlist",
                             "KartManager::finalizeImport")
                             .withDetails(QString("Field: %1, Path: %2").arg(field, path)));
  }

  if (registerCollection) {
    ISettingsManager *settings = m_setup.ctx ? m_setup.ctx->settingsManager() : nullptr;
    if (!settings || !m_setup.getCollections) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                             "KartManager not wired to settings/collections list",
                                             "KartManager::finalizeImport");
    }
    QList<CollectionConfig> *collections = m_setup.getCollections();
    if (!collections) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                             "Collection list unavailable",
                                             "KartManager::finalizeImport");
    }
    QString uniqueName = cfg.name;
    int suffix = 2;
    auto nameExists = [&](const QString &n) {
      for (const CollectionConfig &c : std::as_const(*collections)) {
        if (c.name == n) return true;
      }
      return false;
    };
    while (nameExists(uniqueName)) {
      uniqueName = QString("%1 (%2)").arg(cfg.name).arg(suffix++);
    }
    cfg.name = uniqueName;
    collections->append(cfg);
    settings->saveCollections(*collections);
  }

  const QString collectionUuid =
      CollectionUtils::computeCollectionUuid(cfg.name, cfg.mediaDirectory);

  auto dbRes = openMediaDbConnection(QString("kart-import-%1").arg(collectionUuid));
  if (dbRes.isError()) {
    return dbRes.error();
  }
  QSqlDatabase db = dbRes.value();
  auto persistRes =
      persistImportedMetadata(db, result.manifest, result.destDir, collectionUuid,
                              resolver ? resolver : makeFixedChoiceResolver(MergeChoice::Skip));
  closeMediaDbConnection(db);

  if (persistRes.isError()) {
    return persistRes.error();
  }

  // Kartend-kmj1: restore bundled playlists onto the freshly-registered
  // collection. Smart playlists copy their SmartFilter JSON straight
  // through; static playlists translate bundled media_path entries back
  // to the finalized absolute paths via destDir. Missing payloads (e.g.
  // partial extract) are skipped so a malformed playlist never aborts
  // the import.
  if (registerCollection && m_setup.getPlaylistManager) {
    if (IPlaylistManager *pm = m_setup.getPlaylistManager()) {
      const QDir mediaRoot(QDir(result.destDir).filePath("media"));
      for (const KartManifest::PlaylistEntry &entry : result.manifest.playlists) {
        QString newPlaylistId;
        if (entry.isSmart) {
          auto filterRes = SmartFilter::fromJsonString(entry.smartFilterJson);
          if (filterRes.isError()) {
            ErrorUtils::logError(filterRes.error());
            continue;
          }
          auto created = pm->createSmartPlaylist(entry.name, filterRes.value(), collectionUuid);
          if (created.isError()) {
            ErrorUtils::logError(created.error());
            continue;
          }
          newPlaylistId = created.value();
        } else {
          auto created = pm->createPlaylist(entry.name, collectionUuid, entry.reservedKind);
          if (created.isError()) {
            ErrorUtils::logError(created.error());
            continue;
          }
          newPlaylistId = created.value();
        }
        if (newPlaylistId.isEmpty()) continue;
        if (entry.isSmart) continue;

        // PlaylistItemEntry::mediaPath is relative to the media/ subtree
        // of the bundle, matching what KartWriter populates. We strip a
        // leading "media/" if present, then resolve against mediaRoot to
        // produce the finalized absolute path the new items table sees.
        for (const KartManifest::PlaylistItemEntry &ie : entry.items) {
          QString rel = ie.mediaPath;
          if (rel.startsWith(QStringLiteral("media/"))) rel.remove(0, 6);
          if (rel.isEmpty()) continue;
          const QString abs = mediaRoot.filePath(rel);
          pm->addItem(newPlaylistId, collectionUuid, abs);
        }
      }
    }
  }

  return result.destDir;
}

ErrorUtils::Result<QString> KartManager::importKart(const QString &kartPath, const QString &destDir,
                                                    bool registerCollection) {
  auto extracted = extractKart(kartPath, destDir);
  if (extracted.isError()) return extracted.error();
  // Kartend-s6mj: importKart is the synchronous entry the drop-handler and
  // tests use. It still consults a wired confirmer so dropping a malicious
  // .kart prompts the user before the manifest's launcher path is
  // registered.
  if (m_setup.suspiciousPathConfirmer) {
    const auto suspicious = collectSuspiciousKartPaths(extracted.value().manifest.collectionConfig,
                                                       previouslyTrustedLauncherPaths());
    if (!suspicious.isEmpty() && !m_setup.suspiciousPathConfirmer(suspicious)) {
      return ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::OperationCancelled,
                                               "Import cancelled at suspicious-path confirmation",
                                               "KartManager::importKart");
    }
  }
  return finalizeImport(extracted.value(), registerCollection,
                        makeFixedChoiceResolver(MergeChoice::Skip));
}

ErrorUtils::Result<QString> KartManager::importKartHeadless(const QString &kartPath,
                                                            const QString &destDir,
                                                            bool registerCollection,
                                                            MergeChoice headlessChoice) {
  auto extracted = extractKart(kartPath, destDir);
  if (extracted.isError()) return extracted.error();
  return finalizeImport(extracted.value(), registerCollection,
                        makeFixedChoiceResolver(headlessChoice));
}

ErrorUtils::Result<void> KartManager::exportCollection(int collectionIndex,
                                                       const QString &outPath) {
  if (!m_setup.getCollections) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                           "KartManager not wired to collections list",
                                           "KartManager::exportCollection");
  }
  QList<CollectionConfig> *collections = m_setup.getCollections();
  if (!collections || collectionIndex < 0 || collectionIndex >= collections->size()) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::CollectionNotFound,
                                           "Collection index out of range",
                                           "KartManager::exportCollection");
  }
  const CollectionConfig &cfg = collections->at(collectionIndex);

  QList<LauncherPreset> presets;
  if (m_setup.getLauncherPresets) {
    presets = m_setup.getLauncherPresets();
  }

  const QString collectionUuid =
      CollectionUtils::computeCollectionUuid(cfg.name, cfg.mediaDirectory);
  auto dbRes = openMediaDbConnection(QString("kart-export-%1").arg(collectionUuid));
  if (dbRes.isError()) {
    return dbRes.error();
  }
  QSqlDatabase db = dbRes.value();
  auto prepRes = KartWriter::prepareFromCollection(cfg, collectionUuid, presets, &db);
  closeMediaDbConnection(db);
  if (prepRes.isError()) return prepRes.error();

  KartWriter::WriterParams params = prepRes.value();
  params.uuid = collectionUuid;
  params.name = cfg.name;

  // Kartend-kmj1: bundle every playlist whose parentCollectionUuid points
  // at the exported collection. Static-playlist items are translated to
  // the in-bundle media_path (KartWriter populates that field on
  // prepareFromCollection); references that don't resolve are dropped
  // because the import side can't reconstruct them either.
  if (m_setup.getPlaylistManager) {
    if (IPlaylistManager *pm = m_setup.getPlaylistManager()) {
      QHash<QString, QString> absToRel;
      for (const KartWriter::ItemSource &it : params.items) {
        if (!it.mediaAbs.isEmpty()) {
          absToRel.insert(it.mediaAbs, it.manifestItem.mediaPath);
        }
      }
      const QList<PlaylistRow> rows = pm->loadAll();
      for (const PlaylistRow &row : rows) {
        if (row.parentCollectionUuid != collectionUuid) continue;
        KartManifest::PlaylistEntry entry;
        entry.name = row.name;
        entry.icon = row.icon;
        entry.reservedKind = row.reservedKind;
        entry.isSmart = row.isSmart;
        entry.smartFilterJson = row.smartFilterJson;
        if (!row.isSmart) {
          const QList<PlaylistItemRef> refs = pm->loadItems(row.id);
          for (const PlaylistItemRef &ref : refs) {
            if (ref.sourceCollectionUuid != collectionUuid) continue;
            const auto it = absToRel.constFind(ref.sourcePath);
            if (it == absToRel.cend()) continue;
            KartManifest::PlaylistItemEntry ie;
            ie.mediaPath = it.value();
            ie.position = ref.position;
            entry.items.append(ie);
          }
        }
        params.playlists.append(entry);
      }
    }
  }

  KartWriter::Writer writer;
  auto wr = writer.writeKart(outPath, params);
  if (wr.isError()) return wr.error();
  return {};
}

void KartManager::importInteractive() {
  QWidget *parent = m_setup.getParentWindow ? m_setup.getParentWindow() : nullptr;

  const QString kartPath =
      QFileDialog::getOpenFileName(parent, tr("Import Kart"), QString(), tr("Kart files (*.kart)"));
  if (kartPath.isEmpty()) return;

  auto peeked = KartReader::peekManifest(kartPath);
  if (peeked.isError()) {
    emit importFailed(peeked.error());
    if (parent) {
      QMessageBox::warning(parent, tr("Import Kart"), peeked.error().message);
    }
    return;
  }

  // Preflight pass — surface launcher/path issues before the user picks a
  // destination, so cancelling here costs nothing on disk. The hook is
  // wired by the UI layer (MainWindow); in headless contexts we proceed
  // unconditionally and rely on the existing post-extract suspicious-path
  // gate for safety.
  if (m_setup.preflightConfirmer) {
    QSet<QString> existingNames;
    if (m_setup.getCollections) {
      if (auto *collections = m_setup.getCollections()) {
        for (const CollectionConfig &c : *collections) {
          existingNames.insert(c.name.trimmed().toLower());
        }
      }
    }
    const auto report =
        KartPreflight::buildReport(peeked.value(), previouslyTrustedLauncherPaths(), existingNames);
    if (!m_setup.preflightConfirmer(report)) {
      // Treat as a clean user cancellation — no error toast.
      return;
    }
  }

  const QString suggested = QDir::homePath() + "/" +
                            (peeked.value().name.isEmpty() ? QString("kart") : peeked.value().name);
  const QString destDir = QFileDialog::getExistingDirectory(
      parent, tr("Choose import destination"), suggested,
      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
  if (destDir.isEmpty()) return;

  runImport(kartPath, destDir);
}

void KartManager::exportCollectionInteractive(int collectionIndex) {
  QWidget *parent = m_setup.getParentWindow ? m_setup.getParentWindow() : nullptr;
  if (!m_setup.getCollections) {
    if (parent) {
      QMessageBox::warning(parent, tr("Export Kart"), tr("KartManager not wired"));
    }
    return;
  }
  QList<CollectionConfig> *collections = m_setup.getCollections();
  if (!collections || collectionIndex < 0 || collectionIndex >= collections->size()) {
    if (parent) {
      QMessageBox::warning(parent, tr("Export Kart"), tr("No collection selected"));
    }
    return;
  }
  const QString suggestedName = collections->at(collectionIndex).name + ".kart";
  const QString outPath = QFileDialog::getSaveFileName(parent, tr("Export Collection as Kart"),
                                                       QDir::homePath() + "/" + suggestedName,
                                                       tr("Kart files (*.kart)"));
  if (outPath.isEmpty()) return;

  runExport(collectionIndex, outPath);
}

void KartManager::runImport(const QString &kartPath, const QString &destDir) {
  m_activeReader = std::make_unique<KartReader::Extractor>(this);

  // Ask the owner to put up a progress dialog, then feed it through signals.
  // reader->progress/entryExtracted fire on the QtConcurrent worker thread,
  // so the chain into our own signals is a QueuedConnection (marshals onto
  // KartManager's thread); the owner's onward signal->dialog hops are
  // same-thread and stay direct.
  emit kartProgressStarted(tr("Importing Kart"));

  connect(m_activeReader.get(), &KartReader::Extractor::progress, this,
          &KartManager::kartProgressFraction, Qt::QueuedConnection);
  connect(m_activeReader.get(), &KartReader::Extractor::entryExtracted, this,
          &KartManager::kartProgressEntry, Qt::QueuedConnection);

  auto *watcher = new QFutureWatcher<ErrorUtils::Result<KartReader::ExtractResult>>(this);
  connect(watcher, &QFutureWatcher<ErrorUtils::Result<KartReader::ExtractResult>>::finished, this,
          [this, watcher]() {
            const auto extracted = watcher->result();
            watcher->deleteLater();
            m_activeReader.reset();
            if (extracted.isError()) {
              emit importFailed(extracted.error());
              emit kartProgressFailed();
              QWidget *parent = m_setup.getParentWindow ? m_setup.getParentWindow() : nullptr;
              if (parent) {
                QMessageBox::warning(parent, tr("Import Kart"), extracted.error().message);
              }
              return;
            }
            QWidget *parent = m_setup.getParentWindow ? m_setup.getParentWindow() : nullptr;
            // Kartend-s6mj: ask the user before importing a .kart whose
            // launcher / icon / placeholder paths fall outside the safe
            // allowlist. The pre-extracted manifest's collectionConfig
            // already carries the un-finalized fields (finalizeImport
            // overwrites only the *Directory paths, not launcher /
            // icon / placeholder), so the suspicious set is stable
            // here.
            const auto suspicious = collectSuspiciousKartPaths(
                extracted.value().manifest.collectionConfig, previouslyTrustedLauncherPaths());
            if (!suspicious.isEmpty() && m_setup.suspiciousPathConfirmer) {
              if (!m_setup.suspiciousPathConfirmer(suspicious)) {
                auto ctx = ErrorUtils::ErrorContext::warning(
                    ErrorUtils::ErrorCode::OperationCancelled,
                    "Import cancelled at suspicious-path confirmation", "KartManager::runImport");
                emit importFailed(ctx);
                emit kartProgressFailed();
                return;
              }
            }
            // The interactive merge resolver lives in the UI layer (see
            // Kartend-a3ir) — fall back to Skip in headless contexts where
            // no resolver was wired.
            const ConflictResolver &resolver = m_setup.mergeResolver
                                                   ? m_setup.mergeResolver
                                                   : makeFixedChoiceResolver(MergeChoice::Skip);
            auto finalRes = finalizeImport(extracted.value(), true, resolver);
            if (finalRes.isError()) {
              emit importFailed(finalRes.error());
              emit kartProgressFailed();
              if (parent) {
                QMessageBox::warning(parent, tr("Import Kart"), finalRes.error().message);
              }
              return;
            }
            emit kartProgressFinished();
            emit collectionImported(finalRes.value());
          });

  watcher->setFuture(QtConcurrent::run([kartPath, destDir]() {
    KartReader::Extractor extractor;
    return extractor.extractTo(kartPath, destDir);
  }));
}

void KartManager::runExport(int collectionIndex, const QString &outPath) {
  m_activeWriter = std::make_unique<KartWriter::Writer>(this);

  // Same one-way progress flow as runImport(); see the comment there. The
  // writer emits no per-entry signal, so kartProgressEntry is never sent.
  emit kartProgressStarted(tr("Exporting Kart"));

  connect(m_activeWriter.get(), &KartWriter::Writer::progress, this,
          &KartManager::kartProgressFraction, Qt::QueuedConnection);

  auto *watcher = new QFutureWatcher<ErrorUtils::Result<void>>(this);
  connect(watcher, &QFutureWatcher<ErrorUtils::Result<void>>::finished, this,
          [this, watcher, outPath]() {
            const auto res = watcher->result();
            watcher->deleteLater();
            m_activeWriter.reset();
            if (res.isError()) {
              emit exportFailed(res.error());
              emit kartProgressFailed();
              QWidget *parent = m_setup.getParentWindow ? m_setup.getParentWindow() : nullptr;
              if (parent) {
                QMessageBox::warning(parent, tr("Export Kart"), res.error().message);
              }
            } else {
              emit kartProgressFinished();
              emit kartExported(outPath);
            }
          });

  watcher->setFuture(QtConcurrent::run(
      [this, collectionIndex, outPath]() { return exportCollection(collectionIndex, outPath); }));
}

} // namespace kart
