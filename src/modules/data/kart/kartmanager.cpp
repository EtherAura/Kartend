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
#include "isettingsmanager.h"
#include "kartreader.h"
#include "kartwriter.h"

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

QList<SuspiciousKartPath> collectSuspiciousKartPaths(const CollectionConfig &cfg,
                                                     const QSet<QString> &trustedLauncherPaths) {
  QList<SuspiciousKartPath> out;
  const QString home = QDir::homePath();
  const QStringList allowedRoots = {home, QStringLiteral("/usr/bin"),
                                    QStringLiteral("/usr/local/bin"), QStringLiteral("/opt")};
  auto isPathAllowed = [&](const QString &path) {
    const QString abs = QFileInfo(path).absoluteFilePath();
    for (const QString &root : allowedRoots) {
      if (abs.startsWith(root + QLatin1Char('/')) || abs == root) {
        return true;
      }
    }
    return false;
  };
  auto check = [&](const QString &field, const QString &path, bool launcherField) {
    if (path.isEmpty()) return;
    if (isPathAllowed(path)) return;
    // Kartend-s6mj: a path already trusted via an existing collection's
    // launcher entry doesn't need to re-prompt the user. Only applies to
    // launcher fields — the icon/placeholder fields aren't reused across
    // collections in the same way.
    if (launcherField && trustedLauncherPaths.contains(path)) {
      return;
    }
    out.append({field, path});
  };
  check(QStringLiteral("launcher.launcherPath"), cfg.launcher.launcherPath, /*launcherField=*/true);
  for (int i = 0; i < cfg.launcher.additionalLaunchers.size(); ++i) {
    check(QStringLiteral("additionalLaunchers[%1].launcherPath").arg(i),
          cfg.launcher.additionalLaunchers[i].launcherPath, /*launcherField=*/true);
  }
  check(QStringLiteral("collectionIcon"), cfg.collectionIcon, /*launcherField=*/false);
  check(QStringLiteral("placeholderArtwork"), cfg.placeholderArtwork, /*launcherField=*/false);
  return out;
}

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
    if (!m_setup.settingsManager || !m_setup.getCollections) {
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
    m_setup.settingsManager->saveCollections(*collections);
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
