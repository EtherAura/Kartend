#ifndef KARTMANAGER_H
#define KARTMANAGER_H

#include <functional>
#include <memory>
#include <QList>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>

#include "collection/collectionconfig.h"
#include "collection/launcherpreset.h"
#include "dialogrunners.h"
#include "errorutils.h"
#include "kartlaunchertrust.h"
#include "kartmerge.h"
#include "kartpreflight.h"
#include "kartreader.h"

QT_BEGIN_NAMESPACE
class QWidget;
class QFutureWatcherBase;
QT_END_NAMESPACE

class ISettingsManager;
class IPlaylistManager;
#include "applicationcontext_fwd.h"

namespace KartWriter {
class Writer;
}

namespace kart {

/// Owner-provided confirmation hook for an imported .kart's launcher
/// configuration (Kartend-s6mj, widened by Kartend-kxqqf). The manager calls
/// it with one (field, value) row per launcher field the manifest supplies —
/// launcher paths, cores and launch parameters — plus any icon / placeholder
/// path outside the safe-prefix allowlist, and aborts the import unless it
/// returns true. Rows carry their reason label in the field column. UI side
/// typically shows a QMessageBox warning with default=Cancel.
using SuspiciousPathConfirmer = std::function<bool(const QList<SuspiciousKartPath> &)>;

/// Owner-provided preflight gate fired after KartReader::peekManifest succeeds
/// but before extraction begins. The UI layer typically wires a
/// KartPreflightDialog and returns true when the user accepts. Null in
/// headless contexts — the import skips the preflight and proceeds.
using PreflightConfirmer = std::function<bool(const KartPreflight::PreflightReport &)>;

/// Field+description pair describing an imported launcher path that does
/// not resolve on the importing host. Used by the post-import reporter
/// (Kartend-jset) so the UI can list the offending paths with a
/// human-readable PathStatus description.
using MissingLauncherPathIssue = QPair<QString, QString>;

/// Kartend-jset: post-import reporter invoked when a freshly-registered
/// collection's launcher / additionalLauncher paths fail
/// PathUtils::checkLauncherPath on the importing host (typical when a
/// .kart was built on a different machine). The collection has already
/// been registered + saved by the time this fires; the reporter is purely
/// informational and offers the user a one-shot dialog. Null in headless
/// contexts — the issues are still logged via lcKartManager.
using MissingLauncherPathsReporter =
    std::function<void(const QString &collectionName, const QList<MissingLauncherPathIssue> &)>;

struct KartManagerSetup {
  /// ctx is the canonical source for sibling managers — Kartend-phyc dropped
  /// the prior ISettingsManager *settingsManager field; reads now go through
  /// ctx->settingsManager() at call sites.
  const ApplicationContext *ctx = nullptr;
  std::function<QList<CollectionConfig> *()> getCollections;
  std::function<QList<LauncherPreset>()> getLauncherPresets;
  std::function<QWidget *()> getParentWindow;
  /// Optional accessor for PlaylistManager (Kartend-kmj1). When provided,
  /// runExport bundles every playlist whose parentCollectionUuid matches
  /// the exported collection, and finalizeImport restores playlists onto
  /// the freshly-registered collection. Left null in headless contexts so
  /// existing call sites compile without the playlist round-trip.
  std::function<IPlaylistManager *()> getPlaylistManager;

  /// Owner-provided resolver for the interactive merge-conflict decision.
  /// Kartend-a3ir: KartManager previously #included kartmergedialog.h and
  /// constructed/exec()ed the dialog itself, which was the last data->ui
  /// edge in src/. With this seam the data layer takes a neutral
  /// std::function and the UI layer (typically MainWindow) supplies a
  /// closure that builds the dialog with the right parent and returns the
  /// user's choice. Left null in headless contexts — the manager falls
  /// back to MergeChoice::Skip if no resolver is wired.
  ConflictResolver mergeResolver;

  /// Kartend-s6mj: optional suspicious-path confirmation hook for
  /// interactive .kart imports. Left null in headless contexts; the
  /// import then proceeds (with the warning logged from finalizeImport).
  SuspiciousPathConfirmer suspiciousPathConfirmer;

  /// Kartend-fr4z: optional preflight confirmer fired right after
  /// peekManifest and before extraction. The UI layer wraps a
  /// KartPreflightDialog around this; returning false aborts the import
  /// without writing anything to disk. Left null in headless contexts.
  PreflightConfirmer preflightConfirmer;

