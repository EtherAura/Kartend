#ifndef ISCROLLDATASOURCE_H
#define ISCROLLDATASOURCE_H

#include <QHash>
#include <QString>
#include <QStringList>

class ItemWidget;

/**
 * @brief Data-source role of the scroll layer (Kartend-h1l8f).
 *
 * One of the six role interfaces IScrollManager unions: raw read access to
 * the loaded item set (paths, names, totals, active widgets) plus the
 * subcollection / virtual-folder queries and context switches layered over
 * it. Matches the DataSourceManager / ScrollDataStore side of the
 * implementation split.
 *
 * Plain abstract class, not a QObject — ScrollManager derives QObject once,
 * through IScrollManager. Reached via ctx->scrollData().
 */
class IScrollDataSource {
public:
  virtual ~IScrollDataSource() = default;

  [[nodiscard]] virtual int getTotalItems() const = 0;
  [[nodiscard]] virtual const QStringList &getFilePaths() const = 0;
  [[nodiscard]] virtual const QHash<QString, QString> &getFileNames() const = 0;
  [[nodiscard]] virtual QString filePathForVisualIndex(int visualIndex) const = 0;

  [[nodiscard]] virtual const QHash<int, ItemWidget *> &getActiveWidgets() const = 0;
  /// O(1) widget -> visual index lookup (reverse of getActiveWidgets); -1 if
  /// the widget isn't currently placed (Kartend-th8z).
  [[nodiscard]] virtual int indexForWidget(ItemWidget *widget) const = 0;

  [[nodiscard]] virtual int getSubcollectionCount() const = 0;
  [[nodiscard]] virtual QString getSubcollectionName(int subcollectionIndex) const = 0;
  [[nodiscard]] virtual int getVirtualFolderCount() const = 0;
  [[nodiscard]] virtual QString virtualFolderPathForVisualIndex(int visualIndex) const = 0;
  /// Resolve a raw item index to the subcollection index that owns it, or -1
  /// if the item is not part of a subcollection.
  [[nodiscard]] virtual int subcollectionIndexFromActual(int actualIndex) const = 0;
  virtual void updateContextForSubcollection(int subcollectionIndex) = 0;
  virtual void applySubcollectionFilter(int subcollectionIndex) = 0;
};

#endif // ISCROLLDATASOURCE_H
