#ifndef ARTWORKPATHCATALOG_H
#define ARTWORKPATHCATALOG_H

#include <memory>

#include <QFuture>
#include <QList>
#include <QMutex>
#include <QSet>
#include <QString>
#include <QStringList>

struct CollectionConfig;

/// Owns the discovered artwork-file list for the active collection plus the
/// silent-load progression state for it. Internal QMutex makes every operation
/// thread-safe.
///
/// Two concerns colocate intentionally: path discovery (buildFromCollection
/// walks the collection tree, scans dirs in parallel, populates the path
/// list), and the silent-load cursor + caches that consume that list (batch
/// pop, "is this path already silently cached / pending?" lookups). They
/// share the same mutex so callers don't have to hold two locks atomically.
class ArtworkPathCatalog {
public:
  ArtworkPathCatalog() = default;

  /// Supersedes and drains (bounded) ALL in-flight build tasks, not just the
  /// most recent one (Kartend-lz1zp). The build lambdas co-own the mutable
  /// state via shared_ptr, so a build that outlasts the drain budget — a
  /// QDir::entryList wedged on a network mount has no cancellation point and
  /// QFuture can't be force-cancelled mid-scan — is abandoned (with a
  /// warning) instead of hanging GUI-thread shutdown forever: it finishes
  /// into the co-owned state and discards its results via the generation
  /// check (Kartend-52b4j.3).
  ~ArtworkPathCatalog();

  ArtworkPathCatalog(const ArtworkPathCatalog &) = delete;
  ArtworkPathCatalog &operator=(const ArtworkPathCatalog &) = delete;

  /// Walks @p collections from @p currentIndex to populate the path list.
  /// Includes descendants when showAllSubcollectionItems is set. Kartend-cl86n:
  /// the directory enumeration runs off the calling thread — this clears the
  /// list + resets the cursor synchronously, kicks a background dentry-prewarm
  /// pass and the scan, and returns a QFuture that completes once the path list
  /// is populated. A later build supersedes an earlier one: each call bumps a
  /// generation counter and a stale build's appends are dropped, so callers can
  /// fire-and-watch on every collection switch without cancelling first.
  [[nodiscard]] QFuture<void> buildFromCollection(const QList<CollectionConfig> *collections,
                                                  int currentIndex);

  /// Collects every artwork directory reachable from the given collection,
  /// optionally including descendants. Used by early dentry-prewarm callers.
  [[nodiscard]] static QStringList collectArtworkDirs(const QList<CollectionConfig> *collections,
                                                      int collectionIndex, bool includeDescendants);

  [[nodiscard]] int totalPaths() const;
  [[nodiscard]] bool isEmpty() const;
  [[nodiscard]] bool isExhausted() const;

  /// Pop up to @p maxCount paths starting at the current cursor. Advances
  /// the cursor past whatever was returned.
  [[nodiscard]] QStringList takeNextBatch(int maxCount);

  /// Filter @p batch through the silent-cached + silent-pending sets,
  /// inserting newly-pending paths into the pending set. Returns the subset
  /// that should actually be dispatched to a precache batch.
  [[nodiscard]] QStringList filterAndMarkPending(const QStringList &batch);

  void markSilentlyCached(const QString &path);
  void unmarkSilentPending(const QString &path);

  /// Drops only the silent-pending set. Used by viewport-cancel paths that
  /// abandon in-flight precache work but want to keep the path list, the
  /// cursor, and the silently-cached set.
  void clearSilentPendingOnly();

  /// Drops the path list and resets the cursor and silent-pending tracking,
  /// but keeps the silently-cached set (used after viewport-cancel where
  /// already-decoded artwork is still valid).
  void clearPathsAndPending();

  /// Drops everything: paths, cursor, silent-cached, silent-pending.
  void clearAll();

private:
  /// All mutable catalog state, co-owned (shared_ptr) by every build lambda
  /// so an abandoned build can never touch freed memory after the dtor's
  /// bounded drain gives up on it (Kartend-52b4j.3; same pattern as
  /// ArtworkDispatcherCacheHandle / the scan queue).
  struct State {
    QMutex mutex;
    QStringList allPaths;
    int index = 0;
    QSet<QString> silentlyCached;
    QSet<QString> silentPending;
    /// Kartend-cl86n: bumped on every buildFromCollection (and once more in
    /// the dtor). A background scan captures the value at kick time and only
    /// appends while it still matches, so a superseded build (rapid
    /// collection switch) — or one abandoned at teardown — drops its stale
    /// paths instead of polluting the list the newer build just cleared.
    int buildGeneration = 0;
    /// Every live build future, including superseded ones (Kartend-lz1zp).
    /// Pruned of finished entries on each new build; drained (bounded) in
    /// the dtor. Guarded by mutex.
    QList<QFuture<void>> inFlightBuilds;
  };
  std::shared_ptr<State> m_state = std::make_shared<State>();
};

#endif // ARTWORKPATHCATALOG_H
