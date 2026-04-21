// Executes SQLite queries on worker thread for paginated item loading and
// filtering.
#include "querymanager.h"
#include "collectionutils.h"
#include "dbmigrations.h"
#include "errorutils.h"
#include "pathutils.h"
#include "querymanagerhelpers.h"
#include "sessionmanager.h"
#include "uiconstants.h"
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QRegularExpression>
#include <QRunnable>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QVector>
#include <QWaitCondition>
#include <QtConcurrent>
#include <QtGlobal>
#include <algorithm>
#include <atomic>
#include <random>
#include <stdexcept>

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcQueryManager, "kartend.querymanager")
#define debugLog(msg) qCDebug(lcQueryManager) << msg

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;
using QueryManagerInternal::canonicalKeyPath;
using QueryManagerInternal::displayNameForBase;

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

QueryManager::QueryManager(SessionManager *sessionManager,
                           const QString &connectionName, QObject *parent)
    : QObject(parent),
      m_scanCancellationToken(std::make_shared<std::atomic_bool>(false)),
      m_sessionManager(sessionManager), m_connectionName(connectionName) {
  // Pointer-based cache with automatic LRU eviction.
  m_statementCache.setMaxCost(MAX_STATEMENT_CACHE_SIZE);

  const int idealThreads = QThread::idealThreadCount();
  const int base =
      idealThreads > 0
          ? (idealThreads / UIConstants::Concurrency::WORKER_POOL_DIVISOR)
          : UIConstants::Concurrency::WORKER_POOL_MIN_THREADS;
  m_scanThreadPool = new QThreadPool();
  m_scanThreadPool->setMaxThreadCount(
      std::clamp(base, UIConstants::Concurrency::WORKER_POOL_MIN_THREADS,
                 UIConstants::Concurrency::WORKER_POOL_MAX_THREADS));

  // Register ErrorContext for queued signal/slot connections
  qRegisterMetaType<ErrorUtils::ErrorContext>("ErrorUtils::ErrorContext");
}

QueryManager::~QueryManager() {
  // Abandon the thread pool without waiting - process is exiting anyway.
  if (m_scanThreadPool) {
    m_scanThreadPool->clear();
    // Intentionally NOT deleting - ~QThreadPool blocks. Let OS clean up.
    m_scanThreadPool = nullptr;
  }

  clearStatementCache();
  if (m_db.isValid()) {
    QString connectionName = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
  }
}

void QueryManager::requestCancelScan() {
  if (m_scanCancellationToken) {
    m_scanCancellationToken->store(true, std::memory_order_release);
  }
  // Drop any queued scan work that hasn't started yet.
  if (m_scanThreadPool) {
    m_scanThreadPool->clear();
  }
}

bool QueryManager::isScanCancelled() const {
  return m_scanCancellationToken &&
         m_scanCancellationToken->load(std::memory_order_acquire);
}

void QueryManager::resetScanCancellation() {
  m_scanCancellationToken = std::make_shared<std::atomic_bool>(false);
}

// Forces the connection to see the latest WAL commits from other connections.
// In SQLite WAL mode, a connection can hold onto a stale read snapshot if it
// has an open transaction or cached statements. This method ensures we see
// data committed by the scan worker running on a separate connection.

// ... Helper implementations ...

// SQL constants for prepared statement caching live in a shared header so
// sibling translation units (querymanagerlifecycle.cpp) can reuse them.
#include "querymanagersql.h"

