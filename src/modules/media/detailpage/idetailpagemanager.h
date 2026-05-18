#ifndef IDETAILPAGEMANAGER_H
#define IDETAILPAGEMANAGER_H

class DetailPageOverlay;

/**
 * @brief Sibling-facing interface to the item detail page.
 *
 * Role interface (interface-segregation): the runtime slice siblings reach
 * for through ApplicationContext. DetailPageManager's setupReferences()
 * lifecycle wiring stays on the concrete class, which the owner holds.
 *
 * Plain abstract class, not a QObject — DetailPageManager derives QObject
 * directly; see its multiple-inheritance declaration.
 */
class IDetailPageManager {
public:
  virtual ~IDetailPageManager() = default;

  virtual void showForCurrentSelection() = 0;
  virtual void hideOverlay() = 0;
  [[nodiscard]] virtual DetailPageOverlay *overlay() const = 0;
};

#endif // IDETAILPAGEMANAGER_H
