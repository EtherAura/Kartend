#ifndef KARTEND_UTILS_APP_COLLECTION_VIEW_SETTINGS_H
#define KARTEND_UTILS_APP_COLLECTION_VIEW_SETTINGS_H

// Leaf struct peeled out of GeneralSettings (Kartend-q1w6). Sort + list
// column widths, collection-categorization filters, the View menu chrome
// toggles (menubar/toolbar/fullscreen), and the placeholder-title overlay.

#include <QString>

#include "../collectiontypes.h" // SortMode

struct ViewSettings {
  SortMode sortMode = SortMode::NameAscending; // Current sort mode
  bool excludeSubfoldersFromSort = false;      // Exclude subfolders/subcollections from sorting
  int listCollectionColumnWidth = 150;         // Collection column width in list view
  int listArtworkColumnWidth = 32;             // Artwork column width in list view
  // User-visible label matching CollectionConfig::type (with parent-chain
  // inheritance); empty means "show all types".
  QString collectionTypeFilter;
  // Collapses every subcollection tile out of the current view regardless of
  // type, leaving only media items.
  bool hideSubcollectionTiles = false;
  // When an item falls back to placeholder art, draw the item's title text
  // on top of the placeholder. Pairs well with hideTitles=on so the title
  // only appears where it adds information.
  bool showTitleInPlaceholder = false;
  // View-mode toggles (F10 / F8 / F11). Defaults match the original .ui
  // "all visible, not fullscreen" state.
  bool showMenuBar = true;
  bool showToolbar = true;
  bool fullscreen = false;
  // Defaulted memberwise equality — keeps GeneralSettings::operator== and the
  // settings dirty-check field-complete automatically (Kartend-6oqat).
  bool operator==(const ViewSettings &) const = default;
};

#endif // KARTEND_UTILS_APP_COLLECTION_VIEW_SETTINGS_H
