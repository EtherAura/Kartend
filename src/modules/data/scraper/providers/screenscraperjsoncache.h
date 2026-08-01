#ifndef KARTEND_SCREENSCRAPERJSONCACHE_H
#define KARTEND_SCREENSCRAPERJSONCACHE_H

// Shared scaffolding for the ScreenScraper on-disk JSON caches
// (systemesListe + mediasJeuListe). The two caches have identical file-IO,
// path-resolution, staleness, and response-envelope-unwrap logic; only the
// per-element JSON <-> struct mapping differs. These inline helpers DRY the
// common parts so a fix (e.g. to the write path or the envelope shape) lands
// in one place. Header-only/inline so no new translation unit / CMake entry
// is needed. (Kartend-o55g4)

#include "errorutils.h"

#include <QByteArray>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1Char>
#include <QStandardPaths>
#include <QString>

#include "pathutils.h"

namespace ScreenScraperJsonCache {

// Build <CacheLocation>/kartend/<fileName>, or empty when there is no
// writable cache location.
inline QString cachePath(const char *fileName) {
  const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  if (base.isEmpty()) {
    return {};
  }
  return base + QLatin1Char('/') + QStringLiteral("kartend") + QLatin1Char('/') +
         QString::fromLatin1(fileName);
}

// True when the file is missing, unreadable, or older than ttlDays.
inline bool isStale(const QString &filePath, int ttlDays) {
  if (filePath.isEmpty() || !QFileInfo::exists(filePath)) {
    return true;
  }
  const QFileInfo info(filePath);
  const QDateTime modified = info.lastModified();
  if (!modified.isValid()) {
    return true;
  }
  return modified.secsTo(QDateTime::currentDateTime()) > static_cast<qint64>(ttlDays) * 86400;
}

// SS wraps the list under response.<key> (live API) but round-tripped cache
// files store the raw <key> at the top level; tolerate both. Returns an empty
// array when neither shape is present.
inline QJsonArray unwrapArray(const QJsonObject &root, const char *key) {
  const QString k = QString::fromLatin1(key);
  if (root.contains(QStringLiteral("response"))) {
    return root.value(QStringLiteral("response")).toObject().value(k).toArray();
  }
  if (root.contains(k)) {
    return root.value(k).toArray();
  }
  return {};
}

// Atomically write the JSON object as Compact via PathUtils::atomicWriteFile
// (mkpath + QSaveFile + parent-dir fsync — Kartend-7dq4h; the hand-rolled
// version here never fsync'd the directory, so a crash right after commit
// could lose the rename). Kartend-fux2w is why atomicity matters at all:
// previously a plain QFile truncate-write with an unchecked write() — a
// crash or ENOSPC mid-write left a truncated JSON with a FRESH mtime, which
// isStale() then treated as valid for the whole TTL (days of
// parse-fail-on-every-load). The helper logs the failing stage itself; the
// caller-context warning here names which cache write failed.
inline bool writeJsonCompact(const QString &filePath, const QJsonObject &root, const char *context,
                             const char *writeErrMsg) {
  if (filePath.isEmpty()) {
    return false;
  }
  const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Compact);
  if (!PathUtils::atomicWriteFile(filePath, payload)) {
    ErrorUtils::logError(ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::FileWriteError,
                                                           writeErrMsg, context)
                             .withDetails(filePath));
    return false;
  }
  return true;
}

} // namespace ScreenScraperJsonCache

#endif // KARTEND_SCREENSCRAPERJSONCACHE_H
