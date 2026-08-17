#ifndef COLLECTIONTREEMODEL_H
#define COLLECTIONTREEMODEL_H

#include <QList>

#include "collectionconfig.h"
#include "collectionhierarchycache.h"

/// Pure builder for the collection tree panel's row structure
/// (Kartend-ob1c9). No QObject, no widgets — the panel walks the returned
/// nodes into QTreeWidgetItems, and tests assert on the structure directly
/// (the KartPreflight pattern).
///
/// Shape decisions this encodes (from the feature spec):
/// - Collections form the tree proper: roots are non-playlist configs with
///   parentCollectionIndex == -1; children come from
///   CollectionHierarchyCache::directChildren, so alias parents
///   (additionalParentNames) make the SAME collection index appear under
///   several nodes — that is intended, the hierarchy is a DAG.
/// - Playlists are NOT inline. Every isPlaylist config lands in a separate
///   grouped section, reserved playlists (Favorites) first, regardless of
///   the parentCollectionIndex resyncPlaylistCollections() synthesized for
///   the home-view tiles.
/// - Recursion is guarded per PATH: a child already on the ancestor chain is
///   skipped (an alias diamond renders twice, a hostile cycle renders
///   finitely), with a hard depth cap as the backstop.
namespace CollectionTreeModel {

struct Node {
  int collectionIndex = -1;
  QList<Node> children;

  bool operator==(const Node &other) const = default;
};

struct Model {
  QList<Node> collectionRoots;
  /// Grouped playlists section, reserved kinds (Favorites) first, then the
  /// remaining playlists in collection-list order.
  QList<int> playlistIndices;

  bool operator==(const Model &other) const = default;
};

/// Depth backstop for the per-path cycle guard. Real libraries nest a
/// handful of levels; anything deeper than this is a data bug rendered
/// finitely instead of a stack overflow.
inline constexpr int MAX_TREE_DEPTH = 32;

[[nodiscard]] Model build(const QList<CollectionConfig> &collections,
                          const CollectionHierarchyCache &hierarchy);

} // namespace CollectionTreeModel

#endif // COLLECTIONTREEMODEL_H
