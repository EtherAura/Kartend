#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include "isettingsmanager.h"
#include <QHash>
#include <QPointer>
#include <QString>

class QFile;
class SessionManager;
struct ApplicationContext;

class SettingsManager : public ISettingsManager {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(SettingsManager)
public:
  explicit SettingsManager(const ApplicationContext *ctx, QObject *parent = nullptr);
  ~SettingsManager() override;

  void loadCollections(QList<CollectionConfig> &collections) override;
  // emits collectionsModified so observers (toolbar type filter, hierarchy
  // cache, sidebar summary) refresh after any save — not just
  // settings-dialog-driven ones. Non-const for that reason; the disk write
  // itself doesn't mutate SettingsManager state.
  ErrorUtils::Result<void> saveCollections(const QList<CollectionConfig> &collections) override;
  void openSettingsDialog(const SettingsDialogContext &context) override;
  void loadGeneralSettings(GeneralSettings &settings) override;
  ErrorUtils::Result<void> saveGeneralSettings(const GeneralSettings &settings) override;
  void setLastSelectedItem(int collectionIndex, int itemIndex) override;
  [[nodiscard]] int getLastSelectedItem(int collectionIndex) const override;

  void handleReloadRequired(const QList<CollectionConfig> &collections,
                            const QList<CollectionConfig> &newCollections,
                            const QList<CollectionConfig> &originalCollections,
                            int viewingCollectionIndex, IDetailsPaneManager *detailsPaneManager,
                            IScrollManager *scrollManager, INavigationManager *navigationManager,
                            IArtworkManager *artworkManager, ICacheManager *cacheManager,
                            int currentCollectionIndex) override;

  void handleLayoutChanges(QWidget *parent, const QList<CollectionConfig> &collections,
                           int viewingCollectionIndex, bool titleChangedForView,
                           bool scrollbarChangedForView, bool sidebarModeChangedForView,
                           bool gridWidthChangedForView, bool spacingChangedForView,
                           bool alignmentChangedForView, bool fontSizeChangedForView,
                           bool hideTitlesChangedForView, IDetailsPaneManager *detailsPaneManager,
                           IScrollManager *scrollManager, IArtworkManager *artworkManager,
                           int currentCollectionIndex) override;

private slots:
  /// Handles QueryManager's post-scan summary (forwarded via DatabaseManager).
  /// If the scan's UUID matches a collection the user just added through the
  /// settings dialog, pops a "Collection Added — X of Y items" message box.

  void onCollectionScanSummary(const QString &collectionUuid, int itemsScanned, int itemsApplied,
                               bool success);

private:
  // ctx is the single source of truth for sibling managers (SessionManager,
  // ArtworkManager, CacheManager).
  const ApplicationContext *m_ctx = nullptr;
  GeneralSettings m_generalSettings;

  // UUIDs of collections the user just added through the settings
  // dialog that are still waiting for their first scan-summary signal. Value is
  // the collection's display name so the message box can reference it.
  QHash<QString, QString> m_pendingAddSummaries;
  // Parent widget for the confirmation message box. QPointer so we don't
  // outlive it if the dialog is destroyed before the async scan finishes.
  QPointer<QWidget> m_pendingAddSummaryParent;

  // Last successfully-saved collection list, used as the diff baseline for
  // the per-domain *Changed signals emitted from saveCollections(). Updated
  // atomically at the end of a successful save (and seeded from
  // loadCollections so the first save after launch has a sensible baseline
  // instead of an empty one that fires every signal at once).
  QList<CollectionConfig> m_lastSavedCollections;

  void finalizeCollections(const QHash<QString, CollectionConfig> &tempCollections,
                           QList<CollectionConfig> &collections, const bool &needsRewrite);
};

#endif // SETTINGSMANAGER_H
