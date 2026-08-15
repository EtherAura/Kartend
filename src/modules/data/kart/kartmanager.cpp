#include "kartmanager.h"

#include <atomic>

#include <QDeadlineTimer>
#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QSet>
#include <QtConcurrentRun>
#include <QThread>

#include "collection/collectionconfig.h"
#include "collection/launcherpreset.h"
#include "collection/typehelpers.h"
#include "errorpresentation.h"
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

/// Playlist rows (and their static-playlist item refs) captured BY VALUE
/// before an export's worker task launches. The task consumes only this
/// snapshot — never IPlaylistManager — so a teardown that abandons the task
/// after its bounded join can't leave it calling into freed managers
/// (Kartend audit jpit3 rework; payload semantics from Kartend-kmj1).
struct PlaylistExportSnapshot {
  QList<PlaylistRow> rows;
  QHash<QString, QList<PlaylistItemRef>> itemsByRowId;
};

PlaylistExportSnapshot snapshotPlaylistsForExport(IPlaylistManager *pm,
                                                  const QString &collectionUuid) {
  PlaylistExportSnapshot snap;
  if (!pm) {
    return snap;
  }
  const QList<PlaylistRow> rows = pm->loadAll();
  for (const PlaylistRow &row : rows) {
    if (row.parentCollectionUuid != collectionUuid) continue;
    snap.rows.append(row);
    if (!row.isSmart) {
      snap.itemsByRowId.insert(row.id, pm->loadItems(row.id));
    }
  }
  return snap;
}

