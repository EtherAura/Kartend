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

signals:
  void collectionImported(const QString &name);
  void importFailed(ErrorUtils::ErrorContext ctx);
  void kartExported(const QString &kartPath);
  void exportFailed(ErrorUtils::ErrorContext ctx);

private:
  KartManagerSetup m_setup;
  std::unique_ptr<KartReader::Extractor> m_activeReader;
  std::unique_ptr<KartWriter::Writer> m_activeWriter;

  void runImport(const QString &kartPath, const QString &destDir);
  void runExport(int collectionIndex, const QString &outPath);

  ConflictResolver makeInteractiveResolver(QWidget *parent);
};

} // namespace kart

#endif
