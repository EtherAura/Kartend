#ifndef DIALOGCONTROLLER_H
#define DIALOGCONTROLLER_H

#include "collection/collectionconfig.h"                       // CollectionConfig
#include "controllers/detailspanemanager/detailspanemanager.h" // ItemArtworkLinksInput typedef
#include "interactionmanager.h" // SmartPlaylistEdit, CustomFieldList typedefs
#include "isettingsdialog.h"    // ISettingsDialog (factory return type)
#include "kartmerge.h"          // kart::ConflictResolution, ItemMetadata
#include <functional>
#include <memory>
#include <optional>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace kart {
class KartManager;
}

class ISettingsManager;
struct GeneralSettings;

/// Centralizes dialog construction so MainWindow doesn't need to #include
/// every dialog header itself. Owned by MainWindow; takes the parent
/// widget for dialog parenting / modality.
///
/// Phase 1: just the two dialogs InteractionManagerSetup needs as
/// closures (SmartPlaylist + CustomFields). Subsequent phases move the
/// rest of the dialog launches off MainWindow.
class DialogController : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(DialogController)
public:
  explicit DialogController(QWidget *parentWidget);
  ~DialogController() override;

  /// Runs CreateSmartPlaylistDialog modally, seeded with the given
  /// name + filter. Returns the edited values, or nullopt on cancel.
  [[nodiscard]] std::optional<SmartPlaylistEdit>
  runSmartPlaylistDialog(const QString &initialName,
                         const std::optional<SmartFilter::Filter> &initialFilter);

  /// Runs CustomFieldsDialog modally, seeded with the given item title
  /// + existing custom fields. Returns the edited fields, or nullopt on
  /// cancel.
  [[nodiscard]] std::optional<ItemMetadataStore::CustomFieldList>
  runCustomFieldsDialog(const QString &itemTitle,
                        const ItemMetadataStore::CustomFieldList &initial);

  /// Runs KartMergeDialog modally and returns the resolution. On reject
  /// the resolution carries MergeChoice::Skip.
  [[nodiscard]] kart::ConflictResolution
  runKartMergeDialog(const QString &itemPath, const ItemMetadataStore::ItemMetadata &existing,
                     const ItemMetadataStore::ItemMetadata &incoming);

  /// Creates the long-lived KartProgressDialog and wires it to the given
  /// KartManager (progress signals + cancellation routing). Dialog
  /// deletes itself on close (WA_DeleteOnClose).
  void startKartProgressDialog(kart::KartManager *km, const QString &title);

  /// True iff a ScrapeResultDialog or SettingsDialog is currently showing.
  /// Wraps the dialog classes' static visibility tracking so callers don't
  /// need their headers.
  [[nodiscard]] static bool anyScrapeResultDialogVisible();
  [[nodiscard]] static bool anySettingsDialogVisible();

  /// Runs the modal LauncherChooserDialog. Returns the chosen index or
  /// -1 on cancel.
  [[nodiscard]] int chooseLauncher(const QString &collectionName, const QStringList &launcherNames,
                                   int defaultIndex);

  /// Runs ScraperCredentialsDialog modally.
  void runScraperCredentialsDialog(GeneralSettings *generalSettings,
                                   ISettingsManager *settingsManager);

  /// Runs ItemArtworkLinksDialog modally and returns the user's
  /// overrides, or nullopt on cancel.
  [[nodiscard]] std::optional<QHash<QString, QString>>
  runArtworkLinksDialog(const ItemArtworkLinksInput &in);

  /// Result of the first-run wizard. accepted=true when the user
  /// clicked Finish; pickedConfig may still be empty if they skipped the
  /// media-folder page.
  struct FirstRunWizardResult {
    bool accepted = false;
    CollectionConfig pickedConfig;
  };
  [[nodiscard]] FirstRunWizardResult runFirstRunWizard();

  /// Returns the factory closure SettingsDialogController uses to
  /// construct + signal-wire SettingsDialog. The factory's body lives
  /// here so this TU is the only place that knows the concrete dialog
  /// class; the controller drives it through ISettingsDialog.
  using SettingsDialogFactory = std::function<std::unique_ptr<ISettingsDialog>(
      QWidget *, const QList<CollectionConfig> &, int,
      std::function<void(const QList<CollectionConfig> &)>, std::function<void(int)>)>;
  [[nodiscard]] static SettingsDialogFactory makeSettingsDialogFactory();

private:
  QWidget *m_parent = nullptr;
};

#endif // DIALOGCONTROLLER_H
