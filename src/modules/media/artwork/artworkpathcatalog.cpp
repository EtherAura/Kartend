#include "artworkpathcatalog.h"

#include "artworkutils.h"
#include "collection/collectionconfig.h"
#include "extensionutils.h"
#include "loggingcategories.h"

#include <QDir>
#include <QMutexLocker>
#include <QtConcurrent>

QStringList ArtworkPathCatalog::collectArtworkDirs(const QList<CollectionConfig> *collections,
                                                   int collectionIndex, bool includeDescendants) {
  if (!collections || collectionIndex < 0 || collectionIndex >= collections->size()) {
    return {};
  }
  QSet<QString> dirs;
  const auto addDir = [&dirs](const QString &artworkDirectory) {
    if (!artworkDirectory.isEmpty()) {
      dirs.insert(QDir(artworkDirectory).absolutePath());
    }
  };
  addDir((*collections)[collectionIndex].artworkDirectory);
  if (includeDescendants) {
    // Iterative DFS over child collections with a visited set: a cyclic
    // parentCollectionIndex (validation doesn't forbid one — the hierarchy cache
    // dedups cycles with its own visited set) would otherwise recurse forever
    // and overflow the stack (Kartend-tqg3r).
    QSet<int> visited;
    visited.insert(collectionIndex);
    QList<int> stack;
    stack.append(collectionIndex);
    while (!stack.isEmpty()) {
      const int parentIdx = stack.takeLast();
      for (int i = 0; i < collections->size(); ++i) {
        if ((*collections)[i].parentCollectionIndex == parentIdx && !visited.contains(i)) {
          visited.insert(i);
          addDir((*collections)[i].artworkDirectory);
          stack.append(i);
        }
      }
    }
  }
  return dirs.values();
}

QFuture<void> ArtworkPathCatalog::buildFromCollection(const QList<CollectionConfig> *collections,
                                                      int currentIndex) {
  int generation = 0;
  {
    QMutexLocker locker(&m_mutex);
    generation = ++m_buildGeneration;
    m_allPaths.clear();
    m_index = 0;
  }

  if (!collections || currentIndex < 0 || currentIndex >= collections->size()) {
    return QtConcurrent::run([] {});
  }
  const bool includeDescendants = (*collections)[currentIndex].showAllSubcollectionItems;
  const QStringList allDirs = collectArtworkDirs(collections, currentIndex, includeDescendants);

  // Warm the dentry cache for these dirs in the background so the enumeration
  // below (and later per-file stats) hit warm caches. Kartend-uzs42: routed
  // through schedulePrewarm so rapid collection switches don't pile up a
  // redundant global-pool walk per switch (it caps at one in-flight prewarm).
  ArtworkUtils::DirectoryCache::instance().schedulePrewarm(allDirs);

  // Kartend-cl86n: enumerate the artwork dirs on a worker (was a GUI-thread
  // QtConcurrent::blockingMap, so a collection switch stalled the UI on a
  // parallel dir scan over cold storage). One sequential pool task keeps the
  // cost off the GUI thread and avoids the nested-pool exhaustion a parallel
  // map dispatched from a pool task could hit; allDirs is captured by value so
  // it outlives this call. Appends are generation-guarded so a superseded
  // build (a newer collection switch) drops its stale results instead of
  // polluting the list the newer build cleared.
  return QtConcurrent::run([this, generation, allDirs]() {
    const QStringList exts = ExtensionUtils::imageFilters();
    for (const QString &dirPath : allDirs) {
      QDir dir(dirPath);
      if (!dir.exists()) {
        continue;
      }
      dir.setNameFilters(exts);
      const QStringList files = dir.entryList(QDir::Files);
      if (files.isEmpty()) {
        continue;
      }
      QStringList fullPaths;
      fullPaths.reserve(files.size());
      for (const QString &file : files) {
        fullPaths.append(dir.absoluteFilePath(file));
      }
      // NOLINTBEGIN(clang-analyzer-core.NullDereference) — analyzer can't see
      // that ArtworkManager's destructor waitForFinished()s this build before
      // m_pathCatalog is torn down, so the QtConcurrent lambda never outlives
      // `this`. The captured pointer is safe. (Same pattern as CacheManager's
      // disk-size walk.)
      QMutexLocker locker(&m_mutex);
      if (generation != m_buildGeneration) {
        return; // a newer build superseded this one
      }
      m_allPaths.append(fullPaths);
      // NOLINTEND(clang-analyzer-core.NullDereference)
    }
  });
}

int ArtworkPathCatalog::totalPaths() const {
  QMutexLocker locker(&m_mutex);
  return m_allPaths.size();
}

bool ArtworkPathCatalog::isEmpty() const {
  QMutexLocker locker(&m_mutex);
  return m_allPaths.isEmpty();
}

bool ArtworkPathCatalog::isExhausted() const {
  QMutexLocker locker(&m_mutex);
  return m_index >= m_allPaths.size();
}

QStringList ArtworkPathCatalog::takeNextBatch(int maxCount) {
  if (maxCount <= 0) {
    return {};
  }
  QMutexLocker locker(&m_mutex);
  const int available = m_allPaths.size() - m_index;
  if (available <= 0) {
    return {};
  }
  const int take = qMin(maxCount, available);
  QStringList out = m_allPaths.mid(m_index, take);
  m_index += take;
  return out;
}

QStringList ArtworkPathCatalog::filterAndMarkPending(const QStringList &batch) {
  QStringList out;
  out.reserve(batch.size());
  QMutexLocker locker(&m_mutex);
  for (const QString &path : batch) {
    if (m_silentlyCached.contains(path) || m_silentPending.contains(path)) {
      continue;
    }
    m_silentPending.insert(path);
    out.append(path);
  }
  return out;
}

void ArtworkPathCatalog::markSilentlyCached(const QString &path) {
  QMutexLocker locker(&m_mutex);
  m_silentlyCached.insert(path);
}

void ArtworkPathCatalog::unmarkSilentPending(const QString &path) {
  QMutexLocker locker(&m_mutex);
  m_silentPending.remove(path);
}

void ArtworkPathCatalog::clearSilentPendingOnly() {
  QMutexLocker locker(&m_mutex);
  m_silentPending.clear();
}

void ArtworkPathCatalog::clearPathsAndPending() {
  QMutexLocker locker(&m_mutex);
  m_allPaths.clear();
  m_index = 0;
  m_silentPending.clear();
}

void ArtworkPathCatalog::clearAll() {
  QMutexLocker locker(&m_mutex);
  m_allPaths.clear();
  m_index = 0;
  m_silentlyCached.clear();
  m_silentPending.clear();
}
