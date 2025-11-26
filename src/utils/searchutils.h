#ifndef SEARCHTYPES_H
#define SEARCHTYPES_H

enum class SearchMode {
  CurrentCollection,
  CurrentAndSubcollections,
  AllCollections
};

struct SearchContext {
  bool hasSubs = false;
  bool realDirectItems = false;
  bool allowAll = false;
  bool isContainer = false;
};

#endif