  /// Kartend-jset: optional post-import reporter for launcher paths that
  /// don't exist on the importing host. Fires once after a successful
  /// finalizeImport when any launcher path returned PathStatus != OK. The
  /// import has already committed; this is informational only. Null in
  /// headless contexts — the issues still get logged.
  MissingLauncherPathsReporter missingLauncherPathsReporter;

  /// Kartend-sqoq0: generic stock-modal runners for the import/export file
  /// pickers and failure warnings. Null runners fall back to direct
  /// QFileDialog / QMessageBox construction parented on getParentWindow(),
  /// so headless contexts behave exactly as before.
  DialogRunners dialogs;
};

// SuspiciousKartPath, the launcher-trust findings and the collectors that
// produce them live in kartlaunchertrust.h (included above), so KartPreflight
// can name them without depending on the manager.

class KartManager : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(KartManager)

public:
  explicit KartManager(QObject *parent = nullptr);
  ~KartManager() override;

  void setupReferences(const KartManagerSetup &setup);

  void importInteractive();

  void exportCollectionInteractive(int collectionIndex);

  [[nodiscard]] ErrorUtils::Result<KartReader::ExtractResult> extractKart(const QString &kartPath,
                                                                          const QString &destDir);

  /// Kartend-qbfk1: directory name a bundle's content is extracted into,
  /// derived from the manifest name. Path separators become '_', Windows
  /// segment quirks (trailing dots/spaces, reserved device names) are
  /// defused, and an empty or fully-consumed name falls back to "kart" —
  /// the same fallback importInteractive's suggested destination uses.
  [[nodiscard]] static QString importSubdirNameFor(const QString &bundleName);

  /// Kartend-qbfk1: every import entry point extracts into a FRESH
  /// subdirectory of the caller-chosen directory, never into that directory
  /// itself — a hostile bundle needs no traversal to plant
  /// '.config/autostart/…' when the destination is $HOME. Peeks the
  /// manifest for the bundle name, appends importSubdirNameFor()'s
  /// derivation to @p parentDir, and refuses when that subdirectory already
  /// exists with content (KartReader::extractTo re-checks emptiness as the
  /// hard backstop). Returns the destination extractTo should receive.
  [[nodiscard]] static ErrorUtils::Result<QString>
  deriveImportDestination(const QString &kartPath, const QString &parentDir);

  [[nodiscard]] ErrorUtils::Result<QString> finalizeImport(const KartReader::ExtractResult &result,
                                                           bool registerCollection,
                                                           const ConflictResolver &resolver);

  [[nodiscard]] ErrorUtils::Result<QString>
  importKart(const QString &kartPath, const QString &destDir, bool registerCollection);

  /// Kartend-h7xnr.1: asynchronous import through the same worker path as
  /// the menu-driven flow — QtConcurrent extraction, kartProgress* dialog
  /// signals, and cooperative cancel. The drop-handler drain uses this so a
  /// dropped .kart never blocks the GUI thread; the synchronous importKart
  /// entry above stays for tests and headless callers. Registers the
  /// collection on success (matching the old drop path's importKart(...,
  /// true)). One operation runs at a time — callers sequence follow-up
  /// imports off the collectionImported / importFailed terminal signals.
  void importKartAsync(const QString &kartPath, const QString &destDir);

  /// True while an import/export future is in flight (m_activeWatcher set).
  /// Lets owners defer a new operation instead of clobbering the active
  /// reader/writer the running QtConcurrent task captured by raw pointer.
  [[nodiscard]] bool operationInFlight() const { return m_activeWatcher != nullptr; }

  /// Headless (no-confirmer) import. Because there is no interactive
  /// suspicious-path gate, a manifest whose launcherPath resolves INSIDE the
  /// extracted tree (a self-bundled executable) is refused unless
  /// @p allowUntrustedLauncher is true — the CLI exposes this as
  /// --allow-untrusted-launcher (Kartend-u8wf0). Out-of-allowlist paths that
  /// point elsewhere are still logged but not blocked (finalizeImport's audit).
  [[nodiscard]] ErrorUtils::Result<QString>
  importKartHeadless(const QString &kartPath, const QString &destDir, bool registerCollection,
                     MergeChoice headlessChoice, bool allowUntrustedLauncher = false);

  /// Serialize a collection to a .kart. When `writer` is non-null it runs on
  /// that (signal-connected, cancellable) writer — runExport() injects
  /// m_activeWriter so progress/cancel reach the real work. Callers that don't
  /// need progress (tests, headless) leave it null and a local writer is used.
  [[nodiscard]] ErrorUtils::Result<void> exportCollection(int collectionIndex,
                                                          const QString &outPath,
                                                          KartWriter::Writer *writer = nullptr);

