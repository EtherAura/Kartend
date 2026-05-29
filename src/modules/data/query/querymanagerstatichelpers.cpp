// Pure list/map post-processing helpers in the QueryManagerInternal
// namespace (declared in querymanagerhelpers.h):
//   - appendFileMapsAndListCanonical
//   - sortFiles
// These touch no QSqlDatabase or worker-thread state — they operate only
// on caller-supplied containers / strings.
#include "collection/collectionconfig.h"
#include "collection/helpers.h"
#include "errorutils.h"
#include "pathutils.h"
#include "preparedstatementcache.h"
#include "queryhelpers.h"
#include "querymanager.h"
#include "querymanagerhelpers.h"
#include "querymanagersql.h"
#include <algorithm>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QList>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QVariant>
#include <QVector>
#include <random>
#include <stdexcept>

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;
using QueryManagerInternal::canonicalKeyPath;
using QueryManagerInternal::displayNameForBase;
using QueryManagerInternal::insertIfAbsent;

void QueryManagerInternal::appendFileMapsAndListCanonical(
    int collectionIndex, const CollectionConfig &expandedCollection,
    const QString &mappingArtworkDir, const QStringList &filePaths, QStringList &allFilePaths,
    QHash<QString, QString> &allFileNames, QHash<QString, QString> &fileToArtworkDir,
    QHash<QString, QString> &fileToMediaDir, QHash<QString, int> &fileToCollectionIndex, bool dedup,
    QSet<QString> *seenCanonicalPaths, QHash<QString, QString> *canonicalPathCache) {
  const QString mediaDir = expandedCollection.mediaDirectory;
  QDir mediaQDir(mediaDir);

  QSet<QString> localSeenCanonicalPaths;
  QSet<QString> *effectiveSeenCanonicalPaths = seenCanonicalPaths;
  if (dedup && !effectiveSeenCanonicalPaths) {
    // Ensure dedup stays O(n) even when the caller doesn't provide a set.
    // Preserve ordering by only using the set for membership checks.
    localSeenCanonicalPaths.reserve(allFilePaths.size() + filePaths.size());
    for (const QString &existing : allFilePaths) {
      localSeenCanonicalPaths.insert(existing);
    }
    effectiveSeenCanonicalPaths = &localSeenCanonicalPaths;
  }

  if (!filePaths.isEmpty()) {
    // Pre-reserve to reduce rehashing and reallocations in hot paths.
    // Worst-case we insert 4 keys per file for the dir/index maps.
    const int incoming = filePaths.size();
    allFilePaths.reserve(allFilePaths.size() + incoming);
    allFileNames.reserve(allFileNames.size() + incoming);
    fileToArtworkDir.reserve(fileToArtworkDir.size() + incoming * 4);
    fileToMediaDir.reserve(fileToMediaDir.size() + incoming * 4);
    fileToCollectionIndex.reserve(fileToCollectionIndex.size() + incoming * 4);
    if (seenCanonicalPaths) {
      seenCanonicalPaths->reserve(seenCanonicalPaths->size() + incoming);
    }
    if (canonicalPathCache) {
      canonicalPathCache->reserve(canonicalPathCache->size() + incoming);
    }
  }

  for (const QString &file : filePaths) {
    const QString absPath = mediaQDir.absoluteFilePath(file);
    const QString keyPath = canonicalKeyPath(absPath, dedup, canonicalPathCache);

    if (dedup) {
      if (effectiveSeenCanonicalPaths && !effectiveSeenCanonicalPaths->contains(keyPath)) {
        effectiveSeenCanonicalPaths->insert(keyPath);
        allFilePaths.append(keyPath);
      }
    } else {
      allFilePaths.append(keyPath);
    }

    const int lastSeparator = std::max(file.lastIndexOf('/'), file.lastIndexOf('\\'));
    const QString fileName = (lastSeparator >= 0) ? file.mid(lastSeparator + 1) : file;
    const int lastDot = fileName.lastIndexOf('.');
    const QString baseName = (lastDot > 0) ? fileName.left(lastDot) : fileName;
    const QString displayName = displayNameForBase(baseName);

    allFileNames[keyPath] = displayName;

    insertIfAbsent(fileToArtworkDir, keyPath, mappingArtworkDir);
    insertIfAbsent(fileToArtworkDir, file, mappingArtworkDir);
    if (fileName != file) {
      insertIfAbsent(fileToArtworkDir, fileName, mappingArtworkDir);
    }
    if (baseName != file && baseName != fileName) {
      insertIfAbsent(fileToArtworkDir, baseName, mappingArtworkDir);
    }

    insertIfAbsent(fileToMediaDir, keyPath, mediaDir);
    insertIfAbsent(fileToMediaDir, file, mediaDir);
    if (fileName != file) {
      insertIfAbsent(fileToMediaDir, fileName, mediaDir);
    }
    if (baseName != file && baseName != fileName) {
      insertIfAbsent(fileToMediaDir, baseName, mediaDir);
    }

    insertIfAbsent(fileToCollectionIndex, keyPath, collectionIndex);
    insertIfAbsent(fileToCollectionIndex, file, collectionIndex);
    if (fileName != file) {
      insertIfAbsent(fileToCollectionIndex, fileName, collectionIndex);
    }
    if (baseName != file && baseName != fileName) {
      insertIfAbsent(fileToCollectionIndex, baseName, collectionIndex);
    }
  }
}

