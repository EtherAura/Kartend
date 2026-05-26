#ifndef FILEMAPCACHE_H
#define FILEMAPCACHE_H

#include <QHash>
#include <QMutex>
#include <QString>

#include "collection/collectioncontext.h"

class FileMapCache {
public:
  FileMapCache() = default;

  void replaceFromItemsLoaded(const QHash<QString, QString> &fileToArtworkDir,
                              const QHash<QString, QString> &fileToMediaDir,
                              const QHash<QString, int> &fileToCollectionIndex,
                              const QHash<QString, QString> &filteredNames);

  void mergeRangeLoaded(const QHash<QString, QString> &fileToArtworkDir,
                        const QHash<QString, QString> &fileToMediaDir,
                        const QHash<QString, int> &fileToCollectionIndex);

  [[nodiscard]] int collectionIndexForFile(const QString &filePath) const;

  [[nodiscard]] QString artworkDirForFile(const QString &filePath) const;

  [[nodiscard]] QString resolveFilePath(const QString &rawEntry,
                                        const CollectionContext &context) const;

  [[nodiscard]] QString resolveRelativeFilePath(const QString &rawFileName,
                                                const QHash<QString, QString> &fileNames) const;

private:
  mutable QMutex m_mutex;
  QHash<QString, QString> m_fileToArtworkDir;
  QHash<QString, int> m_fileToCollectionIndex;
  QHash<QString, QString> m_fileToMediaDir;
  QHash<QString, QString> m_relativeToFullPath;
};

#endif
