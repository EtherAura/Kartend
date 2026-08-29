#ifndef DIALOGCONTROLLER_H
#define DIALOGCONTROLLER_H

#include "collection/collectionconfig.h" // CollectionConfig
#include "detailspanemanager.h"          // ItemArtworkLinksInput typedef
#include "interactionmanager.h"          // SmartPlaylistEdit, EditMetadataDialogRunner typedefs
#include "isettingsdialog.h"             // ISettingsDialog (factory return type)
#include "kartmerge.h"                   // kart::ConflictResolution, ItemMetadata
#include <functional>
#include <memory>
#include <optional>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

class KartProgressDialog;

namespace kart {
class KartManager;
}

struct ApplicationContext;
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
  /// name + rule set. `collectionEntries` populates each rule's
  /// ByCollection picker — pass the live (displayName, uuid) list from the
  /// caller's CollectionConfig array. Returns the edited values, or nullopt
  /// on cancel.
  [[nodiscard]] std::optional<SmartPlaylistEdit>
  runSmartPlaylistDialog(const QString &initialName,
                         const std::optional<SmartFilter::FilterSet> &initialFilterSet,
                         const SmartPlaylistCollectionEntries &collectionEntries);

  /// Runs EditMetadataDialog modally, seeded with the given item title
  /// + existing payload (notes, tags, rating, source URL, custom fields).
  /// Returns the edited payload, or nullopt on cancel.
  [[nodiscard]] std::optional<EditMetadataPayload>
  runEditMetadataDialog(const QString &itemTitle, const EditMetadataPayload &initial);

  /// Pops a modal LaunchPreviewDialog seeded with the supplied preview.
  /// Read-only — no user input is round-tripped back to the caller.
  void runLaunchPreviewDialog(const QString &itemTitle, const QString &launcherName,
                              const QString &filePath, const LaunchPreview &preview);

  /// Runs KartMergeDialog modally and returns the resolution. On reject
  /// the resolution carries MergeChoice::Skip.
  [[nodiscard]] kart::ConflictResolution
  runKartMergeDialog(const QString &itemPath, const ItemMetadataStore::ItemMetadata &existing,
                     const ItemMetadataStore::ItemMetadata &incoming);

  /// Creates the long-lived KartProgressDialog and wires it to the given
  /// KartManager (progress signals + cancellation routing). Dialog
  /// deletes itself on close (WA_DeleteOnClose). A previous progress dialog
  /// still open (e.g. finished in its "Close" state while a sequential
  /// drop-import chain starts the next operation, Kartend-h7xnr.1) is
  /// closed first so stacked stale dialogs don't keep tracking the new
  /// operation's signals.
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
  /// class; the controller drives it through ISettingsDialog. @p ctx is
  /// captured into the closure and handed to each SettingsDialog so its
  /// sibling-manager access goes through ApplicationContext (Kartend-qjtz)
  /// rather than an IMainWindow forwarder.
  using SettingsDialogFactory = std::function<std::unique_ptr<ISettingsDialog>(
      QWidget *, const QList<CollectionConfig> &, int,
      std::function<void(const QList<CollectionConfig> &)>, std::function<void(int)>)>;
  [[nodiscard]] static SettingsDialogFactory
  makeSettingsDialogFactory(const ApplicationContext *ctx);

private:
  QWidget *m_parent = nullptr;
  /// Weak handle to the currently-showing KartProgressDialog so a new
  /// startKartProgressDialog can close a stale predecessor. The dialog owns
  /// its own lifetime (WA_DeleteOnClose); QPointer nulls itself on delete.
  QPointer<KartProgressDialog> m_kartProgressDialog;
};

#endif // DIALOGCONTROLLER_H
