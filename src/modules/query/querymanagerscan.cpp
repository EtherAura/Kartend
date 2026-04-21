// Filesystem scan + rescan-needed checks extracted from querymanager.cpp:
//   - needsRescan
//   - scanMediaDirectory
//   - loadOrScanCollection
// Members of QueryManager; access existing class state.
//
// Anonymous-namespace scan helpers are duplicated here (matching the
// querymanagerlifecycle.cpp pattern) so this TU does not depend on private
// symbols of the main TU.
#include "querymanager.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMutex>
#include <QRunnable>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QVector>
#include <QWaitCondition>
#include <atomic>
#include <memory>

#include "collectionutils.h"
#include "querymanagersql.h"
#include "uiconstants.h"

Q_DECLARE_LOGGING_CATEGORY(lcQueryManager)

namespace {
// Result struct for parallel directory scanning
// Holds files found in a single directory plus their timestamps
struct DirectoryScanResult {
  QStringList relativePaths;
  QHash<QString, QDateTime> timestamps;
};

struct DirSignatureSample {
  QString relPath; // Relative to collection root. Empty means root.
  qint64 mtimeSec = 0;
};

static auto addDirSignatureSample(QVector<DirSignatureSample> &samples,
                                  const DirSignatureSample &candidate,
                                  int maxSamples) -> void {
  if (maxSamples <= 0) {
    return;
  }
  for (const auto &s : samples) {
    if (s.relPath == candidate.relPath) {
      return;
    }
  }

  if (samples.size() < maxSamples) {
    samples.append(candidate);
  } else {
    int minIndex = 0;
    for (int i = 1; i < samples.size(); ++i) {
      if (samples[i].mtimeSec < samples[minIndex].mtimeSec) {
        minIndex = i;
      }
    }
    if (candidate.mtimeSec <= samples[minIndex].mtimeSec) {
      return;
    }
    samples[minIndex] = candidate;
  }
}

static auto buildFtsPrefixQuery(const QString &raw) -> QString {
  QString trimmed = raw.trimmed();
  if (trimmed.isEmpty()) {
    return {};
  }

  // Sanitize into simple terms to avoid FTS query parser edge-cases.
  // Keep letters/numbers/underscore; replace everything else with spaces.
  QString cleaned = trimmed;
  cleaned.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}_]+")),
                  QStringLiteral(" "));
  const QStringList terms = cleaned.split(' ', Qt::SkipEmptyParts);
  if (terms.isEmpty()) {
    return {};
  }

  QStringList tokens;
  tokens.reserve(terms.size());
  for (const QString &t : terms) {
    if (t.isEmpty()) {
      continue;
    }
    tokens.append(t + "*");
  }
  if (tokens.isEmpty()) {
    return {};
  }
  return tokens.join(QStringLiteral(" AND "));
}

static auto buildDirSignatureJson(bool includeSubfolders,
                                  const QVector<DirSignatureSample> &samples)
    -> QString {
  QJsonObject root;
  root.insert(QStringLiteral("v"), 1);
  root.insert(QStringLiteral("sub"), includeSubfolders);

  QJsonArray arr;
  for (const auto &s : samples) {
    QJsonObject o;
    o.insert(QStringLiteral("p"), s.relPath);
    o.insert(QStringLiteral("t"), static_cast<double>(s.mtimeSec));
    arr.append(o);
  }
  root.insert(QStringLiteral("s"), arr);
  return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

static auto parseDirSignatureJson(const QString &json,
                                  bool &includeSubfoldersOut,
                                  QVector<DirSignatureSample> &samplesOut)
    -> bool {
  includeSubfoldersOut = false;
  samplesOut.clear();

  if (json.trimmed().isEmpty()) {
    return false;
  }

  QJsonParseError err{};
  const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    return false;
  }

  const QJsonObject obj = doc.object();
  if (obj.value(QStringLiteral("v")).toInt() != 1) {
    return false;
  }

  includeSubfoldersOut = obj.value(QStringLiteral("sub")).toBool(false);

  const QJsonValue sVal = obj.value(QStringLiteral("s"));
  if (!sVal.isArray()) {
    return false;
  }
  const QJsonArray arr = sVal.toArray();
  samplesOut.reserve(arr.size());
  for (const auto &v : arr) {
    if (!v.isObject()) {
      continue;
    }
    const QJsonObject o = v.toObject();
    DirSignatureSample s;
    s.relPath = o.value(QStringLiteral("p")).toString();
    s.mtimeSec =
        static_cast<qint64>(o.value(QStringLiteral("t")).toDouble(0.0));
    samplesOut.append(std::move(s));
  }
  return !samplesOut.isEmpty();
}

