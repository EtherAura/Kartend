#ifndef KARTEND_UTILS_APP_COLLECTION_LISTVIEWOPTIONS_H
#define KARTEND_UTILS_APP_COLLECTION_LISTVIEWOPTIONS_H

// Leaf struct extracted from collectionutils.h (Kartend-0yz3 step 4).
// Carrier for the per-collection list-view appearance overrides. Kept in
// its own translation-unit-input so the list-rendering / settings code
// paths don't drag in CollectionConfig + every other UIConstants
// subnamespace. collectionutils.h re-includes this header so existing
// callers compile unchanged.

#include <QHash>
#include <QString>

#include <uiconstants/item.h>
#include <uiconstants/listview.h>

/// List-view appearance overrides. Only consulted when
/// `cfg.viewType == ViewType::List`. INI keys + kart-manifest JSON keys
/// (`listFontSize`, `listRowHeight`, `listRowColor`, `listAltRowColor`)
/// round-trip unchanged.
struct ListViewOptions {
  /// Font size for list view text.
  int listFontSize = UIConstants::Item::DEFAULT_FONT_SIZE;
  /// Row height in list view (pixels).
  int listRowHeight = UIConstants::ListView::DEFAULT_ROW_HEIGHT;
  /// Primary row color (if blank, uses system Base).
  QString listRowColor;
  /// Alternate row color (if blank, uses system AlternateBase).
  QString listAltRowColor;

  bool operator==(const ListViewOptions &other) const {
    return listFontSize == other.listFontSize && listRowHeight == other.listRowHeight &&
           listRowColor == other.listRowColor && listAltRowColor == other.listAltRowColor;
  }
  bool operator!=(const ListViewOptions &other) const { return !(*this == other); }
};

// Fingerprint hash for the settings hot-reload diff baseline (Kartend-lc58a) —
// must hash exactly the fields operator== compares (see gridlayoutpreferences.h).
inline size_t qHash(const ListViewOptions &key, size_t seed = 0) {
  return qHashMulti(seed, key.listFontSize, key.listRowHeight, key.listRowColor,
                    key.listAltRowColor);
}

#endif