  /// Cancel the in-flight import/export, if any. Owner-facing slot: the
  /// progress dialog's cancelRequested signal is connected here so the data
  /// layer no longer needs to know the dialog type.
public slots:
  void cancelActiveKartOperation();

signals:
  void collectionImported(const QString &name);
  void importFailed(ErrorUtils::ErrorContext ctx);
  void kartExported(const QString &kartPath);
  void exportFailed(ErrorUtils::ErrorContext ctx);

  // Progress-dialog lifecycle, emitted so the owner (MainWindow) can create
  // and drive a KartProgressDialog without KartManager — a data-layer
  // manager — #including the ui/ dialog header. One import/export runs at a
  // time, so a single in-flight dialog is implied.
  /// A long-running kart operation began; @p title is the dialog caption.
  void kartProgressStarted(const QString &title);
  /// Completion fraction in [0, 1] for the active operation.
  void kartProgressFraction(double fraction);
  /// Relative path of the entry currently being extracted (import only).
  void kartProgressEntry(const QString &relPath);
  /// The active operation finished successfully — dialog should show "done".
  void kartProgressFinished();
  /// The active operation failed or was cancelled — dialog should close.
  void kartProgressFailed();

private:
  KartManagerSetup m_setup;
  /// The active import/export workhorses. shared_ptr, NOT unique_ptr, and
  /// deliberately un-parented: the QtConcurrent task body holds its own
  /// shared_ptr copy (value/shared captures only — no `this`, no m_setup
  /// closures), so a destructor whose bounded join times out can safely
  /// ABANDON the task; the leaked task keeps its Writer/Extractor alive on
  /// its own until it eventually returns (or process exit reaps it).
  std::shared_ptr<KartReader::Extractor> m_activeReader;
  std::shared_ptr<KartWriter::Writer> m_activeWriter;
  /// The QFutureWatcher of an in-flight export/import (null when idle).
  /// Kartend audit jpit3 introduced the destructor join so a close during a
  /// long .kart export/import can't free state the task still touches. The
  /// task bodies now own their state by value/shared_ptr, so the join is
  /// BOUNDED (2s, the DatabaseManager/CacheManager teardown norm) and an
  /// overrunning task is abandoned instead of hanging the GUI — see
  /// ~KartManager. Set on launch, cleared in each finished slot; a child of
  /// `this`, so no continuation ever fires post-dtor. Base-class pointer so
  /// the header needs no template type.
  QFutureWatcherBase *m_activeWatcher = nullptr;

  /// @p preflightConfirmed marks an import the user already accepted in the
  /// preflight dialog. That dialog lists the same launcher rows, so re-asking
  /// about them post-extract would be two prompts for one decision; only the
  /// escalation preflight could not have seen — a payload the bundle ships,
  /// which needs the extraction root to detect — still stops for consent.
  void runImport(const QString &kartPath, const QString &destDir, bool preflightConfirmed = false);
  void runExport(int collectionIndex, const QString &outPath);

  /// Kartend-s6mj: gather launcher paths already configured in any saved
  /// collection so collectSuspiciousKartPaths can treat them as trusted —
  /// the user has effectively approved them before, so re-prompting on
  /// every re-import would be noise.
  [[nodiscard]] QSet<QString> previouslyTrustedLauncherPaths() const;

  /// Kartend-kxqqf: the (field, value) rows the interactive confirmer is
  /// handed for @p cfg — every launcher field the bundle supplies plus any
  /// icon / placeholder path outside the allowlist. Empty only when the
  /// bundle asks for nothing executable, which is the one case that imports
  /// without a prompt. @p extractedRoot enables the bundled-payload
  /// escalation; pass an empty string before extraction.
  /// @p onlyBundledPayloads narrows the rows to that escalation alone, for a
  /// caller whose user has already seen the rest (see runImport).
  [[nodiscard]] QList<SuspiciousKartPath>
  launcherConfirmationRows(const CollectionConfig &cfg, const QString &extractedRoot,
                           bool onlyBundledPayloads = false) const;

  /// Kartend-sqoq0: warn via the owner-supplied runner when wired, else the
  /// stock QMessageBox parented on getParentWindow() (shown only when a
  /// parent exists, matching the pre-runner behavior).
  void showWarning(const QString &title, const QString &text);
};

} // namespace kart

#endif
