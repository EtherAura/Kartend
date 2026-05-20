#ifndef KARTMANAGER_H
#define KARTMANAGER_H

#include <functional>
#include <memory>
#include <QList>
#include <QObject>
#include <QString>

#include "collectionutils.h"
#include "errorutils.h"
#include "kartmerge.h"
#include "kartreader.h"

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

class ISettingsManager;

namespace KartWriter {
class Writer;
}

namespace kart {

struct KartManagerSetup {
  ISettingsManager *settingsManager = nullptr;
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
};

class KartManager : public QObject {
  Q_OBJECT

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
};

} // namespace kart

#endif