void QueryManagerInternal::sortFiles(QStringList &allFilePaths, SortMode mode) {
  if (mode == SortMode::Random) {
    // Fisher-Yates shuffle
    auto seed = static_cast<unsigned>(QDateTime::currentMSecsSinceEpoch());
    std::mt19937 rng(seed);
    for (int i = allFilePaths.size() - 1; i > 0; --i) {
      std::uniform_int_distribution<int> dist(0, i);
      int j = dist(rng);
      allFilePaths.swapItemsAt(i, j);
    }
    return;
  }

  const bool descending = (mode == SortMode::NameDescending || mode == SortMode::DateDescending ||
                           mode == SortMode::SizeDescending);

  struct SortEntry {
    QString path;
    QString sortKey;
    int priority = 0;
    qint64 numericKey = 0;
  };

  QVector<SortEntry> entries;
  entries.reserve(allFilePaths.size());
  for (const QString &path : allFilePaths) {
    const QFileInfo info(path);
    const QString baseName = info.completeBaseName();
    QString sortKey = PathUtils::normalizeDisplayName(baseName);
    if (baseName.startsWith('\'') && baseName.length() > 1 &&
        (baseName[1].isDigit() || baseName[1].isLetter())) {
      sortKey = PathUtils::normalizeDisplayName(baseName.mid(1));
    }

    qint64 numericKey = 0;
    if (mode == SortMode::DateAscending || mode == SortMode::DateDescending) {
      numericKey = info.exists() ? info.lastModified().toMSecsSinceEpoch() : 0;
    } else if (mode == SortMode::SizeAscending || mode == SortMode::SizeDescending) {
      numericKey = info.exists() ? info.size() : 0;
    }

    entries.append(
        SortEntry{path, sortKey, QueryHelpers::characterSortPriority(sortKey), numericKey});
  }

  std::ranges::sort(entries, [&](const SortEntry &lhs, const SortEntry &rhs) {
    if (mode == SortMode::DateAscending || mode == SortMode::DateDescending ||
        mode == SortMode::SizeAscending || mode == SortMode::SizeDescending) {
      if (lhs.numericKey != rhs.numericKey) {
        return descending ? lhs.numericKey > rhs.numericKey : lhs.numericKey < rhs.numericKey;
      }
    }

    if (lhs.priority != rhs.priority) {
      return descending ? lhs.priority > rhs.priority : lhs.priority < rhs.priority;
    }
    const int cmp = lhs.sortKey.compare(rhs.sortKey, Qt::CaseInsensitive);
    return descending ? cmp > 0 : cmp < 0;
  });

  allFilePaths.clear();
  allFilePaths.reserve(entries.size());
  for (const SortEntry &entry : entries) {
    allFilePaths.append(entry.path);
  }
}

