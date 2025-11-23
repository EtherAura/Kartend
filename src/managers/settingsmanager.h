#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include "collectionconfig.h"
#include "extensionutils.h"
#include <QObject>

class QWidget;
class QFile;
class SidebarManager;
class ScrollManager;
class NavigationManager;

class SettingsManager : public QObject {
  Q_OBJECT
public:
  explicit SettingsManager(QObject *parent = nullptr);
  ~SettingsManager();

  static auto getConfigPath() -> QString;
  void loadCollections(QList<CollectionConfig> &collections) const;
  void saveCollections(const QList<CollectionConfig> &collections) const;
  void setupDefaultCollections(QList<CollectionConfig> &collections);
  static auto loadMainScreenSettings(MainScreenConfig &config) -> void;
  static auto saveMainScreenSettings(const MainScreenConfig &config) -> void;
  static auto expandConfigVariables(const QString &input,
                                    const QString &collectionName) -> QString;
  void openSettingsDialog(QWidget *parent, QList<CollectionConfig> &collections,
                          int &currentCollectionIndex,
                          SidebarManager *sidebarManager,
                          ScrollManager *scrollManager,
                          NavigationManager *navigationManager);
  static auto
  applyHorizontalScrollbarSetting(QWidget *parent, int collectionIndex,
                                  const QList<CollectionConfig> &collections)
      -> void;
  static auto
  applyVerticalScrollbarSetting(QWidget *parent, int collectionIndex,
                                const QList<CollectionConfig> &collections)
      -> void;
  auto loadGeneralSettings(GeneralSettings &settings) -> void;
  auto saveGeneralSettings(const GeneralSettings &settings) -> void;
  auto setLastSelectedItem(int collectionIndex, int itemIndex) -> void;
  auto getLastSelectedItem(int collectionIndex) const -> int;

private:
  GeneralSettings m_generalSettings;

  // Helper methods to reduce cognitive complexity
  // loadCollections helper methods
  static auto parseConfigFile(QFile &file,
                              QHash<QString, CollectionConfig> &tempCollections,
                              bool &needsRewrite) -> bool;
  static auto processGeneralConfigLine(const QString &line) -> void;
  static auto processConfigLine(const QString &line,
                                CollectionConfig &currentCollection,
                                bool &needsRewrite) -> void;
  static auto setCollectionExtensions(const QString &value,
                                      CollectionConfig &collection,
                                      bool &needsRewrite) -> void;
  static auto setCollectionGridWidth(const QString &value,
                                     CollectionConfig &collection) -> void;
  static auto setCollectionSpacing(const QString &value, int &spacing) -> void;
  static auto setCollectionItemWidth(const QString &value,
                                     CollectionConfig &collection) -> void;
  static auto setCollectionItemHeight(const QString &value,
                                      CollectionConfig &collection) -> void;
  static auto setCollectionFontSize(const QString &value,
                                    CollectionConfig &collection) -> void;
  void
  finalizeCollections(const QHash<QString, CollectionConfig> &tempCollections,
                      QList<CollectionConfig> &collections,
                      bool &needsRewrite) const;
  static auto processSubcollection(const QString &sectionName,
                                   CollectionConfig &collection,
                                   QList<CollectionConfig> &collections)
      -> void;
  static auto
  findParentCollectionIndex(const QStringList &parts,
                            const QString &immediateParentName,
                            const QList<CollectionConfig> &collections) -> int;

  // openSettingsDialog helper methods

  static auto
  detectChanges(const QList<CollectionConfig> &newCollections,
                const QList<CollectionConfig> &originalCollections,
                int viewingCollectionIndex, bool &needsReload,
                bool &gridWidthChangedForView, bool &alignmentChangedForView,
                bool &spacingChangedForView, bool &scrollbarChangedForView,
                bool &sidebarModeChangedForView, bool &titleChangedForView,
                bool &fontSizeChangedForView, bool &hideTitlesChangedForView)
      -> bool;
  static auto handleReloadRequired(
      QList<CollectionConfig> &collections,
      const QList<CollectionConfig> &newCollections,
      const QList<CollectionConfig> &originalCollections,
      int viewingCollectionIndex, SidebarManager *sidebarManager,
      ScrollManager *scrollManager,
      NavigationManager *navigationManager, int currentCollectionIndex) -> void;
  static auto handleLayoutChanges(
      QWidget *parent, QList<CollectionConfig> &collections,
      int viewingCollectionIndex, bool titleChangedForView,
      bool scrollbarChangedForView, bool sidebarModeChangedForView,
      bool gridWidthChangedForView, bool spacingChangedForView,
      bool alignmentChangedForView, bool fontSizeChangedForView,
      bool hideTitlesChangedForView,
      SidebarManager *sidebarManager, ScrollManager *scrollManager,
      int currentCollectionIndex) -> void;
};

#endif