static auto seedDirSignatureFromFilesystem(const QString &rootPath,
                                           bool includeSubfolders) -> QString {
  QVector<DirSignatureSample> samples;
  samples.reserve(UIConstants::Database::DIR_SIGNATURE_SAMPLE_COUNT);

  QFileInfo rootInfo(rootPath);
  if (!rootInfo.exists()) {
    return QString();
  }
  addDirSignatureSample(
      samples,
      DirSignatureSample{QString(), rootInfo.lastModified().toSecsSinceEpoch()},
      UIConstants::Database::DIR_SIGNATURE_SAMPLE_COUNT);

  if (includeSubfolders) {
    QDirIterator it(rootPath, QDir::Dirs | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    int inspected = 0;
    while (it.hasNext() &&
           inspected < UIConstants::Database::DIR_SIGNATURE_SEED_MAX_DIRS) {
      it.next();
      const QString absPath = it.filePath();
      const QString relPath = QDir(rootPath).relativeFilePath(absPath);
      const qint64 mtimeSec =
          QFileInfo(absPath).lastModified().toSecsSinceEpoch();
      addDirSignatureSample(samples, DirSignatureSample{relPath, mtimeSec},
                            UIConstants::Database::DIR_SIGNATURE_SAMPLE_COUNT);
      ++inspected;
    }
  }

  return buildDirSignatureJson(includeSubfolders, samples);
}

static auto dirSignatureStillValid(const QString &rootPath,
                                   bool includeSubfolders,
                                   const QString &storedSignature) -> bool {
  bool storedSub = false;
  QVector<DirSignatureSample> samples;
  if (!parseDirSignatureJson(storedSignature, storedSub, samples)) {
    return false;
  }
  if (storedSub != includeSubfolders) {
    return false;
  }
  if (samples.isEmpty()) {
    return false;
  }

  QDir root(rootPath);
  for (const auto &s : samples) {
    const QString absPath =
        s.relPath.isEmpty() ? rootPath : root.absoluteFilePath(s.relPath);
    QFileInfo info(absPath);
    if (!info.exists()) {
      return false;
    }
    const qint64 currentSec = info.lastModified().toSecsSinceEpoch();
    if (currentSec != s.mtimeSec) {
      return false;
    }
  }

  return true;
}

struct ScanCompletionQueue {
  QMutex mutex;
  QWaitCondition hasResult;
  QWaitCondition hasSpace;
  QVector<DirectoryScanResult> ready;
  int inFlight = 0;
};

class DirectoryScanTask final : public QRunnable {
public:
  DirectoryScanTask(QString dirPath, QString rootPath, QStringList nameFilters,
                    std::shared_ptr<std::atomic_bool> cancelToken,
                    ScanCompletionQueue *queue)
      : m_dirPath(std::move(dirPath)), m_rootPath(std::move(rootPath)),
        m_nameFilters(std::move(nameFilters)),
        m_cancelToken(std::move(cancelToken)), m_queue(queue) {
    setAutoDelete(true);
  }

  void run() override {
    if (!m_queue || !m_cancelToken) {
      return;
    }

    // Scan this directory (non-recursively) but emit bounded chunks so a single
    // huge folder cannot allocate an unbounded QStringList/QHash in memory.
    QDir rootDir(m_rootPath);
    QDirIterator iterator(m_dirPath, m_nameFilters, QDir::Files,
                          QDirIterator::NoIteratorFlags);

    auto pushChunk = [&](DirectoryScanResult &&chunk) {
      if (!m_queue) {
        return;
      }

      // Backpressure: block (with timeout) if the queue is full, so memory
      // stays bounded even when directory scans outpace the consumer.
      QMutexLocker locker(&m_queue->mutex);
      while (m_queue->ready.size() >=
                 UIConstants::Database::SCAN_READY_MAX_RESULTS &&
             !m_cancelToken->load(std::memory_order_acquire)) {
        m_queue->hasSpace.wait(&m_queue->mutex, 50);
      }

      if (m_cancelToken->load(std::memory_order_acquire)) {
        return;
      }

      m_queue->ready.append(std::move(chunk));
      m_queue->hasResult.wakeOne();
    };

    DirectoryScanResult chunk;
    chunk.relativePaths.reserve(
        UIConstants::Database::SCAN_DIR_RESULT_CHUNK_SIZE);
    chunk.timestamps.reserve(UIConstants::Database::SCAN_DIR_RESULT_CHUNK_SIZE *
                             2);

    while (iterator.hasNext()) {
      if (m_cancelToken->load(std::memory_order_acquire)) {
        break;
      }

      iterator.next();
      const QString filePath = iterator.filePath();
      const QString relativePath = rootDir.relativeFilePath(filePath);
      const QFileInfo info = iterator.fileInfo();

      chunk.relativePaths.append(relativePath);
      chunk.timestamps.insert(relativePath, info.lastModified());

      if (chunk.relativePaths.size() >=
          UIConstants::Database::SCAN_DIR_RESULT_CHUNK_SIZE) {
        pushChunk(std::move(chunk));
        chunk = DirectoryScanResult{};
        chunk.relativePaths.reserve(
            UIConstants::Database::SCAN_DIR_RESULT_CHUNK_SIZE);
        chunk.timestamps.reserve(
            UIConstants::Database::SCAN_DIR_RESULT_CHUNK_SIZE * 2);
      }
    }

    if (!m_cancelToken->load(std::memory_order_acquire) &&
        !chunk.relativePaths.isEmpty()) {
      pushChunk(std::move(chunk));
    }

    // Mark this directory task as complete.
    {
      QMutexLocker locker(&m_queue->mutex);
      --m_queue->inFlight;
      m_queue->hasResult.wakeOne();
    }
  }

private:
  QString m_dirPath;
  QString m_rootPath;
  QStringList m_nameFilters;
  std::shared_ptr<std::atomic_bool> m_cancelToken;
  ScanCompletionQueue *m_queue = nullptr;
};

class SynchronousPragmaGuard {
public:
  explicit SynchronousPragmaGuard(QSqlDatabase &db) : m_db(db) {}

  SynchronousPragmaGuard(const SynchronousPragmaGuard &) = delete;
  auto operator=(const SynchronousPragmaGuard &)
      -> SynchronousPragmaGuard & = delete;

  SynchronousPragmaGuard(SynchronousPragmaGuard &&) = delete;
  auto operator=(SynchronousPragmaGuard &&)
      -> SynchronousPragmaGuard & = delete;

  ~SynchronousPragmaGuard() {
    if (!m_db.isOpen()) {
      return;
    }
    QSqlQuery pragmaOn(m_db);
    pragmaOn.exec("PRAGMA synchronous = NORMAL");
  }

private:
  QSqlDatabase &m_db;
};
} // namespace

