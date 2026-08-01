#include "scrapependingstate.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLoggingCategory>
#include <QStandardPaths>

#include "pathutils.h"

namespace Scraper {
namespace ScrapePendingState {

namespace {
Q_LOGGING_CATEGORY(lcScrapePendingState, "kartend.scrapependingstate", QtWarningMsg)
// See the header's versioning note.
constexpr int kCurrentStateVersion = 2;
constexpr int kMinSupportedStateVersion = 1;
} // namespace

QString filePath() {
  const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
  return QDir(dir).filePath(QStringLiteral("pending-scrape.json"));
}

QString lockFilePath() {
  return filePath() + QStringLiteral(".lock");
}

QByteArray serialize(const ScraperService::PendingState &snap) {
  QJsonObject root;
  root[QStringLiteral("version")] = kCurrentStateVersion;
  root[QStringLiteral("started_at_unix_ms")] = snap.startedAtUnixMs;
  root[QStringLiteral("mode")] = snap.mode == ScraperService::Mode::Auto
                                     ? QStringLiteral("auto")
                                     : QStringLiteral("interactive");
  root[QStringLiteral("write_metadata")] = snap.writeMetadata;
  QJsonArray filterArr;
  for (const auto &t : snap.mediaFilter) filterArr.append(t);
  root[QStringLiteral("media_filter")] = filterArr;
  QJsonObject sumObj;
  const ScraperService::Summary &summary = snap.summarySoFar;
  sumObj[QStringLiteral("scraped")] = summary.scraped;
  sumObj[QStringLiteral("skipped")] = summary.skipped;
  sumObj[QStringLiteral("errors")] = summary.errors;
  sumObj[QStringLiteral("not_found")] = summary.notFound;
  sumObj[QStringLiteral("media_written")] = summary.mediaWritten;
  // Media failure counters mirror the runner's; without them a resumed run
  // under-reports its media failures in the final dialog (Kartend-jjyst.16).
  sumObj[QStringLiteral("media_fetch_failures")] = summary.mediaFetchFailures;
  sumObj[QStringLiteral("media_write_failures")] = summary.mediaWriteFailures;
  QJsonArray failArr;
  for (const auto &f : summary.firstFailures) failArr.append(f);
  sumObj[QStringLiteral("first_failures")] = failArr;
  // failedItems drives the "Re-scrape failed items" button and
  // sidecarFailures/quotaExhausted the summary readout — without these a
  // resumed run comes back with an empty failure list and a reset count.
  sumObj[QStringLiteral("sidecar_failures")] = summary.sidecarFailures;
  sumObj[QStringLiteral("quota_exhausted")] = summary.quotaExhausted;
  QJsonArray failedItemsArr;
  for (const auto &fi : summary.failedItems) {
    QJsonObject o;
    o[QStringLiteral("collection_index")] = fi.collectionIndex;
    o[QStringLiteral("collection_uuid")] = fi.collectionUuid;
    o[QStringLiteral("path")] = fi.path;
    // Entity failures round-trip their discriminator + target so a resumed run
    // re-queues them AS entity jobs (not bogus game lookups of the systemeid).
    // Only written for entity items — a missing is_entity/entity on read means a
    // game item (backward-compat with legacy snapshots).
    if (fi.isEntity) {
      o[QStringLiteral("is_entity")] = true;
      QJsonObject e;
      e[QStringLiteral("type")] = static_cast<int>(fi.entity.type);
      e[QStringLiteral("identity")] = fi.entity.identity;
      e[QStringLiteral("collection_index")] = fi.entity.collectionIndex;
      o[QStringLiteral("entity")] = e;
    }
    failedItemsArr.append(o);
  }
  sumObj[QStringLiteral("failed_items")] = failedItemsArr;
  root[QStringLiteral("summary_so_far")] = sumObj;
  QJsonArray queueArr;
  for (const auto &j : snap.queue) {
    // Entity jobs carry no item list but ARE remaining work.
    if (j.items.isEmpty() && !j.isEntityJob()) continue;
    QJsonObject jObj;
    jObj[QStringLiteral("collection_index")] = j.collectionIndex;
    jObj[QStringLiteral("collection_uuid")] = j.collectionUuid;
    jObj[QStringLiteral("collection_name")] = j.collectionName;
    jObj[QStringLiteral("artwork_dir")] = j.artworkDir;
    QJsonArray itemsArr;
    for (const auto &p : j.items) itemsArr.append(p);
    jObj[QStringLiteral("remaining")] = itemsArr;
    if (j.isEntityJob()) {
      QJsonObject e;
      e[QStringLiteral("type")] = static_cast<int>(j.entity.type);
      e[QStringLiteral("identity")] = j.entity.identity;
      e[QStringLiteral("collection_index")] = j.entity.collectionIndex;
      jObj[QStringLiteral("entity")] = e;
    }
    queueArr.append(jObj);
  }
  root[QStringLiteral("queue")] = queueArr;
  // Compact rather than Indented: smaller file, faster to format, and the
  // file is machine-read only — no human ever edits this.
  return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

ScraperService::PendingState deserialize(const QByteArray &bytes) {
  ScraperService::PendingState out;
  QJsonParseError err{};
  const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    qCWarning(lcScrapePendingState) << "Bad pending-scrape.json:" << err.errorString();
    return out;
  }
  const QJsonObject root = doc.object();
  const int version = root.value(QStringLiteral("version")).toInt(0);
  // Accept the current and older-but-supported versions; a v1 (pre-entity)
  // file loads fine since it simply has no entity descriptors.
  if (version < kMinSupportedStateVersion || version > kCurrentStateVersion) {
    qCWarning(lcScrapePendingState) << "Unsupported pending-scrape.json version" << version;
    return out;
  }
  out.startedAtUnixMs =
      static_cast<qint64>(root.value(QStringLiteral("started_at_unix_ms")).toDouble(0));
  out.mode = root.value(QStringLiteral("mode")).toString() == QLatin1String("interactive")
                 ? ScraperService::Mode::Interactive
                 : ScraperService::Mode::Auto;
  out.writeMetadata = root.value(QStringLiteral("write_metadata")).toBool(true);
  const auto filterArr = root.value(QStringLiteral("media_filter")).toArray();
  for (const auto &v : filterArr) out.mediaFilter.insert(v.toString());
  const auto sumObj = root.value(QStringLiteral("summary_so_far")).toObject();
  out.summarySoFar.scraped = sumObj.value(QStringLiteral("scraped")).toInt(0);
  out.summarySoFar.skipped = sumObj.value(QStringLiteral("skipped")).toInt(0);
  out.summarySoFar.errors = sumObj.value(QStringLiteral("errors")).toInt(0);
  out.summarySoFar.notFound = sumObj.value(QStringLiteral("not_found")).toInt(0);
  out.summarySoFar.mediaWritten = sumObj.value(QStringLiteral("media_written")).toInt(0);
  // Default 0 on legacy snapshots that never wrote them (Kartend-jjyst.16).
  out.summarySoFar.mediaFetchFailures =
      sumObj.value(QStringLiteral("media_fetch_failures")).toInt(0);
  out.summarySoFar.mediaWriteFailures =
      sumObj.value(QStringLiteral("media_write_failures")).toInt(0);
  const auto failArr = sumObj.value(QStringLiteral("first_failures")).toArray();
  for (const auto &v : failArr) out.summarySoFar.firstFailures.append(v.toString());
  // Restore the re-scrape-failed state. All three default to their zero
  // values on legacy snapshots that never wrote them.
  out.summarySoFar.sidecarFailures = sumObj.value(QStringLiteral("sidecar_failures")).toInt(0);
  out.summarySoFar.quotaExhausted = sumObj.value(QStringLiteral("quota_exhausted")).toBool(false);
  const auto failedItemsArr = sumObj.value(QStringLiteral("failed_items")).toArray();
  for (const auto &v : failedItemsArr) {
    const auto o = v.toObject();
    ScraperService::Summary::FailedItem fi;
    fi.collectionIndex = o.value(QStringLiteral("collection_index")).toInt(-1);
    fi.collectionUuid = o.value(QStringLiteral("collection_uuid")).toString();
    fi.path = o.value(QStringLiteral("path")).toString();
    // A missing is_entity/entity means a game item (legacy + game-item compat).
    fi.isEntity = o.value(QStringLiteral("is_entity")).toBool(false);
    if (o.contains(QStringLiteral("entity"))) {
      const auto e = o.value(QStringLiteral("entity")).toObject();
      const int rawType =
          e.value(QStringLiteral("type")).toInt(static_cast<int>(Scraper::ScrapeEntityType::Game));
      fi.entity.type = static_cast<Scraper::ScrapeEntityType>(rawType);
      fi.entity.identity = e.value(QStringLiteral("identity")).toString();
      fi.entity.collectionIndex = e.value(QStringLiteral("collection_index")).toInt(-1);
    }
    out.summarySoFar.failedItems.append(fi);
  }
  const auto queueArr = root.value(QStringLiteral("queue")).toArray();
  for (const auto &v : queueArr) {
    const auto jo = v.toObject();
    ScraperService::CollectionJob job;
    job.collectionIndex = jo.value(QStringLiteral("collection_index")).toInt(-1);
    job.collectionUuid = jo.value(QStringLiteral("collection_uuid")).toString();
    job.collectionName = jo.value(QStringLiteral("collection_name")).toString();
    job.artworkDir = jo.value(QStringLiteral("artwork_dir")).toString();
    const auto items = jo.value(QStringLiteral("remaining")).toArray();
    for (const auto &p : items) job.items.append(p.toString());
    // Restore an entity descriptor when present so an interrupted entity
    // scrape resumes like any other queued job.
    if (jo.contains(QStringLiteral("entity"))) {
      const auto e = jo.value(QStringLiteral("entity")).toObject();
      const int rawType =
          e.value(QStringLiteral("type")).toInt(static_cast<int>(Scraper::ScrapeEntityType::Game));
      job.entity.type = static_cast<Scraper::ScrapeEntityType>(rawType);
      job.entity.identity = e.value(QStringLiteral("identity")).toString();
      job.entity.collectionIndex = e.value(QStringLiteral("collection_index")).toInt(-1);
    }
    if (!job.items.isEmpty() || job.isEntityJob()) out.queue.append(job);
  }
  out.hasState = !out.queue.isEmpty();
  return out;
}

bool atomicWrite(const QString &path, const QByteArray &bytes) {
  // stopForQuotaExhaustion flushes this file precisely because the user may
  // quit right after — the shared helper flushes the directory entry too so
  // the rename survives a crash/power-cut (QSaveFile + syncDirectory, the
  // sanctioned pattern).
  if (!PathUtils::atomicWriteFile(path, bytes)) {
    qCWarning(lcScrapePendingState) << "Failed to write pending state to" << path;
    return false;
  }
  return true;
}

} // namespace ScrapePendingState
} // namespace Scraper