void QueryManagerInternal::clearCollectionFromDatabaseByUuid(QSqlDatabase &db,
                                                             PreparedStatementCache &cache,
                                                             const QString &collectionUuid) {
  if (!db.isOpen()) {
    return;
  }

  // Retry logic for database lock scenarios
  constexpr int MAX_RETRIES = 5;
  constexpr int BASE_DELAY_MS = 100;

  for (int attempt = 0; attempt < MAX_RETRIES; ++attempt) {
    if (attempt > 0) {
      // Exponential backoff: 100ms, 200ms, 400ms, 800ms, 1600ms
      QThread::msleep(BASE_DELAY_MS * (1 << (attempt - 1)));
    }

    if (!db.transaction()) {
      continue; // Retry if can't start transaction
    }

    try {
      // Use cached prepared statement for deleting items
      QSqlQuery &query = cache.get(QuerySQL::DELETE_ITEMS_BY_UUID);
      query.bindValue(0, collectionUuid);
      if (!query.exec()) {
        throw std::runtime_error(query.lastError().text().toStdString());
      }

      // Use cached prepared statement for deleting collection
      QSqlQuery &delc = cache.get(QuerySQL::DELETE_COLLECTION_BY_UUID);
      delc.bindValue(0, collectionUuid);
      delc.exec();

      db.commit();
      return; // Success - exit retry loop
    } catch (const std::exception &e) {
      db.rollback();

      QString errorText = ErrorUtils::exceptionMessage(e);
      bool isLockError = errorText.contains("locked", Qt::CaseInsensitive);

      if (!isLockError || attempt == MAX_RETRIES - 1) {
        // Non-lock error or final attempt - log and give up
        auto err = ErrorContext::critical(ErrorCode::DatabaseTransactionFailed,
                                          "Failed to clear collection from database",
                                          "QueryManagerInternal::clearCollectionFromDatabaseByUuid")
                       .withDetails(errorText);
        ErrorUtils::logError(err);
        return;
      }
      // Lock error - will retry
    }
  }
}