/// The worker-safe body of a collection export: everything after the setup
/// closures have been consulted. Takes only value snapshots (plus the
/// optional signal-connected writer), so the QtConcurrent task in runExport
/// can run — and, when the destructor's bounded join times out, be abandoned
/// — without ever touching KartManager state (Kartend audit jpit3 rework).
ErrorUtils::Result<void> doExportKart(const CollectionConfig &cfg,
                                      const QList<LauncherPreset> &presets,
                                      const PlaylistExportSnapshot &playlists,
                                      const QString &outPath, KartWriter::Writer *writer) {
  const QString collectionUuid =
      CollectionUtils::computeCollectionUuid(cfg.name, cfg.mediaDirectory);
  // Connection name suffixed with a process-global counter, not just the
  // uuid: an abandoned (timed-out teardown) export leaks its task, and a
  // later export of the SAME collection would otherwise addDatabase() under
  // the identical name, replacing the leaked task's in-use connection —
  // Qt-documented UB. Same reasoning as ScrapeWriteWorker's counter
  // (Kartend-fux2w).
  static std::atomic<quint64> s_exportConnectionId{0};
  auto dbRes =
      openMediaDbConnection(QString("kart-export-%1-%2")
                                .arg(collectionUuid)
                                .arg(s_exportConnectionId.fetch_add(1, std::memory_order_relaxed)));
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

  // Kartend-kmj1: bundle every playlist whose parentCollectionUuid points at
  // the exported collection (the snapshot is pre-filtered to those).
  // Static-playlist items are translated to the in-bundle media_path
  // (KartWriter populates that field on prepareFromCollection); references
  // that don't resolve are dropped because the import side can't
  // reconstruct them either.
  QHash<QString, QString> absToRel;
  for (const KartWriter::ItemSource &it : params.items) {
    if (!it.mediaAbs.isEmpty()) {
      absToRel.insert(it.mediaAbs, it.manifestItem.mediaPath);
    }
  }
  for (const PlaylistRow &row : playlists.rows) {
    KartManifest::PlaylistEntry entry;
    entry.name = row.name;
    entry.icon = row.icon;
    entry.reservedKind = row.reservedKind;
    entry.isSmart = row.isSmart;
    entry.smartFilterJson = row.smartFilterJson;
    if (!row.isSmart) {
      const QList<PlaylistItemRef> refs = playlists.itemsByRowId.value(row.id);
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

  // Use the caller-supplied writer (runExport's m_activeWriter, whose
  // progress signal + cancel flag are wired up) when provided; otherwise a
  // local one.
  KartWriter::Writer localWriter;
  KartWriter::Writer &activeWriter = writer ? *writer : localWriter;
  auto wr = activeWriter.writeKart(outPath, params);
  if (wr.isError()) return wr.error();
  return {};
}

} // namespace

// The launcher-trust collectors (collectLauncherTrustFindings,
// collectSuspiciousKartPaths, collectInTreeLauncherPaths, …) live in
// kartsuspiciouspaths.cpp so the preflight unit tests can link them without
// pulling in the whole manager translation unit. Their declarations are in
// kartlaunchertrust.h, which both this header and kartpreflight.h include.

KartManager::KartManager(QObject *parent) : QObject(parent) {}

KartManager::~KartManager() {
  // Kartend audit jpit3: an in-flight export/import runs on the global
  // QThreadPool. QtConcurrent futures can't be cancelled and ~QObject does
  // not wait, so flip the cooperative cancel flag (the Writer/Extractor poll
  // it and bail at the next entry/asset boundary) and join the future before
  // this object goes away.
  cancelActiveKartOperation();
  if (m_activeWatcher) {
    // The join is BOUNDED (2s — the DatabaseManager teardown norm) rather
    // than the previous unbounded waitForFinished(): a task wedged inside a
    // single un-interruptible syscall (stalled mount) never reaches the
    // cancel-flag poll, and an unbounded wait hung the app close for as long
    // as that syscall took. Abandoning past the budget is safe because the
    // task bodies capture only values plus their OWN shared_ptr to the
    // Writer/Extractor — no `this`, no m_setup closures (runExport snapshots
    // the collection/preset/playlist data up front) — so on timeout the task
    // and its shared captures are intentionally leaked and reaped at process
    // exit, the same warn-and-leak trade DatabaseManager makes. Data safety
    // on abandon: an export stages through writeKart's QSaveFile, which
    // never commits on the cancel/error path, so the destination .kart can't
    // be left corrupt; an import can leave partial files under the
    // user-chosen destDir, exactly as a user cancel already can. The watcher
    // is a child of `this` and is destroyed below, so no continuation ever
    // fires post-dtor.
    constexpr int kTeardownJoinBudgetMs = 2000;
    QDeadlineTimer deadline(kTeardownJoinBudgetMs);
    while (!m_activeWatcher->isFinished() && !deadline.hasExpired()) {
      QThread::msleep(10);
    }
    if (!m_activeWatcher->isFinished()) {
      qWarning() << "KartManager: in-flight kart operation did not finish within"
                 << kTeardownJoinBudgetMs
                 << "ms at teardown; abandoning it (the task holds only value/shared captures)";
    }
  }
}

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

QList<SuspiciousKartPath> KartManager::launcherConfirmationRows(const CollectionConfig &cfg,
                                                                const QString &extractedRoot,
                                                                bool onlyBundledPayloads) const {
  // Kartend-kxqqf: the gate is "does this bundle want to run something?",
  // not "is the path it names on a list?". A launcher living in an
  // allowlisted directory still arrives with a bundle-chosen core and
  // bundle-chosen arguments, which is enough to run anything; the allowlist
  // survives only as the reason label on the row.
  const auto findings =
      collectLauncherTrustFindings(cfg, previouslyTrustedLauncherPaths(), extractedRoot);
  if (!onlyBundledPayloads) {
    return importConfirmationRows(findings, collectSuspiciousAssetPaths(cfg));
  }
  QList<KartLauncherFinding> bundled;
  for (const KartLauncherFinding &f : findings) {
    if (f.reason == LauncherTrustReason::BundledExecutable) bundled.append(f);
  }
  return importConfirmationRows(bundled, {});
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

  // Kartend-kxqqf: the allowlist above says nothing about the two fields that
  // make an import dangerous — the core the launcher loads and the arguments
  // it is handed. Log every launcher field that tripped a danger signal, with
  // the extraction root in hand so a bundle-shipped payload is recognised.
  const auto trustFindings =
      collectLauncherTrustFindings(cfg, previouslyTrustedLauncherPaths(), result.destDir);
  for (const KartLauncherFinding &finding : trustFindings) {
    if (!isElevatedLauncherTrustReason(finding.reason)) continue;
    ErrorUtils::logError(
        ErrorUtils::ErrorContext::warning(
            ErrorUtils::ErrorCode::InvalidFilePath,
            "Imported .kart carries a launcher configuration that can execute arbitrary code",
            "KartManager::finalizeImport")
            .withDetails(
                QString("Field: %1, Reason: %2, Value: %3")
                    .arg(finding.field, launcherTrustReasonLabel(finding.reason), finding.value)));
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
    ErrorPresentation::reportSaveResult(settings->saveCollections(*collections), "collections",
                                        true);
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

  // Kartend-jset: post-import launcher-path probe. Karts built on a
  // different machine often carry absolute launcher paths that don't exist
  // on the importing host (e.g. /opt/imported-from-another-machine/runner).
  // Log every issue + invoke the optional UI reporter so the user finds
  // out at import time rather than at first-Launch click.
  QList<MissingLauncherPathIssue> launcherIssues;
  auto recordLauncherIssue = [&](const QString &field, const QString &path) {
    if (path.isEmpty()) return;
    const PathUtils::PathStatus status = PathUtils::checkLauncherPath(path);
    if (status == PathUtils::PathStatus::OK || status == PathUtils::PathStatus::Empty) {
      return;
    }
    launcherIssues.append(
        {field, QStringLiteral("'%1' %2").arg(path, PathUtils::pathStatusDescription(status))});
  };
  recordLauncherIssue(QStringLiteral("launcherPath"), cfg.launcher.launcherPath);
  for (int i = 0; i < cfg.launcher.additionalLaunchers.size(); ++i) {
    recordLauncherIssue(QStringLiteral("additionalLaunchers[%1].launcherPath").arg(i),
                        cfg.launcher.additionalLaunchers[i].launcherPath);
  }
  if (!launcherIssues.isEmpty()) {
    QStringList lines;
    lines.reserve(launcherIssues.size());
    for (const auto &[field, desc] : launcherIssues) {
      lines.append(QStringLiteral("%1: %2").arg(field, desc));
    }
    ErrorUtils::logError(
        ErrorUtils::ErrorContext::warning(
            ErrorUtils::ErrorCode::InvalidFilePath,
            "Imported .kart launcher paths don't resolve on this host",
            "KartManager::finalizeImport")
            .withDetails(QStringLiteral("Collection: %1, Issues:\n  - %2")
                             .arg(cfg.name, lines.join(QStringLiteral("\n  - ")))));
    if (m_setup.missingLauncherPathsReporter) {
      m_setup.missingLauncherPathsReporter(cfg.name, launcherIssues);
    }
  }

  return result.destDir;
}

ErrorUtils::Result<QString> KartManager::importKart(const QString &kartPath, const QString &destDir,
                                                    bool registerCollection) {
  auto extracted = extractKart(kartPath, destDir);
  if (extracted.isError()) return extracted.error();
  // Kartend-s6mj: importKart is the synchronous entry kept for tests and
  // headless-style callers (the drag-drop drain used to run it on the GUI
  // thread; Kartend-h7xnr.1 rerouted drops through importKartAsync's worker
  // path). It still consults a wired confirmer so a malicious .kart prompts
  // the user before the manifest's launcher path is registered.
  if (m_setup.suspiciousPathConfirmer) {
    const auto rows = launcherConfirmationRows(extracted.value().manifest.collectionConfig,
                                               extracted.value().destDir);
    if (!rows.isEmpty() && !m_setup.suspiciousPathConfirmer(rows)) {
      return ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::OperationCancelled,
                                               "Import cancelled at launcher confirmation",
                                               "KartManager::importKart");
    }
  }
  return finalizeImport(extracted.value(), registerCollection,
                        makeFixedChoiceResolver(MergeChoice::Skip));
}

void KartManager::importKartAsync(const QString &kartPath, const QString &destDir) {
  runImport(kartPath, destDir);
}

ErrorUtils::Result<QString> KartManager::importKartHeadless(const QString &kartPath,
                                                            const QString &destDir,
                                                            bool registerCollection,
                                                            MergeChoice headlessChoice,
                                                            bool allowUntrustedLauncher) {
  auto extracted = extractKart(kartPath, destDir);
  if (extracted.isError()) return extracted.error();
  // Kartend-u8wf0: headless import has no interactive confirmer, so the
  // suspicious-path gate the synchronous/GUI paths run (importKart and the
  // async dialog flow) is bypassed by design. The dangerous case the GUI
  // gate would catch is a manifest that bundles its OWN executable and points
  // launcherPath at it: collectSuspiciousKartPaths can't flag that because the
  // extraction root usually sits under $HOME (an allowlisted prefix), yet a
  // later Launch click would run code the kart fully chose. Refuse those
  // in-tree launcher paths unless the caller explicitly opts in
  // (--allow-untrusted-launcher), and always log the finding either way.
  const auto inTree = collectInTreeLauncherPaths(extracted.value().manifest.collectionConfig,
                                                 extracted.value().destDir);
  if (!inTree.isEmpty()) {
    QStringList lines;
    lines.reserve(inTree.size());
    for (const auto &[field, path] : inTree) {
      lines.append(QStringLiteral("%1: %2").arg(field, path));
    }
    const QString joined = lines.join(QStringLiteral("\n  - "));
    if (!allowUntrustedLauncher) {
      return ErrorUtils::ErrorContext::error(
                 ErrorUtils::ErrorCode::InvalidFilePath,
                 "Refusing headless import: the .kart bundles its own launcher executable",
                 "KartManager::importKartHeadless")
          .withDetails(
              QStringLiteral("These launcher paths resolve inside the extracted kart tree (%1), so "
                             "the kart would run an executable it shipped. Re-run with "
                             "--allow-untrusted-launcher only if you trust this source.\n  - %2")
                  .arg(extracted.value().destDir, joined));
    }
    ErrorUtils::logError(
        ErrorUtils::ErrorContext::warning(
            ErrorUtils::ErrorCode::InvalidFilePath,
            "Headless import accepted a self-bundled launcher via --allow-untrusted-launcher",
            "KartManager::importKartHeadless")
            .withDetails(QStringLiteral("Source: %1, In-tree launcher paths:\n  - %2")
                             .arg(kartPath, joined)));
  }
  return finalizeImport(extracted.value(), registerCollection,
                        makeFixedChoiceResolver(headlessChoice));
}

ErrorUtils::Result<void> KartManager::exportCollection(int collectionIndex, const QString &outPath,
                                                       KartWriter::Writer *writer) {
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
  const CollectionConfig cfg = collections->at(collectionIndex);

  QList<LauncherPreset> presets;
  if (m_setup.getLauncherPresets) {
    presets = m_setup.getLauncherPresets();
  }
  const QString collectionUuid =
      CollectionUtils::computeCollectionUuid(cfg.name, cfg.mediaDirectory);
  PlaylistExportSnapshot playlists;
  if (m_setup.getPlaylistManager) {
    playlists = snapshotPlaylistsForExport(m_setup.getPlaylistManager(), collectionUuid);
  }
  return doExportKart(cfg, presets, playlists, outPath, writer);
}

// Kartend-sqoq0: route the warning through the owner-supplied runner when
// wired; otherwise fall back to the stock QMessageBox parented on
// getParentWindow() — shown only when a parent exists, matching the
// pre-runner behavior (headless contexts stayed silent).
void KartManager::showWarning(const QString &title, const QString &text) {
  if (m_setup.dialogs.warn) {
    m_setup.dialogs.warn(title, text);
    return;
  }
  QWidget *parent = m_setup.getParentWindow ? m_setup.getParentWindow() : nullptr;
  if (parent) {
    QMessageBox::warning(parent, title, text);
  }
}

void KartManager::importInteractive() {
  QWidget *parent = m_setup.getParentWindow ? m_setup.getParentWindow() : nullptr;

  const QString kartPath =
      m_setup.dialogs.getOpenFileName
          ? m_setup.dialogs.getOpenFileName(tr("Import Kart"), QString(), tr("Kart files (*.kart)"))
          : QFileDialog::getOpenFileName(parent, tr("Import Kart"), QString(),
                                         tr("Kart files (*.kart)"));
  if (kartPath.isEmpty()) return;

  auto peeked = KartReader::peekManifest(kartPath);
  if (peeked.isError()) {
    emit importFailed(peeked.error());
    showWarning(tr("Import Kart"), peeked.error().message);
    return;
  }

  // Preflight pass — surface the bundle's launcher configuration and any
  // path issues before the user picks a destination, so cancelling here
  // costs nothing on disk. The hook is wired by the UI layer (MainWindow);
  // in headless contexts we proceed unconditionally and rely on the
  // post-extract launcher gate for safety.
  bool preflightConfirmed = false;
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
    preflightConfirmed = true;
  }

  const QString suggested = QDir::homePath() + "/" +
                            (peeked.value().name.isEmpty() ? QString("kart") : peeked.value().name);
  const QString destDir =
      m_setup.dialogs.getExistingDirectory
          ? m_setup.dialogs.getExistingDirectory(tr("Choose import destination"), suggested)
          : QFileDialog::getExistingDirectory(parent, tr("Choose import destination"), suggested,
                                              QFileDialog::ShowDirsOnly |
                                                  QFileDialog::DontResolveSymlinks);
  if (destDir.isEmpty()) return;

  runImport(kartPath, destDir, preflightConfirmed);
}

