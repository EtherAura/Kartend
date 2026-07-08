#ifndef SETTINGSDIALOGCONTROLLER_H
#define SETTINGSDIALOGCONTROLLER_H

#include "applicationcontext_fwd.h"
#include "collection/collectionconfig.h"
#include "dialogrunners.h"
#include "isettingsdialog.h" // for SettingsPage + ISettingsDialog
#include <functional>
#include <memory>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

class QLabel;
class QScrollArea;
class QWidget;
class IDetailsPaneManager;
class IScrollManager;
class INavigationManager;
class IArtworkManager;
class ICacheManager;
class IDatabaseManager;

struct SettingsDialogContext {
  QWidget *parent = nullptr;
  QList<CollectionConfig> *collections = nullptr;
  int *currentCollectionIndex = nullptr;
  IDetailsPaneManager *detailsPaneManager = nullptr;
  // Kartend-h1l8f: stays the IScrollManager facade — the controller spans
  // two scroll roles (grid layout + lifecycle cleanup) through one stored
  // pointer AND uses it as the QObject lifetime guard for
  // TimerUtils::singleShotGuarded, which the plain role classes can't be.
  IScrollManager *scrollManager = nullptr;
  INavigationManager *navigationManager = nullptr;
  // needed so the dialog controller can subscribe to post-scan summary
  // signals and display the "X of Y items added" confirmation box when a
  // newly-added collection finishes its first scan.
  IDatabaseManager *databaseManager = nullptr;

  // MainWindow chrome the post-save refresh touches directly: the items-page
  // breadcrumb label (plain-text title fallback until NavigationManager
  // rebuilds the real breadcrumb) and the grid scroll area (scrollbar
  // visibility re-apply). Injected by the owner instead of located with a
  // stringly-typed findChild() so an objectName rename in mainwindow.ui
  // breaks compilation at the population site rather than silently skipping
  // the refresh. Null is tolerated (headless callers): the refresh steps
  // that need a widget just no-op.
  QLabel *itemsTitleLabel = nullptr;
  QScrollArea *itemScrollArea = nullptr;

  // Factory that builds the concrete settings dialog and wires its
  // collectionSaved / rescanRequired signals to the supplied callbacks.
  // Supplied by MainWindow (which legally #includes the concrete
  // SettingsDialog), so the controller never names the dialog type directly:
  // it constructs the dialog through this hook and drives it via the neutral
  // ISettingsDialog interface. The returned dialog is owned by the caller.
  // The Qt signal connections must be made on the concrete type, hence they
  // live in the factory rather than here.
  std::function<std::unique_ptr<ISettingsDialog>(
      QWidget *parent, const QList<CollectionConfig> &initialCollections, int initialIndex,
      std::function<void(const QList<CollectionConfig> &)> onCollectionSaved,
      std::function<void(int)> onRescanRequired)>
      createSettingsDialog;

  /// Optional caller hint for the navigation row the dialog should land on
  /// when it opens. Default leaves the dialog at its standard first row.
  /// See SettingsPage for the curated set of public targets.
  SettingsPage initialPage = SettingsPage::Default;

  /// Kartend-sqoq0: owner-supplied stock-Qt-modal runners. The controller
  /// uses info/warn for the async "Collection Added" scan-summary boxes
  /// instead of constructing QMessageBox directly. Null runners fall back
  /// to the direct construction, so headless callers are unaffected.
  DialogRunners dialogs;
};

/**
 * @brief Ui-layer orchestrator for the settings dialog (Kartend-q8p29).
 *
 * Owns the open-dialog flow that used to live on SettingsManager
 * (modules/data/settings/settingsdialogcontroller.cpp): constructing the
 * dialog through the caller-supplied factory, diffing the accepted
 * collection list against the pre-dialog snapshot, persisting via
 * ISettingsManager::saveCollections, and driving the post-save UI refresh
 * (window title, scrollbars, sidebar, scroll-layout branch, reloads) plus
 * the async "Collection Added" scan-summary message boxes. Relocated to
 * src/ui/controllers/ following the DetailsPaneManager precedent — it
 * orchestrates QDialog/QMessageBox/QScrollArea, which is ui-layer work,
 * not persistence.
 *
 * Siblings (ArtworkManager, CacheManager, SettingsManager) are reached
 * through the injected ApplicationContext — the sanctioned pattern for
 * ui-layer dialog drivers (Kartend-qjtz).
 */
class SettingsDialogController : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(SettingsDialogController)
public:
  explicit SettingsDialogController(const ApplicationContext *ctx, QObject *parent = nullptr)
      : QObject(parent), m_ctx(ctx) {}
  ~SettingsDialogController() override = default;

  void openSettingsDialog(const SettingsDialogContext &context);

private slots:
  /// Handles QueryManager's post-scan summary (forwarded via DatabaseManager).
  /// If the scan's UUID matches a collection the user just added through the
  /// settings dialog, pops a "Collection Added — X of Y items" message box.
  void onCollectionScanSummary(const QString &collectionUuid, int itemsScanned, int itemsApplied,
                               bool success);

private:
  void handleReloadRequired(const QList<CollectionConfig> &collections,
                            const QList<CollectionConfig> &newCollections,
                            const QList<CollectionConfig> &originalCollections,
                            int viewingCollectionIndex, IDetailsPaneManager *detailsPaneManager,
                            IScrollManager *scrollManager, INavigationManager *navigationManager,
                            IArtworkManager *artworkManager, ICacheManager *cacheManager,
                            int currentCollectionIndex);

  void handleLayoutChanges(QWidget *parent, QLabel *itemsTitleLabel, QScrollArea *itemScrollArea,
                           const QList<CollectionConfig> &collections, int viewingCollectionIndex,
                           bool titleChangedForView, bool scrollbarChangedForView,
                           bool sidebarModeChangedForView, bool gridWidthChangedForView,
                           bool spacingChangedForView, bool alignmentChangedForView,
                           bool fontSizeChangedForView, bool hideTitlesChangedForView,
                           IDetailsPaneManager *detailsPaneManager, IScrollManager *scrollManager,
                           IArtworkManager *artworkManager, int currentCollectionIndex);

  // ctx is the single source of truth for sibling managers (SettingsManager,
  // ArtworkManager, CacheManager).
  const ApplicationContext *m_ctx = nullptr;

  // UUIDs of collections the user just added through the settings
  // dialog that are still waiting for their first scan-summary signal. Value is
  // the collection's display name so the message box can reference it.
  QHash<QString, QString> m_pendingAddSummaries;
  // Parent widget for the confirmation message box. QPointer so we don't
  // outlive it if the dialog is destroyed before the async scan finishes.
  QPointer<QWidget> m_pendingAddSummaryParent;
  // Kartend-sqoq0: owner-supplied stock-modal runners captured from the
  // SettingsDialogContext at openSettingsDialog time, used by the async
  // scan-summary message boxes. Null runners fall back to direct
  // QMessageBox construction (the pre-sqoq0 behavior).
  DialogRunners m_dialogRunners;
};

#endif // SETTINGSDIALOGCONTROLLER_H
