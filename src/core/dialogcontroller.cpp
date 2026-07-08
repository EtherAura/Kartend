#include "dialogcontroller.h"
#include "createsmartplaylistdialog.h"
#include "editmetadatadialog.h"
#include "firstrunwizard.h"
#include "itemartworklinksdialog.h"
#include "kartmanager.h"
#include "kartmergedialog.h"
#include "kartprogressdialog.h"
#include "launcherchooserdialog.h"
#include "launchpreviewdialog.h"
#include "scrapercredentialsdialog.h"
#include "scraperesultdialog.h"
#include "settingsdialog.h"
#include <QDialog>
#include <QWidget>

DialogController::DialogController(QWidget *parentWidget)
    : QObject(parentWidget), m_parent(parentWidget) {}

DialogController::~DialogController() = default;

std::optional<SmartPlaylistEdit>
DialogController::runSmartPlaylistDialog(const QString &initialName,
                                         const std::optional<SmartFilter::Filter> &initialFilter,
                                         const SmartPlaylistCollectionEntries &collectionEntries) {
  CreateSmartPlaylistDialog dialog(m_parent);
  // Populate the ByCollection picker before setInitialFilter so the
  // initial-selection lookup can find the saved uuid in the combo.
  dialog.setCollectionList(collectionEntries);
  if (!initialName.isEmpty()) {
    dialog.setInitialName(initialName);
  }
  if (initialFilter.has_value()) {
    dialog.setInitialFilter(*initialFilter);
  }
  if (dialog.exec() != QDialog::Accepted) {
    return std::nullopt;
  }
  SmartPlaylistEdit out;
  out.name = dialog.name();
  out.filter = dialog.filter();
  return out;
}

void DialogController::runLaunchPreviewDialog(const QString &itemTitle, const QString &launcherName,
                                              const QString &filePath,
                                              const LaunchPreview &preview) {
  LaunchPreviewDialog dialog(m_parent);
  dialog.setPreview(itemTitle, launcherName, filePath, preview);
  dialog.exec();
}

std::optional<EditMetadataPayload>
DialogController::runEditMetadataDialog(const QString &itemTitle,
                                        const EditMetadataPayload &initial) {
  EditMetadataDialog dialog(m_parent);
  dialog.setItemTitle(itemTitle);
  dialog.setPayload(initial);
  if (dialog.exec() != QDialog::Accepted) {
    return std::nullopt;
  }
  return dialog.payload();
}

kart::ConflictResolution
DialogController::runKartMergeDialog(const QString &itemPath,
                                     const ItemMetadataStore::ItemMetadata &existing,
                                     const ItemMetadataStore::ItemMetadata &incoming) {
  KartMergeDialog dlg(itemPath, existing, incoming, m_parent);
  kart::ConflictResolution r;
  if (dlg.exec() == QDialog::Accepted) {
    r.choice = dlg.choice();
    r.policy = dlg.policy();
    r.applyToAll = dlg.applyToAll();
  } else {
    r.choice = kart::MergeChoice::Skip;
  }
  return r;
}

void DialogController::startKartProgressDialog(kart::KartManager *km, const QString &title) {
  // Kartend-h7xnr.1: sequential drop imports start the next operation while
  // the previous dialog may still be up in its finished "Close" state. Every
  // dialog stays connected to the same KartManager signals until it is
  // deleted, so a stale one would keep tracking the new operation (and the
  // dialogs would stack, one per dropped file). Close it — WA_DeleteOnClose
  // then deletes it, dropping its km connections.
  if (m_kartProgressDialog) {
    m_kartProgressDialog->close();
  }
  auto *dlg = new KartProgressDialog(title, m_parent);
  m_kartProgressDialog = dlg;
  dlg->setAttribute(Qt::WA_DeleteOnClose);
  connect(km, &kart::KartManager::kartProgressFraction, dlg, &KartProgressDialog::setFraction);
  connect(km, &kart::KartManager::kartProgressEntry, dlg, &KartProgressDialog::setEntryName);
  connect(km, &kart::KartManager::kartProgressFinished, dlg, &KartProgressDialog::markFinished);
  connect(km, &kart::KartManager::kartProgressFailed, dlg, &QDialog::reject);
  connect(dlg, &KartProgressDialog::cancelRequested, km,
          &kart::KartManager::cancelActiveKartOperation);
  dlg->show();
}

bool DialogController::anyScrapeResultDialogVisible() {
  return ScrapeResultDialog::isAnyInstanceVisible();
}

bool DialogController::anySettingsDialogVisible() {
  return SettingsDialog::isAnyInstanceVisible();
}

int DialogController::chooseLauncher(const QString &collectionName,
                                     const QStringList &launcherNames, int defaultIndex) {
  return LauncherChooserDialog::choose(m_parent ? m_parent->window() : nullptr, collectionName,
                                       launcherNames, defaultIndex);
}

void DialogController::runScraperCredentialsDialog(GeneralSettings *generalSettings,
                                                   ISettingsManager *settingsManager) {
  ScraperCredentialsDialog dialog(generalSettings, settingsManager, m_parent);
  dialog.exec();
}

std::optional<QHash<QString, QString>>
DialogController::runArtworkLinksDialog(const ItemArtworkLinksInput &in) {
  ItemArtworkLinksDialog dialog(m_parent);
  dialog.setItemTitle(in.itemTitle);
  dialog.setTypeRows(in.standardTypes, in.customTypes);
  dialog.setOverrides(in.overrides);
  if (!in.autoResolvedPaths.isEmpty()) {
    dialog.setAutoResolvedPaths(in.autoResolvedPaths);
  }
  if (!in.browseStartDir.isEmpty()) {
    dialog.setBrowseStartDirectory(in.browseStartDir);
  }
  if (dialog.exec() != QDialog::Accepted) {
    return std::nullopt;
  }
  return dialog.overrides();
}

DialogController::FirstRunWizardResult DialogController::runFirstRunWizard() {
  FirstRunWizard wizard(m_parent);
  wizard.exec();
  const auto result = wizard.result();
  return {result.accepted, result.pickedConfig};
}

DialogController::SettingsDialogFactory
DialogController::makeSettingsDialogFactory(const ApplicationContext *ctx) {
  return [ctx](QWidget *parent, const QList<CollectionConfig> &initialCollections, int initialIndex,
               std::function<void(const QList<CollectionConfig> &)> onCollectionSaved,
               std::function<void(int)> onRescanRequired) -> std::unique_ptr<ISettingsDialog> {
    auto dlg = std::make_unique<SettingsDialog>(parent, initialCollections, initialIndex, ctx);
    // Wire concrete Qt signals here — they cannot cross the neutral
    // ISettingsDialog interface. The dialog object is the connection
    // context so each connection is torn down with the dialog.
    if (onCollectionSaved) {
      QObject::connect(dlg.get(), &SettingsDialog::collectionSaved, dlg.get(),
                       std::move(onCollectionSaved));
    }
    if (onRescanRequired) {
      QObject::connect(dlg.get(), &SettingsDialog::rescanRequired, dlg.get(),
                       std::move(onRescanRequired));
    }
    return dlg;
  };
}