void KartManager::exportCollectionInteractive(int collectionIndex) {
  QWidget *parent = m_setup.getParentWindow ? m_setup.getParentWindow() : nullptr;
  if (!m_setup.getCollections) {
    showWarning(tr("Export Kart"), tr("KartManager not wired"));
    return;
  }
  QList<CollectionConfig> *collections = m_setup.getCollections();
  if (!collections || collectionIndex < 0 || collectionIndex >= collections->size()) {
    showWarning(tr("Export Kart"), tr("No collection selected"));
    return;
  }
  const QString suggestedName = collections->at(collectionIndex).name + ".kart";
  const QString suggestedPath = QDir::homePath() + "/" + suggestedName;
  const QString outPath =
      m_setup.dialogs.getSaveFileName
          ? m_setup.dialogs.getSaveFileName(tr("Export Collection as Kart"), suggestedPath,
                                            tr("Kart files (*.kart)"))
          : QFileDialog::getSaveFileName(parent, tr("Export Collection as Kart"), suggestedPath,
                                         tr("Kart files (*.kart)"));
  if (outPath.isEmpty()) return;

  runExport(collectionIndex, outPath);
}

void KartManager::runImport(const QString &kartPath, const QString &destDir,
                            bool preflightConfirmed) {
  // Un-parented and shared_ptr-owned on purpose — the worker task holds its
  // own reference so a timed-out teardown join can abandon it safely (see
  // the m_activeReader member doc and ~KartManager).
  m_activeReader = std::make_shared<KartReader::Extractor>();

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
  m_activeWatcher = watcher; // Kartend audit jpit3: ~KartManager joins on this.
  connect(watcher, &QFutureWatcher<ErrorUtils::Result<KartReader::ExtractResult>>::finished, this,
          [this, watcher, preflightConfirmed]() {
            const auto extracted = watcher->result();
            watcher->deleteLater();
            m_activeWatcher = nullptr;
            m_activeReader.reset();
            if (extracted.isError()) {
              emit importFailed(extracted.error());
              emit kartProgressFailed();
              showWarning(tr("Import Kart"), extracted.error().message);
              return;
            }
            // Kartend-s6mj / Kartend-kxqqf: ask the user before importing a
            // .kart that carries a launcher configuration at all — the
            // program, the core and the arguments are the bundle's choice,
            // and the drag-and-drop route reaches here without passing the
            // preflight dialog. The pre-extracted manifest's collectionConfig
            // already carries the un-finalized fields (finalizeImport
            // overwrites only the *Directory paths, not launcher / icon /
            // placeholder), so the row set is stable here; the extraction
            // root is passed so a bundle-shipped payload is recognised too
            // (Kartend-f8y08). A menu import already showed these rows in the
            // preflight dialog and got its answer, so it re-asks only about
            // the one escalation that needs the extracted tree to see.
            const auto rows = launcherConfirmationRows(extracted.value().manifest.collectionConfig,
                                                       extracted.value().destDir,
                                                       /*onlyBundledPayloads=*/preflightConfirmed);
            if (!rows.isEmpty() && m_setup.suspiciousPathConfirmer) {
              if (!m_setup.suspiciousPathConfirmer(rows)) {
                auto ctx = ErrorUtils::ErrorContext::warning(
                    ErrorUtils::ErrorCode::OperationCancelled,
                    "Import cancelled at launcher confirmation", "KartManager::runImport");
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
              showWarning(tr("Import Kart"), finalRes.error().message);
              return;
            }
            emit kartProgressFinished();
            emit collectionImported(finalRes.value());
          });

  // Run the *connected* reader (m_activeReader) on the worker, not a throwaway
  // local Extractor — otherwise its progress/entryExtracted signals (wired
  // above) never fire and cancelActiveKartOperation()'s cancel flag can't reach
  // the Extractor doing the work. The task captures its OWN shared_ptr (never
  // `this`), so the destructor's bounded join can abandon a wedged extraction
  // without freeing the Extractor out from under it (see ~KartManager).
  auto reader = m_activeReader;
  watcher->setFuture(QtConcurrent::run(
      [reader, kartPath, destDir]() { return reader->extractTo(kartPath, destDir); }));
}

void KartManager::runExport(int collectionIndex, const QString &outPath) {
  // Snapshot everything the worker needs ON THIS THREAD, before the task
  // launches: the task must capture only values plus the shared writer so
  // the destructor's bounded join can abandon it (see ~KartManager).
  // Consulting the m_setup closures from the worker thread was what forced
  // the old destructor to join unbounded. The wiring checks mirror
  // exportCollection's; the interactive entry validated the index already,
  // so these are defensive — reported through the same failure signals,
  // minus the progress-dialog bracket that never opened (the importInteractive
  // peek-failure path sets that precedent).
  if (!m_setup.getCollections) {
    auto ctx = ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                               "KartManager not wired to collections list",
                                               "KartManager::runExport");
    emit exportFailed(ctx);
    showWarning(tr("Export Kart"), ctx.message);
    return;
  }
  QList<CollectionConfig> *collections = m_setup.getCollections();
  if (!collections || collectionIndex < 0 || collectionIndex >= collections->size()) {
    auto ctx =
        ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::CollectionNotFound,
                                        "Collection index out of range", "KartManager::runExport");
    emit exportFailed(ctx);
    showWarning(tr("Export Kart"), ctx.message);
    return;
  }
  const CollectionConfig cfg = collections->at(collectionIndex);
  QList<LauncherPreset> presets;
  if (m_setup.getLauncherPresets) {
    presets = m_setup.getLauncherPresets();
  }
  const QString collectionUuid =
      CollectionUtils::computeCollectionUuid(cfg.name, cfg.mediaDirectory);
  PlaylistExportSnapshot playlists;
  if (m_setup.getPlaylistManager) {
    playlists = snapshotPlaylistsForExport(m_setup.getPlaylistManager(), collectionUuid);
  }

  // Un-parented and shared_ptr-owned on purpose — see the m_activeWriter
  // member doc and ~KartManager.
  m_activeWriter = std::make_shared<KartWriter::Writer>();

  // Same one-way progress flow as runImport(); see the comment there. The
  // writer emits no per-entry signal, so kartProgressEntry is never sent.
  emit kartProgressStarted(tr("Exporting Kart"));

  connect(m_activeWriter.get(), &KartWriter::Writer::progress, this,
          &KartManager::kartProgressFraction, Qt::QueuedConnection);

  auto *watcher = new QFutureWatcher<ErrorUtils::Result<void>>(this);
  m_activeWatcher = watcher; // ~KartManager's bounded join reads this.
  connect(watcher, &QFutureWatcher<ErrorUtils::Result<void>>::finished, this,
          [this, watcher, outPath]() {
            const auto res = watcher->result();
            watcher->deleteLater();
            m_activeWatcher = nullptr;
            m_activeWriter.reset();
            if (res.isError()) {
              emit exportFailed(res.error());
              emit kartProgressFailed();
              showWarning(tr("Export Kart"), res.error().message);
            } else {
              emit kartProgressFinished();
              emit kartExported(outPath);
            }
          });

  // Inject the *connected* writer (m_activeWriter) so its progress signal and
  // cancel flag reach the writer actually serializing the collection. The
  // task holds its own shared_ptr copy — never `this` (abandon-safety, see
  // ~KartManager).
  auto writer = m_activeWriter;
  watcher->setFuture(QtConcurrent::run([writer, cfg, presets, playlists, outPath]() {
    return doExportKart(cfg, presets, playlists, outPath, writer.get());
  }));
}

} // namespace kart
