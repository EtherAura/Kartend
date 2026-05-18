// Pure list/map post-processing helpers in the QueryManagerInternal
// namespace (declared in querymanagerhelpers.h):
//   - appendFileMapsAndListCanonical
//   - sortFiles
// These touch no QSqlDatabase or worker-thread state — they operate only
// on caller-supplied containers / strings.
#include "pathutils.h"
#include "queryhelpers.h"
#include "querymanager.h"
#include "querymanagerhelpers.h"
#include <algorithm>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QVector>
#include <random>

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