void QueryManagerInternal::maybeAbsolutizeItemPaths(QSqlDatabase &db,
                                                    const QList<CollectionConfig> &allCollections) {
  if (!db.isOpen()) {
    return;
  }

  // Gate: the v13 reconcile only needs to run once. Seed the flag at '0' if
  // it doesn't exist yet, then bail immediately if a prior run completed.
  {
    QSqlQuery seed(db);
    if (!seed.exec("INSERT OR IGNORE INTO meta(key, value) "
                   "VALUES('items_paths_absolutized', '0')")) {
      // meta table may not exist on a very old DB that never reached the v3
      // migration; nothing to reconcile in that case either.
      return;
    }
  }
  {
    QSqlQuery check(db);
    if (check.exec("SELECT value FROM meta WHERE key = 'items_paths_absolutized'") &&
        check.next() && check.value(0).toString() == QStringLiteral("1")) {
      return;
    }
  }

  if (!db.transaction()) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseTransactionFailed,
                                     "Failed to start transaction to absolutize item paths",
                                     "QueryManagerInternal::maybeAbsolutizeItemPaths")
                   .withDetails(db.lastError().text());
    ErrorUtils::logError(err);
    return;
  }

  bool allCollectionsProcessed = true;
  bool reconcileFailed = false;

  for (const CollectionConfig &cfg : allCollections) {
    // Playlists are virtual collections — they own no items rows of their
    // own, so there is nothing to rewrite (and no media directory to use).
    if (cfg.isPlaylist) {
      continue;
    }

    // Compute the expanded media dir + uuid IDENTICALLY to every other call
    // site (loadAllCollections, computeCollectionUuid, the scanner).
    const QString expandedMediaDir = PathUtils::validateAndExpandPath(cfg.mediaDirectory, cfg.name);
    if (expandedMediaDir.trimmed().isEmpty()) {
      // Media directory unresolved (e.g. an offline mount). Skip this
      // collection and remember the run is incomplete so the next startup
      // retries it.
      allCollectionsProcessed = false;
      continue;
    }
    const QString uuid = CollectionUtils::computeCollectionUuid(cfg.name, expandedMediaDir);
    if (uuid.isEmpty()) {
      allCollectionsProcessed = false;
      continue;
    }

    QSqlQuery rows(db);
    rows.prepare("SELECT id, path, rel_path FROM items WHERE collection_uuid = ?");
    rows.addBindValue(uuid);
    if (!rows.exec()) {
      auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                       "Failed to read items for path absolutization",
                                       "QueryManagerInternal::maybeAbsolutizeItemPaths")
                     .withDetails(rows.lastError().text());
      ErrorUtils::logError(err);
      reconcileFailed = true;
      break;
    }

    const QDir mediaDir(expandedMediaDir);

    struct PendingUpdate {
      qint64 id = 0;
      QString path;
      QString relPath;
    };
    QList<PendingUpdate> updates;
    while (rows.next()) {
      const qint64 id = rows.value(0).toLongLong();
      const QString storedPath = rows.value(1).toString();
      const QVariant relPathValue = rows.value(2);

      if (!QDir::isAbsolutePath(storedPath)) {
        // Pre-v13 row: path is relative. Rewrite to absolute and stash the
        // original relative form into rel_path.
        updates.append({id, mediaDir.absoluteFilePath(storedPath), storedPath});
      } else if (relPathValue.isNull() || relPathValue.toString().isEmpty()) {
        // Path is already absolute (written by a post-v13 scan, or a prior
        // partial reconcile) but rel_path was never populated — backfill it.
        updates.append({id, storedPath, mediaDir.relativeFilePath(storedPath)});
      }
      // else: already fully migrated, nothing to do.
    }

    QSqlQuery update(db);
    update.prepare("UPDATE items SET path = ?, rel_path = ? WHERE id = ?");
    for (const PendingUpdate &u : updates) {
      update.bindValue(0, u.path);
      update.bindValue(1, u.relPath);
      update.bindValue(2, u.id);
      if (!update.exec()) {
        auto err =
            ErrorContext::warning(ErrorCode::DatabaseQueryFailed, "Failed to absolutize item path",
                                  "QueryManagerInternal::maybeAbsolutizeItemPaths")
                .withDetails(update.lastError().text());
        ErrorUtils::logError(err);
        reconcileFailed = true;
        break;
      }
    }
    if (reconcileFailed) {
      break;
    }
  }

  if (reconcileFailed) {
    db.rollback();
    return;
  }

  // Only flip the flag to '1' when EVERY non-playlist collection was
  // processed. If any was skipped (offline mount), leave it '0' so the next
  // startup retries — the per-row guards above make the retry idempotent.
  if (allCollectionsProcessed) {
    QSqlQuery mark(db);
    mark.prepare("UPDATE meta SET value = '1' WHERE key = 'items_paths_absolutized'");
    if (!mark.exec()) {
      auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                       "Failed to mark item paths as absolutized",
                                       "QueryManagerInternal::maybeAbsolutizeItemPaths")
                     .withDetails(mark.lastError().text());
      ErrorUtils::logError(err);
      db.rollback();
      return;
    }
  }

  if (!db.commit()) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseTransactionFailed,
                                     "Failed to commit item path absolutization",
                                     "QueryManagerInternal::maybeAbsolutizeItemPaths")
                   .withDetails(db.lastError().text());
    ErrorUtils::logError(err);
    db.rollback();
  }
}
