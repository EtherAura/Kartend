#ifndef ISEARCHSTATESCROLL_H
#define ISEARCHSTATESCROLL_H

#include <QString>

/**
 * @brief Search/filter + pre-search-state role of the scroll layer
 * (Kartend-h1l8f).
 *
 * One of the six role interfaces IScrollManager unions: applying/clearing the
 * text filter, the search loading overlay, the pre-search state snapshot that
 * search-clear restores, and the filtered → actual index mapping. Matches the
 * FilterManager / PreSearchStateManager side of the implementation split.
 *
 * Plain abstract class, not a QObject — ScrollManager derives QObject once,
 * through IScrollManager. Reached via ctx->scrollSearch().
 */
class ISearchStateScroll {
public:
  virtual ~ISearchStateScroll() = default;

  virtual void applyFilter(const QString &searchText) = 0;
  virtual void clearFilter() = 0;
  [[nodiscard]] virtual int getFilteredIndex(int visualIndex) const = 0;
  virtual void showSearchLoadingOverlay() = 0;
  virtual void hideSearchLoadingOverlay() = 0;
  virtual void savePreSearchState() = 0;
  virtual void restorePreSearchState() = 0;
  [[nodiscard]] virtual bool hasPreSearchState() const = 0;
};

#endif // ISEARCHSTATESCROLL_H
