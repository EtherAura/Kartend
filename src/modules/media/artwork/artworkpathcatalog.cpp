#include "artworkpathcatalog.h"

#include "artworkutils.h"
#include "collection/collectionconfig.h"
#include "extensionutils.h"
#include "loggingcategories.h"

#include <algorithm>

#include <QDeadlineTimer>
#include <QDir>
#include <QLoggingCategory>
#include <QMutexLocker>
#include <QtConcurrent>
#include <QThread>

Q_DECLARE_LOGGING_CATEGORY(lcArtworkManager)

ArtworkPathCatalog::~ArtworkPathCatalog() {
  // Supersede every in-flight build — not just the latest (Kartend-lz1zp) —
  // then drain with a bounded budget. A build's QDir::entryList over a
  // network mount can block indefinitely with no cancellation point and a
  // QFuture can't be force-cancelled mid-scan, so waiting unbounded here hung
  // GUI-thread shutdown forever (Kartend-52b4j.3). The build lambdas co-own
  // the catalog state via shared_ptr, so a build that outlasts the budget is
  // abandoned safely: it finishes into state nothing else reads and the
  // generation bump makes it discard its results. Default = the full
  // standalone budget; instant when the owner already drained the catalog
  // against its shared teardown deadline.
  abandonPendingBuilds();
}

void ArtworkPathCatalog::abandonPendingBuilds(QDeadlineTimer deadline) {
  QList<QFuture<void>> pending;
  {
    QMutexLocker locker(&m_state->mutex);
    ++m_state->buildGeneration;
    pending = m_state->inFlightBuilds;
    m_state->inFlightBuilds.clear();
  }
  if (pending.isEmpty()) {
    return;
  }
  bool drained = true;
  for (QFuture<void> &future : pending) {
    // QFuture has no bounded wait; poll against the shared deadline (the
    // 10 ms granularity is noise next to the budget).
    while (!future.isFinished() && !deadline.hasExpired()) {
      QThread::msleep(10);
    }
    if (!future.isFinished()) {
      drained = false;
      break;
    }
  }
  if (!drained) {
    qCWarning(lcArtworkManager)
        << "ArtworkPathCatalog: build task(s) still enumerating artwork directories at the"
           " teardown drain deadline; abandoning them to avoid blocking exit (they finish into"
           " co-owned state and drop their results via the generation guard)";
  }
}

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
    QMutexLocker locker(&m_state->mutex);
    generation = ++m_state->buildGeneration;
    m_state->allPaths.clear();
    m_state->index = 0;
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
  // build (a newer collection switch, or teardown) drops its stale results
  // instead of polluting the list the newer build cleared. The lambda co-owns
  // the state (shared_ptr by value), so it stays safe even when the dtor's
  // bounded drain abandons it mid-scan (Kartend-52b4j.3).
  std::shared_ptr<State> state = m_state;
  QFuture<void> future = QtConcurrent::run([state, generation, allDirs]() {
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
      QMutexLocker locker(&state->mutex);
      if (generation != state->buildGeneration) {
        return; // a newer build (or the dtor) superseded this one
      }
      state->allPaths.append(fullPaths);
    }
  });

  // Register the build so the dtor can drain it even after a newer build
  // supersedes it; prune finished entries so the list stays O(live builds).
  {
    QMutexLocker locker(&m_state->mutex);
    m_state->inFlightBuilds.erase(
        std::remove_if(m_state->inFlightBuilds.begin(), m_state->inFlightBuilds.end(),
                       [](const QFuture<void> &f) { return f.isFinished(); }),
        m_state->inFlightBuilds.end());
    m_state->inFlightBuilds.append(future);
  }
  return future;
}

int ArtworkPathCatalog::totalPaths() const {
  QMutexLocker locker(&m_state->mutex);
  return m_state->allPaths.size();
}

bool ArtworkPathCatalog::isEmpty() const {
  QMutexLocker locker(&m_state->mutex);
  return m_state->allPaths.isEmpty();
}

bool ArtworkPathCatalog::isExhausted() const {
  QMutexLocker locker(&m_state->mutex);
  return m_state->index >= m_state->allPaths.size();
}

QStringList ArtworkPathCatalog::takeNextBatch(int maxCount) {
  if (maxCount <= 0) {
    return {};
  }
  QMutexLocker locker(&m_state->mutex);
  const int available = m_state->allPaths.size() - m_state->index;
  if (available <= 0) {
    return {};
  }
  const int take = qMin(maxCount, available);
  QStringList out = m_state->allPaths.mid(m_state->index, take);
  m_state->index += take;
  return out;
}

QStringList ArtworkPathCatalog::filterAndMarkPending(const QStringList &batch) {
  QStringList out;
  out.reserve(batch.size());
  QMutexLocker locker(&m_state->mutex);
  for (const QString &path : batch) {
    if (m_state->silentlyCached.contains(path) || m_state->silentPending.contains(path)) {
      continue;
    }
    m_state->silentPending.insert(path);
    out.append(path);
  }
  return out;
}

void ArtworkPathCatalog::markSilentlyCached(const QString &path) {
  QMutexLocker locker(&m_state->mutex);
  m_state->silentlyCached.insert(path);
}

void ArtworkPathCatalog::unmarkSilentPending(const QString &path) {
  QMutexLocker locker(&m_state->mutex);
  m_state->silentPending.remove(path);
}

void ArtworkPathCatalog::clearSilentPendingOnly() {
  QMutexLocker locker(&m_state->mutex);
  m_state->silentPending.clear();
}

void ArtworkPathCatalog::clearPathsAndPending() {
  QMutexLocker locker(&m_state->mutex);
  m_state->allPaths.clear();
  m_state->index = 0;
  m_state->silentPending.clear();
}

void ArtworkPathCatalog::clearAll() {
  QMutexLocker locker(&m_state->mutex);
  m_state->allPaths.clear();
  m_state->index = 0;
  m_state->silentlyCached.clear();
  m_state->silentPending.clear();
}
