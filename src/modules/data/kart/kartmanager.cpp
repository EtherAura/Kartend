#include "kartmanager.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QtConcurrentRun>

#include "collectionutils.h"
#include "kartdb.h"
// kartmergedialog.h is intentionally still included: the merge dialog is a
// blocking, interactive decision (KartManager::makeInteractiveResolver runs
// dlg.exec() mid-import and waits on the user's choice). Converting that to a
// signal-driven flow is a larger redesign tracked as a follow-up. The
// one-way progress dialog, by contrast, has been replaced with signals — see
// the kartProgress* signals in the header.
#include "isettingsmanager.h"
#include "kartmergedialog.h"
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

KartManager::KartManager(QObject *parent) : QObject(parent) {}
KartManager::~KartManager() = default;

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

ConflictResolver KartManager::makeInteractiveResolver(QWidget *parent) {
  return [parent](const QString &itemPath, const ItemMetadataStore::ItemMetadata &existing,
                  const ItemMetadataStore::ItemMetadata &incoming) {
    KartMergeDialog dlg(itemPath, existing, incoming, parent);
    ConflictResolution r;
    if (dlg.exec() == QDialog::Accepted) {
      r.choice = dlg.choice();
      r.policy = dlg.policy();
      r.applyToAll = dlg.applyToAll();
    } else {
      r.choice = MergeChoice::Skip;
    }
    return r;
  };
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
            auto finalRes =
                finalizeImport(extracted.value(), true, makeInteractiveResolver(parent));
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
