// Sibling translation unit for ArtworkManager.
// Extracted from artworkmanager.cpp during LOC-reduction refactor.
// These remain ArtworkManager members; this is a translation-unit split.
#include "artworkmanager.h"
#include "loggingcategories.h"
#include "applicationcontext.h"
#include "artworkutils.h"
#include "cachemanager.h"
#include "collectionutils.h"
#include "extensionutils.h"
#include "interactionstateholder.h"
#include "propertyutils.h"
#include "setuputils.h"
#include "timerutils.h"
#include "ui/widgets/itemwidget.h"
#include "uiconstants.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QtConcurrent>
#include <algorithm>
#include <functional>

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcArtworkManager)
#define debugLog(msg) qCDebug(lcArtworkManager) << msg

void ArtworkManager::buildArtworkPathsList() {
  {
    QMutexLocker locker(&m_dataMutex);
    m_allArtworkPaths.clear();
    m_silentLoadIndex = 0;
  }

  if (!currentCollectionIndex || *currentCollectionIndex < 0 || !collections ||
      *currentCollectionIndex >= collections->size()) {
    return;
  }

  const CollectionConfig &collection = (*collections)[*currentCollectionIndex];

  // PHASE 1: Collect all directory paths first (fast, no I/O)
  QSet<QString> processedDirectories;
  std::function<void(int)> collectDirsRecursive = [&](int parentIdx) {
    for (int i = 0; i < collections->size(); ++i) {
      if ((*collections)[i].parentCollectionIndex == parentIdx) {
        QString artDir = (*collections)[i].artworkDirectory;
        if (!artDir.isEmpty()) {
          processedDirectories.insert(QDir(artDir).absolutePath());
        }
        collectDirsRecursive(i);
      }
    }
  };

  // Add main collection directory
  if (!collection.artworkDirectory.isEmpty()) {
    processedDirectories.insert(
        QDir(collection.artworkDirectory).absolutePath());
  }

  if (collection.showAllSubcollectionItems) {
    // Collect all subcollection directories
    for (int i = 0; i < collections->size(); ++i) {
      if ((*collections)[i].parentCollectionIndex == *currentCollectionIndex) {
        QString artDir = (*collections)[i].artworkDirectory;
        if (!artDir.isEmpty()) {
          processedDirectories.insert(QDir(artDir).absolutePath());
        }
        collectDirsRecursive(i);
      }
    }

    // PHASE 2: Start dentry warmup IMMEDIATELY in background (parallel)
    QStringList allDirs = processedDirectories.values();
    QThreadPool::globalInstance()->start([allDirs]() {
      auto &cache = ArtworkUtils::DirectoryCache::instance();
      cache.prewarmDirectories(allDirs);
      cache.processQueuedDirectories();
        qCDebug(lcPerfTrace) << "Background dentry warmup complete: dirs="
                   << allDirs.size();
    });

    // PHASE 3: Build artwork paths list in parallel
    // Use QtConcurrent to scan all directories simultaneously
    QMutex pathsMutex;
    QtConcurrent::blockingMap(
        allDirs, [this, &pathsMutex](const QString &dirPath) {
          QDir dir(dirPath);
          if (!dir.exists()) {
            return;
          }
          const QStringList exts = ExtensionUtils::imageFilters();
          dir.setNameFilters(exts);
          const QStringList files = dir.entryList(QDir::Files);
          if (!files.isEmpty()) {
            QStringList fullPaths;
            fullPaths.reserve(files.size());
            for (const QString &file : files) {
              fullPaths.append(dir.absoluteFilePath(file));
            }
            QMutexLocker locker(&pathsMutex);
            QMutexLocker dataLocker(&m_dataMutex);
            m_allArtworkPaths.append(fullPaths);
          }
        });
  } else {
    // Single collection - just scan the one directory
    appendArtworkFromDir(collection.artworkDirectory, processedDirectories);
  }
}

// Recursively add artwork paths from descendant subcollections with
// deduplication
void ArtworkManager::addSubcollectionArtworkPathsWithDedup(
    int parentIndex, QSet<QString> &processedDirectories) {
  if (!collections || parentIndex < 0 || parentIndex >= collections->size()) {
    return;
  }

  for (int i = 0; i < collections->size(); ++i) {
    if ((*collections)[i].parentCollectionIndex == parentIndex) {
      appendArtworkFromDir((*collections)[i].artworkDirectory,
                           processedDirectories);
      addSubcollectionArtworkPathsWithDedup(i, processedDirectories);
    }
  }
}

// Initializes persistent cache from disk
void ArtworkManager::initializeCache() {
  if (m_cacheManager) {
    m_cacheManager->initialize();
  }
}

// Loads artwork, processes it, and caches only in CacheManager to avoid
// duplicate in-memory residency
auto ArtworkManager::loadArtworkFromFile(const QString &artworkPath)
    -> QPixmap {
  QPixmap cached = getCachedPixmap(artworkPath);
  if (!cached.isNull()) {
    return cached;
  }

  if (!QFile::exists(artworkPath)) {
    return {};
  }

  QPixmap pixmap(artworkPath);
  if (pixmap.isNull()) {
    return {};
  }

  QPixmap processedPixmap = ArtworkManager::createProcessedArtwork(pixmap);
  if (!processedPixmap.isNull() && m_cacheManager) {
    m_cacheManager->cacheArtwork(artworkPath, processedPixmap);
  }

  return processedPixmap;
}

// Delegates to ArtworkUtils::findArtworkForFile for artwork path resolution
auto ArtworkManager::findArtworkForFile(const QString &fileName,
                                        const QString &artworkDirectory)
    -> QString {
  return ArtworkUtils::findArtworkForFile(fileName, artworkDirectory);
}

// Clears widget references and in-memory state related to artwork loading
