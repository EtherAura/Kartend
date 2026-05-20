#ifndef IDETAILSPANEMANAGER_H
#define IDETAILSPANEMANAGER_H

#include <QString>

class IDetailsPane;
class ItemWidget;

/**
 * @brief Sibling-facing interface to the details / metadata side pane.
 *
 * Role interface (interface-segregation): the slice sibling managers reach
 * for through ApplicationContext. DetailsPaneManager's setupReferences()
 * wiring, persistence helpers and its sidebarVisibilityChanged /
 * sidebarLayoutChanged signals stay on the concrete class — the signal
 * connections are all made against the owner's concrete pointer.
 *
 * Plain abstract class, not a QObject — DetailsPaneManager derives QObject
 * directly; see its multiple-inheritance declaration.
 */
class IDetailsPaneManager {
public:
  virtual ~IDetailsPaneManager() = default;

  /// Cached resolution context for the currently-displayed item, so
  /// siblings (e.g. DetailPageManager) can render the same item without
  /// redoing the owner / artwork / video / manual directory lookup.
  /// `uuid` is empty when no item is selected or the lookup failed.
  struct ItemContext {
    QString filePath;
    QString itemName;
    QString uuid;         // collection_uuid for DB metadata lookup
    QString artworkDir;   // expanded, owner-aware artwork directory
    QString videoDir;     // expanded, owner-aware video directory
    QString manualDir;    // expanded, owner-aware manual directory
    int owningIndex = -1; // index in the collection list, -1 if unknown
    [[nodiscard]] bool isValid() const { return !filePath.isEmpty() && !uuid.isEmpty(); }
  };

  virtual void toggleSidebar() = 0;
  virtual void updateSidebarMetadata(ItemWidget *selectedItem) = 0;
  virtual void updateSidebarMetadata(const QString &filePath, const QString &itemName) = 0;
  virtual void refreshSidebarMetadataImmediate() = 0;
  virtual void applySidebarStateForCollection(int collectionIndex) = 0;
  virtual void updateSidebarLayout(int currentCollectionIndex) = 0;
  [[nodiscard]] virtual bool isSidebarVisible() const = 0;
  [[nodiscard]] virtual IDetailsPane *sidebarWidget() const = 0;
  [[nodiscard]] virtual const ItemContext &currentItemContext() const = 0;
};

#endif // IDETAILSPANEMANAGER_H