bool QueryManager::needsRescan(int collectionIndex,
                               const CollectionConfig &collection) {
  Q_UNUSED(collectionIndex)

  if (collection.mediaDirectory.trimmed().isEmpty()) {
    if (m_db.isOpen()) {
      const QString uuid = CollectionUtils::computeCollectionUuid(
          collection.name, collection.mediaDirectory);
      clearCollectionFromDatabaseByUuid(uuid);
    }
    return false;
  }

  // Include includeContentSubfolders in the signature - changing it requires
  // rescan
  QString currentSignature = collection.extensions.isEmpty()
                                 ? QString()
                                 : collection.extensions.join('|');
  currentSignature += collection.includeContentSubfolders ? "|subfolders" : "";

  const QString uuid = CollectionUtils::computeCollectionUuid(
      collection.name, collection.mediaDirectory);

  // Use cached prepared statement for collection info lookup
  QSqlQuery &query = getPreparedStatement(QuerySQL::COLLECTION_INFO);
  query.bindValue(0, uuid);

  bool rowPresent = query.exec() && query.next();
  if (!rowPresent) {
    return true;
  }

  QString storedName = query.value(1).toString();
  QString storedSignature = query.value(2).toString();
  QString storedDirSignature = query.value(3).toString();

  // Capture lastScanned before any early returns that might invalidate query
  // state
  QDateTime lastScanned =
      QDateTime::fromString(query.value(0).toString(), Qt::ISODate);

  if (storedName != collection.name) {
    return true;
  }

  // If an older DB is missing ext_signature metadata, don't force a full
  // rescan. Seed it from the current config when items already exist.
  if (storedSignature != currentSignature) {
    QSqlQuery &countQuery = getPreparedStatement(QuerySQL::ITEMS_COUNT_BY_UUID);
    countQuery.bindValue(0, uuid);
    const bool hasItems = (countQuery.exec() && countQuery.next() &&
                           countQuery.value(0).toInt() > 0);
    if (hasItems && storedSignature.trimmed().isEmpty()) {
      QSqlQuery update(m_db);
      update.prepare("UPDATE collections SET ext_signature = ? WHERE uuid = ?");
      update.addBindValue(currentSignature);
      update.addBindValue(uuid);
      (void)update.exec();
    } else {
      return true;
    }
  }

  // Use cached prepared statement for item path check
  QSqlQuery &pathQuery = getPreparedStatement(QuerySQL::ITEM_PATH_CHECK);
  pathQuery.bindValue(0, uuid);

  if (pathQuery.exec() && pathQuery.next()) {
    QString storedPath = pathQuery.value(0).toString();
    QString storedFullPath =
        QDir(collection.mediaDirectory).absoluteFilePath(storedPath);
    if (!QFile::exists(storedFullPath)) {
      return true;
    }
  }

  QFileInfo dirInfo(collection.mediaDirectory);

  if (!dirInfo.exists()) {
    return true;
  }

  // When includeContentSubfolders is enabled, check for subdirectory
  // modifications. For large collections, this check is expensive (iterates all
  // subdirs). Skip deep check if collection has items in DB - trust cached data
  // on startup. Full validation happens when user navigates into subfolders or
  // forces refresh.
  if (collection.includeContentSubfolders) {
    // If we have items in the database, validate the stored directory signature
    // by checking a bounded set of sampled directories (cheap, avoids deep
    // scans).
    QSqlQuery &countQuery = getPreparedStatement(QuerySQL::ITEMS_COUNT_BY_UUID);
    countQuery.bindValue(0, uuid);
    const bool hasItems = (countQuery.exec() && countQuery.next() &&
                           countQuery.value(0).toInt() > 0);
    if (!hasItems) {
      return true;
    }

    if (!storedDirSignature.trimmed().isEmpty()) {
      if (!dirSignatureStillValid(collection.mediaDirectory, true,
                                  storedDirSignature)) {
        return true;
      }
    } else {
      // Older DBs may have no dir_signature. Avoid forcing a full rescan when
      // items already exist; seed a bounded signature from the filesystem.
      const QString seeded =
          seedDirSignatureFromFilesystem(collection.mediaDirectory, true);
      if (seeded.trimmed().isEmpty()) {
        return true;
      }

      // Preserve the existing last_scanned value while recording the signature.
      QSqlQuery &meta =
          getPreparedStatement(QuerySQL::UPDATE_COLLECTION_SCAN_METADATA);
      const QString lastScannedIso =
          lastScanned.isValid()
              ? lastScanned.toString(Qt::ISODate)
              : QDateTime::currentDateTime().toString(Qt::ISODate);
      meta.bindValue(0, lastScannedIso);
      meta.bindValue(1, seeded);
      meta.bindValue(2, uuid);
      (void)meta.exec();
    }
  } else {
    // Flat collections: directory mtime is a sufficient cheap proxy for
    // new/deleted files.
    if (dirInfo.lastModified() > lastScanned) {
      return true;
    }
  }

  // Use cached prepared statement for modified items count
  QSqlQuery &newer = getPreparedStatement(QuerySQL::ITEMS_MODIFIED_COUNT);
  newer.bindValue(0, uuid);
  newer.bindValue(1, lastScanned.toString(Qt::ISODate));
  newer.exec();

  return newer.next() && newer.value(0).toInt() > 0;
}

