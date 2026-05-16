#ifndef SCRAPEJOBGROUPING_H
#define SCRAPEJOBGROUPING_H

#include <QHash>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

/// Pure grouping step behind the unified Scraper dialog's "Scrape"
/// button. Splits the dialog's per-collection item selections into one
/// job per collection that actually OWNS the items.
///
/// A "shell" parent collection displays the items of its
/// subcollections; when the user scrapes such a parent the checked
/// item list spans several collections, and each item's scraped
/// artwork + metadata must be routed to the subcollection that owns it
/// rather than dumped on the parent (Kartend-7dng). Kept free-standing
/// and dependency-light so the routing is unit-testable without the
/// dialog.
namespace ScrapeJobGrouping {

/// Group checked-collection item selections by owning collection.
///
/// @param checkedOrder          Checked collection indices in tree
///                              display order. Drives owner ordering;
///                              duplicate and invalid (e.g. -1) entries
///                              are tolerated.
/// @param selectionByCollection Per checked collection: the item paths
///                              the user picked to scrape from it.
/// @param ownerByCollection     Per viewed collection: item path → the
///                              collection index that owns that item.
///                              A path with no entry is attributed to
///                              the collection it was listed under.
/// @param collectionCount       Size of the collection list; an owner
///                              index outside [0, collectionCount) is
///                              treated as unmapped and falls back to
///                              the listing collection.
/// @return (ownerIndex, items) pairs. Items are deduplicated across
///         collections (a parent row and a subcollection row can both
///         contribute the same item) and each owner keeps the order in
///         which its items were first seen; owners themselves appear in
///         first-seen order.
[[nodiscard]] QList<QPair<int, QStringList>>
byOwningCollection(const QList<int> &checkedOrder,
                   const QHash<int, QStringList> &selectionByCollection,
                   const QHash<int, QHash<QString, int>> &ownerByCollection, int collectionCount);

} // namespace ScrapeJobGrouping

#endif // SCRAPEJOBGROUPING_H
