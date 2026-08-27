#include "collectiontreemodel.h"

#include <algorithm>
#include <QSet>

namespace CollectionTreeModel {

namespace {

Node buildNode(int index, const QList<CollectionConfig> &collections,
               const CollectionHierarchyCache &hierarchy, QSet<int> &pathAncestors, int depth) {
  Node node;
  node.collectionIndex = index;
  if (depth >= MAX_TREE_DEPTH) {
    return node;
  }
  pathAncestors.insert(index);
  const QList<int> &children = hierarchy.directChildren(index);
  for (int child : children) {
    if (child < 0 || child >= collections.size()) {
      continue;
    }
    // Playlists are grouped in their own section, never inline in the tree —
    // resyncPlaylistCollections() parents them for the home-view tiles, and
    // directChildren() faithfully reports that, so filter here.
    if (collections.at(child).isPlaylist) {
      continue;
    }
    // Per-path guard: an alias diamond legitimately repeats a subtree under
    // two parents, but a node must never appear on its own ancestor chain.
    if (pathAncestors.contains(child)) {
      continue;
    }
    node.children.append(buildNode(child, collections, hierarchy, pathAncestors, depth + 1));
  }
  // Kartend-fxn4v: order children case-INSENSITIVELY by name, matching the
  // grid. directChildren() reports config order, which for a recursively
  // imported library is the directory-scan order — case-SENSITIVE on Linux. So
  // vendor folders NEC / Nintendo / SNK / Sega / Sharp listed as
  // "NEC, Nintendo, SNK, Sega, Sharp" in the sidebar ('N' < 'e' in ASCII) while
  // the grid, which sorts through ScrollDataManager's NameAscending comparator,
  // showed the same five as "NEC, Nintendo, Sega, Sharp, SNK". Both orders are
  // defensible alone; showing both at once on one screen is not.
  //
  // Comparator is byte-identical to that grid one
  // (scrolldatamanager.cpp, SortMode::NameAscending) so the two cannot drift.
  // stable_sort, not sort: names differing only in case compare EQUAL here, and
  // a stable pass leaves those in config order rather than an arbitrary one.
  //
  // Children only. Roots keep config order deliberately — the tree controller
  // treats the first root as the chrome-wearing header row, so reordering roots
  // would move the toolbar chrome, which is well outside this bug.
  std::stable_sort(
      node.children.begin(), node.children.end(), [&collections](const Node &a, const Node &b) {
        return QString::compare(collections.at(a.collectionIndex).name,
                                collections.at(b.collectionIndex).name, Qt::CaseInsensitive) < 0;
      });
  pathAncestors.remove(index);
  return node;
}

} // namespace

Model build(const QList<CollectionConfig> &collections, const CollectionHierarchyCache &hierarchy) {
  Model model;
  QList<int> reservedPlaylists;
  QList<int> plainPlaylists;

  for (int i = 0; i < collections.size(); ++i) {
    const CollectionConfig &cfg = collections.at(i);
    if (cfg.isPlaylist) {
      if (!cfg.playlistReservedKind.isEmpty()) {
        reservedPlaylists.append(i);
      } else {
        plainPlaylists.append(i);
      }
      continue;
    }
    if (cfg.parentCollectionIndex == -1) {
      QSet<int> pathAncestors;
      model.collectionRoots.append(buildNode(i, collections, hierarchy, pathAncestors, 0));
    }
  }

  model.playlistIndices = reservedPlaylists + plainPlaylists;
  return model;
}

} // namespace CollectionTreeModel