QStringList
QueryManager::scanMediaDirectory(const CollectionConfig &collection,
                                 QHash<QString, QDateTime> &timestamps,
                                 QString *dirSignatureOut) {
  QStringList filePaths;
  QDir dir(collection.mediaDirectory);

  if (!dir.exists()) {
    return filePaths;
  }

  if (dirSignatureOut) {
    *dirSignatureOut = QString();
  }

  QStringList nameFilters;
  if (!collection.extensions.isEmpty()) {
    for (const QString &ext : collection.extensions) {
      nameFilters << "*." + ext;
    }
  }

  // For non-recursive scans or small directories, use sequential scanning
  // Parallel scanning has overhead that only pays off with multiple directories
  if (!collection.includeContentSubfolders) {
    if (dirSignatureOut) {
      *dirSignatureOut =
          seedDirSignatureFromFilesystem(dir.absolutePath(), false);
    }

    // Throttle scan progress emissions to avoid spamming the UI event loop.
    QElapsedTimer progressTimer;
    progressTimer.start();
    qint64 lastProgressEmitMs =
        -UIConstants::Database::SCAN_PROGRESS_MIN_INTERVAL_MS;
    auto maybeEmitScanProgress = [&](int processed, int total,
                                     bool force = false) {
      if (force) {
        emit scanItemsProgress(processed, total);
        lastProgressEmitMs = progressTimer.elapsed();
        return;
      }
      const qint64 nowMs = progressTimer.elapsed();
      if (nowMs - lastProgressEmitMs <
          UIConstants::Database::SCAN_PROGRESS_MIN_INTERVAL_MS) {
        return;
      }
      emit scanItemsProgress(processed, total);
      lastProgressEmitMs = nowMs;
    };

    // Sequential scan for flat directories (original behavior)
    constexpr int PROGRESS_REPORT_INTERVAL = 500;
    int itemsScanned = 0;

    QDirIterator iterator(dir.absolutePath(), nameFilters, QDir::Files,
                          QDirIterator::NoIteratorFlags);
    while (iterator.hasNext()) {
      if (isScanCancelled()) {
        filePaths.clear();
        timestamps.clear();
        return filePaths;
      }

      iterator.next();
      const QString relativePath = iterator.fileName();
      const QFileInfo info = iterator.fileInfo();
      filePaths.append(relativePath);
      timestamps[relativePath] = info.lastModified();

      ++itemsScanned;
      if (itemsScanned % PROGRESS_REPORT_INTERVAL == 0) {
        maybeEmitScanProgress(itemsScanned, -1);
      }
    }

    if (itemsScanned > 0) {
      maybeEmitScanProgress(itemsScanned, -1, true);
    }
    return filePaths;
  }

  // Parallel scanning for recursive directory structures
  // Scan directories in parallel with bounded in-flight tasks and consume
  // results as they complete to avoid head-of-line blocking.
  QElapsedTimer scanTimer;
  scanTimer.start();

  const QString rootPath = dir.absolutePath();
  const QDir rootDir(rootPath);
  const auto cancelToken = m_scanCancellationToken;
  if (!cancelToken) {
    return filePaths;
  }
  const std::atomic<bool> &cancelFlag = *cancelToken;

  const int maxThreads =
      m_scanThreadPool ? std::max(1, m_scanThreadPool->maxThreadCount()) : 1;
  const int maxInFlight = std::max(1, maxThreads * 2);
  ScanCompletionQueue queue;

  QVector<DirSignatureSample> signatureSamples;
  signatureSamples.reserve(UIConstants::Database::DIR_SIGNATURE_SAMPLE_COUNT);
  {
    QFileInfo rootInfo(rootPath);
    addDirSignatureSample(
        signatureSamples,
        DirSignatureSample{QString(),
                           rootInfo.lastModified().toSecsSinceEpoch()},
        UIConstants::Database::DIR_SIGNATURE_SAMPLE_COUNT);
  }

  auto enqueue = [&](const QString &dirPath) {
    if (cancelFlag.load(std::memory_order_acquire)) {
      return;
    }
    if (!m_scanThreadPool) {
      return;
    }
    {
      QMutexLocker locker(&queue.mutex);
      ++queue.inFlight;
    }
    m_scanThreadPool->start(new DirectoryScanTask(
        dirPath, rootPath, nameFilters, cancelToken, &queue));
  };

  // Always scan root.
  enqueue(rootPath);

  QDirIterator dirIterator(rootPath, QDir::Dirs | QDir::NoDotAndDotDot,
                           QDirIterator::Subdirectories);

  int totalItemsScanned = 0;
  constexpr int PROGRESS_REPORT_INTERVAL = 500;
  int lastReportedCount = 0;
  int directoriesEnqueued = 1; // root
  int directoryResultsConsumed = 0;

  // Throttle scan progress emissions to avoid spamming the UI event loop.
  QElapsedTimer progressTimer;
  progressTimer.start();
  qint64 lastProgressEmitMs =
      -UIConstants::Database::SCAN_PROGRESS_MIN_INTERVAL_MS;
  auto maybeEmitScanProgress = [&](int processed, int total,
                                   bool force = false) {
    if (force) {
      emit scanItemsProgress(processed, total);
      lastProgressEmitMs = progressTimer.elapsed();
      return;
    }
    const qint64 nowMs = progressTimer.elapsed();
    if (nowMs - lastProgressEmitMs <
        UIConstants::Database::SCAN_PROGRESS_MIN_INTERVAL_MS) {
      return;
    }
    emit scanItemsProgress(processed, total);
    lastProgressEmitMs = nowMs;
  };

  while (!cancelFlag.load(std::memory_order_acquire)) {
    // Fill in-flight queue.
    while (dirIterator.hasNext() &&
           !cancelFlag.load(std::memory_order_acquire)) {
      int inFlight = 0;
      {
        QMutexLocker locker(&queue.mutex);
        inFlight = queue.inFlight;
      }
      if (inFlight >= maxInFlight) {
        break;
      }
      dirIterator.next();
      const QFileInfo dirInfo = dirIterator.fileInfo();
      const QString dirPath = dirIterator.filePath();
      enqueue(dirPath);
      {
        const QString relPath = rootDir.relativeFilePath(dirPath);
        const qint64 mtimeSec = dirInfo.lastModified().toSecsSinceEpoch();
        addDirSignatureSample(
            signatureSamples, DirSignatureSample{relPath, mtimeSec},
            UIConstants::Database::DIR_SIGNATURE_SAMPLE_COUNT);
      }
      ++directoriesEnqueued;
    }

    DirectoryScanResult result;
    bool gotResult = false;
    bool done = false;

    {
      QMutexLocker locker(&queue.mutex);
      while (queue.ready.isEmpty() && queue.inFlight > 0 &&
             !cancelFlag.load(std::memory_order_acquire)) {
        queue.hasResult.wait(&queue.mutex, 50);
      }

      if (!queue.ready.isEmpty()) {
        result = std::move(queue.ready.back());
        queue.ready.removeLast();
        queue.hasSpace.wakeOne();
        gotResult = true;
      } else if (queue.inFlight == 0 && !dirIterator.hasNext()) {
        done = true;
      }
    }

    if (done) {
      break;
    }
    if (!gotResult) {
      continue;
    }

    ++directoryResultsConsumed;

    if (result.relativePaths.isEmpty()) {
      continue;
    }

    filePaths.reserve(filePaths.size() + result.relativePaths.size());
    filePaths.append(result.relativePaths);

    timestamps.reserve(timestamps.size() + result.timestamps.size());
    for (auto it = result.timestamps.constBegin();
         it != result.timestamps.constEnd(); ++it) {
      timestamps.insert(it.key(), it.value());
    }

    totalItemsScanned += result.relativePaths.size();
    if (totalItemsScanned - lastReportedCount >= PROGRESS_REPORT_INTERVAL) {
      lastReportedCount = totalItemsScanned;
      maybeEmitScanProgress(totalItemsScanned, -1);
    }
  }

  // Ensure all worker tasks have finished before destroying the queue.
  // This is critical on cancellation, where we may exit early.
  {
    QMutexLocker locker(&queue.mutex);
    while (queue.inFlight > 0) {
      queue.hasResult.wait(&queue.mutex, 50);
    }
    queue.ready.clear();
  }

  if (lcQueryManager().isDebugEnabled()) {
    qCDebug(lcQueryManager)
        << "Recursive scan done"
        << "collection=" << collection.name << "cancelled="
        << (cancelFlag.load(std::memory_order_acquire) ? "yes" : "no")
        << "dirsEnqueued=" << directoriesEnqueued
        << "dirResults=" << directoryResultsConsumed
        << "filesFound=" << totalItemsScanned
        << "elapsedMs=" << scanTimer.elapsed();
  }

  // Check if cancelled during parallel scan
  if (isScanCancelled()) {
    filePaths.clear();
    timestamps.clear();
    return filePaths;
  }

  if (dirSignatureOut) {
    *dirSignatureOut = buildDirSignatureJson(true, signatureSamples);
  }

  // Emit final progress
  if (totalItemsScanned > 0) {
    maybeEmitScanProgress(totalItemsScanned, -1, true);
  }

  return filePaths;
}

QStringList
QueryManager::loadOrScanCollection(int collectionIndex,
                                   const CollectionConfig &collection,
                                   QHash<QString, QDateTime> &timestamps) {
  QStringList filePaths;
  if (collection.mediaDirectory.trimmed().isEmpty()) {
    return filePaths;
  }

  if (needsRescan(collectionIndex, collection)) {
    QString dirSignature;
    filePaths = scanMediaDirectory(collection, timestamps, &dirSignature);
    if (!filePaths.isEmpty()) {
      saveItemsToDatabase(collectionIndex, filePaths, timestamps, collection,
                          dirSignature);
    }
  } else {
    const QString uuid = CollectionUtils::computeCollectionUuid(
        collection.name, collection.mediaDirectory);
    filePaths = loadItemsFromDatabaseByUuid(uuid);
  }

  return filePaths;
}

