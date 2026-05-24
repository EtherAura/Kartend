#ifndef KARTMANAGER_H
#define KARTMANAGER_H

#include <functional>
#include <memory>
#include <QList>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>

#include "collectionutils.h"
#include "errorutils.h"
#include "kartmerge.h"
#include "kartpreflight.h"
#include "kartreader.h"

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

class ISettingsManager;
struct ApplicationContext;

namespace KartWriter {
class Writer;
}

namespace kart {

/// Field+path pair returned by collectSuspiciousKartPaths.
using SuspiciousKartPath = QPair<QString, QString>;

/// Owner-provided confirmation hook for suspicious .kart paths during
/// interactive import (Kartend-s6mj). When the manifest carries
/// launcherPath / collectionIcon / placeholderArtwork entries outside the
/// safe-prefix allowlist (and not previously trusted), the manager calls
/// this closure with the offending (field, path) pairs and aborts the
/// import unless it returns true. UI side typically shows a QMessageBox
/// warning with default=Cancel.
using SuspiciousPathConfirmer = std::function<bool(const QList<SuspiciousKartPath> &)>;

/// Owner-provided preflight gate fired after KartReader::peekManifest succeeds
/// but before extraction begins. The UI layer typically wires a
/// KartPreflightDialog and returns true when the user accepts. Null in
/// headless contexts — the import skips the preflight and proceeds.
using PreflightConfirmer = std::function<bool(const KartPreflight::PreflightReport &)>;

struct KartManagerSetup {
  /// ctx is the canonical source for sibling managers — Kartend-phyc dropped
  /// the prior ISettingsManager *settingsManager field; reads now go through
  /// ctx->settingsManager() at call sites.
  const ApplicationContext *ctx = nullptr;
  std::function<QList<CollectionConfig> *()> getCollections;
  std::function<QList<LauncherPreset>()> getLauncherPresets;
  std::function<QWidget *()> getParentWindow;

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
};

/// Classify the imported manifest's externally-controlled path fields against
/// a safe-prefix allowlist (user home, /usr/bin, /usr/local/bin, /opt) plus
/// the @p trustedLauncherPaths set (typically already-configured launcher
/// paths from existing collections). Returns the (field, path) pairs that
/// fall outside any safe root so the caller can surface a confirmation
/// dialog before registering the collection.
[[nodiscard]] QList<SuspiciousKartPath>
collectSuspiciousKartPaths(const CollectionConfig &cfg, const QSet<QString> &trustedLauncherPaths);

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

  [[nodiscard]] ErrorUtils::Result<QString> finalizeImport(const KartReader::ExtractResult &result,
                                                           bool registerCollection,
                                                           const ConflictResolver &resolver);

  [[nodiscard]] ErrorUtils::Result<QString>
  importKart(const QString &kartPath, const QString &destDir, bool registerCollection);

  [[nodiscard]] ErrorUtils::Result<QString> importKartHeadless(const QString &kartPath,
                                                               const QString &destDir,
                                                               bool registerCollection,
                                                               MergeChoice headlessChoice);

  [[nodiscard]] ErrorUtils::Result<void> exportCollection(int collectionIndex,
                                                          const QString &outPath);

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
  std::unique_ptr<KartReader::Extractor> m_activeReader;
  std::unique_ptr<KartWriter::Writer> m_activeWriter;

  void runImport(const QString &kartPath, const QString &destDir);
  void runExport(int collectionIndex, const QString &outPath);

  /// Kartend-s6mj: gather launcher paths already configured in any saved
  /// collection so collectSuspiciousKartPaths can treat them as trusted —
  /// the user has effectively approved them before, so re-prompting on
  /// every re-import would be noise.
  [[nodiscard]] QSet<QString> previouslyTrustedLauncherPaths() const;
};

} // namespace kart

#endif
