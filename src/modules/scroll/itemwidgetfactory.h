#ifndef ITEMWIDGETFACTORY_H
#define ITEMWIDGETFACTORY_H

#include "collectionutils.h"
#include <QObject>
#include <QString>
#include <functional>

class ItemWidget;
class QWidget;
class ArtworkManager;
class DatabaseManager;
class WidgetPoolManager;

/**
 * @brief Factory for creating and configuring ItemWidget instances.
 *
 * Handles widget acquisition from pool, configuration based on collection
 * settings, and artwork loading setup. Separates widget lifecycle management
 * from scrolling logic.
 */
class ItemWidgetFactory : public QObject {
  Q_OBJECT
public:
  explicit ItemWidgetFactory(QObject *parent = nullptr);
  ~ItemWidgetFactory() override = default;

  // Dependencies
  void setWidgetPool(WidgetPoolManager *pool) { m_widgetPool = pool; }
  void setArtworkManager(ArtworkManager *manager) { m_artworkManager = manager; }
  void setDatabaseManager(DatabaseManager *manager) { m_databaseManager = manager; }
  void setParentWidget(QWidget *parent) { m_parentWidget = parent; }

  // Context for widget creation
  void setCollectionContext(const CollectionContext &context) { m_context = context; }
  void setMetrics(int itemWidth, int itemHeight);

  // Subcollection name resolver callback
  using SubcollectionNameResolver = std::function<QString(int)>;
  void setSubcollectionNameResolver(SubcollectionNameResolver resolver) {
    m_subcollectionNameResolver = std::move(resolver);
  }

  // File data accessors (set by ScrollManager before creating widgets)
  void setFileData(const QStringList *filePaths, const QHash<QString, QString> *fileNames);

  /**
   * @brief Creates a subcollection widget.
   * @param subcollectionIndex The index of the subcollection.
   * @return Configured ItemWidget for the subcollection.
   */
  [[nodiscard]] ItemWidget *createSubcollectionWidget(int subcollectionIndex);

  /**
   * @brief Creates a media item widget.
   * @param mediaIndex Index into the file paths list (after subcollections).
   * @param collectionIndex Output: detected collection index for the item.
   * @return Configured ItemWidget for the media item, or nullptr if invalid.
   */
  [[nodiscard]] ItemWidget *createMediaWidget(int mediaIndex, int &collectionIndex);

  /**
   * @brief Creates a virtual folder widget for navigating subfolders.
   * @param folderPath The relative path of the folder.
   * @return Configured ItemWidget for the virtual folder.
   */
  [[nodiscard]] ItemWidget *createVirtualFolderWidget(const QString &folderPath);

  /**
   * @brief Creates a placeholder widget for items still loading.
   * @return ItemWidget showing "Loading..." state.
   */
  [[nodiscard]] ItemWidget *createPlaceholderWidget();

  /**
   * @brief Releases a widget back to the pool.
   * @param widget The widget to release.
   * @param visibleRows Current visible row count for pool sizing.
   * @param itemsPerRow Items per row for pool sizing.
   */
  void releaseWidget(ItemWidget *widget, int visibleRows, int itemsPerRow);

signals:
  /**
   * @brief Emitted when a subcollection widget is double-clicked.
   * @param subcollectionIndex The index of the subcollection.
   */
  void subcollectionDoubleClicked(int subcollectionIndex);

  /**
   * @brief Emitted when a virtual folder widget is double-clicked.
   * @param folderPath The relative path of the folder.
   */
  void virtualFolderDoubleClicked(const QString &folderPath);

  /**
   * @brief Emitted when items need to be loaded from the database.
   * @param startIndex Start of the range to load.
   * @param count Number of items to load.
   */
  void requestItemsRange(int startIndex, int count);

private:
  [[nodiscard]] ItemWidget *acquireWidget();
  void configureBaseWidget(ItemWidget *widget);
  void configureArtworkForWidget(ItemWidget *widget, const QString &fullPath);
  void resolveMediaItemPaths(const QString &rawFileName, QString &fullPath,
                             QString &displayName, int &collectionIndex);
  void updateCollectionIndexFromDatabase(const QString &fullPath,
                                         int &collectionIndex);

  WidgetPoolManager *m_widgetPool = nullptr;
  ArtworkManager *m_artworkManager = nullptr;
  DatabaseManager *m_databaseManager = nullptr;
  QWidget *m_parentWidget = nullptr;

  CollectionContext m_context;
  int m_itemWidth = 0;
  int m_itemHeight = 0;

  SubcollectionNameResolver m_subcollectionNameResolver;
  const QStringList *m_filePaths = nullptr;
  const QHash<QString, QString> *m_fileNames = nullptr;
};

#endif // ITEMWIDGETFACTORY_H
