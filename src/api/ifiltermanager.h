#ifndef IFILTERMANAGER_H
#define IFILTERMANAGER_H

#include <QString>

/**
 * @brief Read-side role interface to the scroll-side filter.
 *
 * Exposes only what cross-module callers actually need: the active-state
 * flag, the visual → actual index map, and the filtered count. The
 * mutation surface (setSourceData, applyFilter, applySubcollectionFilter,
 * clearFilter, …) stays on the concrete FilterManager and is reached
 * directly by the ScrollManager-side helpers that own it.
 *
 * `ctx->filterManager()` returns this role interface so external callers
 * (CoverFlowController and any future module that needs to map a visible
 * index to its underlying item) don't have to #include the concrete
 * filtermanager.h header.
 */
class IFilterManager {
public:
  virtual ~IFilterManager() = default;

  /// True when a search or subcollection filter (or the hide-missing-
  /// artwork predicate) is active. False on the unfiltered passthrough.
  [[nodiscard]] virtual bool isFiltered() const = 0;

  /// Map a visual index (position in the filtered view) to the underlying
  /// item index. Returns -1 for out-of-range inputs. When `isFiltered()`
  /// is false, callers should bypass this and treat the visual index as
  /// the actual index directly.
  [[nodiscard]] virtual int getActualIndex(int visualIndex) const = 0;

  /// Number of items visible after the active filter is applied.
  [[nodiscard]] virtual int filteredCount() const = 0;
};

#endif // IFILTERMANAGER_H
