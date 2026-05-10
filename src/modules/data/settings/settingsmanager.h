#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include "collectionutils.h"
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>

class QWidget;
class QFile;
class DetailsPaneManager;
class ScrollManager;
class NavigationManager;
class SessionManager;
class ArtworkManager;
class CacheManager;
class DatabaseManager;
struct ApplicationContext;

struct SettingsDialogContext {
  QWidget *parent = nullptr;
  QList<CollectionConfig> *collections = nullptr;
  int *currentCollectionIndex = nullptr;
  DetailsPaneManager *detailsPaneManager = nullptr;
  ScrollManager *scrollManager = nullptr;
  NavigationManager *navigationManager = nullptr;
  // needed so the dialog controller can subscribe to post-scan
  // summary signals and display the "X of Y items added" confirmation box
  // when a newly-added collection finishes its first scan.
  DatabaseManager *databaseManager = nullptr;
};

class SettingsManager : public QObject {
  Q_OBJECT
public:
  explicit SettingsManager(const ApplicationContext *ctx, QObject *parent = nullptr);
  ~SettingsManager();

  void loadCollections(QList<CollectionConfig> &collections);
  // emits collectionsModified so observers (toolbar type
  // filter, hierarchy cache, sidebar summary) refresh after any save —
  // not just settings-dialog-driven ones. Non-const for that reason; the
  // disk write itself doesn't mutate SettingsManager state.
  void saveCollections(const QList<CollectionConfig> &collections);
  void setupDefaultCollections(QList<CollectionConfig> &collections);
  void openSettingsDialog(const SettingsDialogContext &context);
  auto loadGeneralSettings(GeneralSettings &settings) -> void;
  auto saveGeneralSettings(const GeneralSettings &settings) -> void;
  auto setLastSelectedItem(int collectionIndex, int itemIndex) -> void;
  [[nodiscard]] auto getLastSelectedItem(int collectionIndex) const -> int;

signals:
  void collectionsModified();

public:
  auto handleReloadRequired(const QList<CollectionConfig> &collections,
                            const QList<CollectionConfig> &newCollections,
                            const QList<CollectionConfig> &originalCollections,
                            int viewingCollectionIndex, DetailsPaneManager *detailsPaneManager,
                            ScrollManager *scrollManager, NavigationManager *navigationManager,
                            ArtworkManager *artworkManager, CacheManager *cacheManager,
                            int currentCollectionIndex) -> void;

  auto handleLayoutChanges(QWidget *parent, const QList<CollectionConfig> &collections,
                           int viewingCollectionIndex, bool titleChangedForView,
                           bool scrollbarChangedForView, bool sidebarModeChangedForView,
                           bool gridWidthChangedForView, bool spacingChangedForView,
                           bool alignmentChangedForView, bool fontSizeChangedForView,
                           bool hideTitlesChangedForView, DetailsPaneManager *detailsPaneManager,
                           ScrollManager *scrollManager, ArtworkManager *artworkManager,
                           int currentCollectionIndex) -> void;

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

  void finalizeCollections(const QHash<QString, CollectionConfig> &tempCollections,
                           QList<CollectionConfig> &collections, const bool &needsRewrite);
};

#endif // SETTINGSMANAGER_H