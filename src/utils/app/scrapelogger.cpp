// Optional scrape diagnostic logging — see scrapelogger.h. Keeps a
// single appended log file under the app config dir, fed by a chained
// Qt message handler that filters for the `kartend.scrape*` categories.
#include "scrapelogger.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMutex>
#include <QStandardPaths>
#include <QtGlobal>

namespace {

// `kartend.scrape*` is a prefix wildcard: it covers `kartend.scrape`,
// `kartend.scrape.timings` AND `kartend.scraperservice` — all three
// scrape-related categories share the "kartend.scrape" prefix.
constexpr char kScrapeCategoryPrefix[] = "kartend.scrape";
// Only debug+info are toggled — warning/critical stay at their
// compiled-in default (on) so disabling logging never hides a real
// scrape warning. Each rule line is "<category>.<type>=<bool>".
constexpr char kRulesOn[] = "kartend.scrape*.debug=true\nkartend.scrape*.info=true";
constexpr char kRulesOff[] = "kartend.scrape*.debug=false\nkartend.scrape*.info=false";

// Rotate once the live file passes this size so an unattended
// overnight batch can't fill the disk. One previous generation is
// retained as `scrape.log.old`.
constexpr qint64 kMaxLogBytes = 8 * 1024 * 1024;

// Guards every state field below plus all log-file I/O. The message
// handler runs on whichever thread emitted the line — scrape file
// writes happen on a worker thread — so the handler and setEnabled()
// must not race on the file.
QMutex g_mutex;
QFile *g_logFile = nullptr;                  // open + owned while enabled, else nullptr
qint64 g_bytesThisGeneration = 0;            // size of the current g_logFile generation
QtMessageHandler g_chainedHandler = nullptr; // handler installed before ours (stderr sink)
bool g_handlerInstalled = false;
bool g_enabled = false;

const char *levelTag(QtMsgType type) {
  switch (type) {
  case QtDebugMsg:
    return "DEBUG";
  case QtInfoMsg:
    return "INFO ";
  case QtWarningMsg:
    return "WARN ";
  case QtCriticalMsg:
    return "CRIT ";
  case QtFatalMsg:
    return "FATAL";
  }
  return "?????";
}

// Locks the log down to 0600. Even with URL-query redaction, the log can
// still accumulate response fragments + the user's collection structure
// next to the cleartext credentials in kartend.cfg — both live in the
// same per-user config dir.
void restrictLogPermissionsLocked(QFile *file) {
  if (!file) return;
  file->setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

// Caller must hold g_mutex. Closes the live file, shifts it to
// `scrape.log.old` (replacing any prior generation), reopens a fresh
// empty log. On reopen failure g_logFile is cleared so the handler
// stops trying.
void rotateLocked() {
  if (!g_logFile) return;
  const QString path = g_logFile->fileName();
  g_logFile->close();
  const QString oldPath = path + QStringLiteral(".old");
  QFile::remove(oldPath);
  QFile::rename(path, oldPath);
  // Tighten the rotated copy too — it carries the same sensitive context.
  QFile::setPermissions(oldPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  g_bytesThisGeneration = 0;
  if (!g_logFile->open(QIODevice::WriteOnly | QIODevice::Append)) {
    delete g_logFile;
    g_logFile = nullptr;
    return;
  }
  restrictLogPermissionsLocked(g_logFile);
}

void scrapeMessageHandler(QtMsgType type, const QMessageLogContext &context,
                          const QString &message) {
  // Always chain so normal stderr logging keeps working for the whole
  // application — scrape category or not.
  if (g_chainedHandler) {
    g_chainedHandler(type, context, message);
  }
  const char *category = context.category ? context.category : "";
  if (qstrncmp(category, kScrapeCategoryPrefix, sizeof(kScrapeCategoryPrefix) - 1) != 0) {
    return;
  }
  QMutexLocker lock(&g_mutex);
  if (!g_logFile) return; // logging toggled off between filter check and here
  QByteArray line = QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toUtf8();
  line += ' ';
  line += levelTag(type);
  line += ' ';
  line += category;
  line += ": ";
  line += message.toUtf8();
  line += '\n';
  if (g_logFile->write(line) > 0) {
    // Flush every line — the whole point of the file is to survive a
    // crash, so buffered tail lines would defeat it.
    g_logFile->flush();
    g_bytesThisGeneration += line.size();
    if (g_bytesThisGeneration >= kMaxLogBytes) {
      rotateLocked();
    }
  }
}

} // namespace

namespace ScrapeLogger {

QString logFilePath() {
  const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
  return QDir(dir).filePath(QStringLiteral("scrape.log"));
}

bool isEnabled() {
  QMutexLocker lock(&g_mutex);
  return g_enabled;
}

void setEnabled(bool enabled) {
  QMutexLocker lock(&g_mutex);
  if (enabled == g_enabled) return; // idempotent — safe to call on every settings save
  g_enabled = enabled;
  if (enabled) {
    const QString path = logFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    auto *file = new QFile(path);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Append)) {
      // Can't open the log — stay disabled rather than half-enabled.
      delete file;
      g_enabled = false;
      return;
    }
    g_logFile = file;
    restrictLogPermissionsLocked(g_logFile);
    g_bytesThisGeneration = g_logFile->size();
    if (!g_handlerInstalled) {
      // Capture-once + never-uninstall: g_chainedHandler is whatever handler
      // was active at first enable, and scrapeMessageHandler always chains to
      // it so the stderr sink survives. ScrapeLogger is thus the outermost
      // handler by construction — any process-wide handler it must coexist
      // with has to be installed earlier (see the ordering invariant on
      // setEnabled() in scrapelogger.h). A later qInstallMessageHandler that
      // replaces without chaining would silently shadow this one.
      g_chainedHandler = qInstallMessageHandler(scrapeMessageHandler);
      g_handlerInstalled = true;
    }
    QLoggingCategory::setFilterRules(QString::fromLatin1(kRulesOn));
    // Session header so successive runs appended to one file stay
    // visually separable.
    QByteArray header = "\n===== scrape logging enabled ";
    header += QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toUtf8();
    header += " =====\n";
    g_logFile->write(header);
    g_logFile->flush();
    g_bytesThisGeneration += header.size();
  } else {
    QLoggingCategory::setFilterRules(QString::fromLatin1(kRulesOff));
    if (g_logFile) {
      g_logFile->close();
      delete g_logFile;
      g_logFile = nullptr;
    }
    // The handler is left installed: with g_logFile null it is a pure
    // passthrough to g_chainedHandler, and leaving it avoids an
    // uninstall/reinstall race with worker threads still logging.
  }
}

} // namespace ScrapeLogger
