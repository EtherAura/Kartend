#include "collectiontreemodel.h"

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
