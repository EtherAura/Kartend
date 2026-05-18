// Pure grouping for the unified Scraper dialog — see scrapejobgrouping.h.
#include "scrapejobgrouping.h"

#include <QSet>

namespace ScrapeJobGrouping {

QList<QPair<int, QStringList>>
byOwningCollection(const QList<int> &checkedOrder,
                   const QHash<int, QStringList> &selectionByCollection,
                   const QHash<int, QHash<QString, int>> &ownerByCollection, int collectionCount) {
  QList<int> ownerOrder;                 // owners in first-seen order
  QHash<int, QStringList> itemsByOwner;  // owner → its (ordered) items
  QHash<int, QSet<QString>> seenByOwner; // owner → items already taken

  for (int idx : checkedOrder) {
    if (idx < 0 || idx >= collectionCount) continue;
    const QStringList &items = selectionByCollection.value(idx);
    if (items.isEmpty()) continue;
    const QHash<QString, int> &owners = ownerByCollection.value(idx);
    for (const QString &path : items) {
      // No owner entry (or an out-of-range one) → the collection the
      // item was listed under is itself the owner: a plain collection,
      // or an item recorded before its owner map landed.
      int owner = owners.value(path, idx);
      if (owner < 0 || owner >= collectionCount) owner = idx;
      QSet<QString> &seen = seenByOwner[owner];
      if (seen.contains(path)) continue; // already contributed by another row
      seen.insert(path);
      if (!itemsByOwner.contains(owner)) ownerOrder.append(owner);
      itemsByOwner[owner].append(path);
    }
  }

  QList<QPair<int, QStringList>> result;
  result.reserve(ownerOrder.size());
  for (int owner : ownerOrder) {
    result.append({owner, itemsByOwner.value(owner)});
  }
  return result;
}

} // namespace ScrapeJobGrouping
