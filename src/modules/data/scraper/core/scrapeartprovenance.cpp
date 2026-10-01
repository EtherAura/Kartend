#include "scrapeartprovenance.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

#include "pathutils.h"
#include "scrapeassetdedup.h"

namespace {
Q_LOGGING_CATEGORY(lcArtProvenance, "kartend.scrape.provenance")

constexpr int kRecordVersion = 1;
constexpr const char *kFileName = "provenance.json";

QString sharedDir(const QString &artworkDir) {
  return QDir(artworkDir).filePath(QStringLiteral("_shared"));
}
} // namespace

namespace Scraper::ArtProvenance {

QString provenanceFilePath(const QString &artworkDir) {
  return QDir(sharedDir(artworkDir)).filePath(QLatin1String(kFileName));
}

QString sharedRelativePath(const QString &artworkDir, const QString &absolutePath) {
  if (artworkDir.isEmpty() || absolutePath.isEmpty()) return {};
  const QDir shared(sharedDir(artworkDir));
  const QString rel = shared.relativeFilePath(QDir::cleanPath(absolutePath));
  // relativeFilePath climbs out with ".." when the file is elsewhere; an
  // absolute result means the two are on different roots (Windows drives).
  if (rel.isEmpty() || rel == QLatin1String(".") || rel.startsWith(QLatin1String("..")) ||
      QDir::isAbsolutePath(rel)) {
    return {};
  }
  return rel;
}

QHash<QString, QString> loadRecordedHashes(const QString &artworkDir) {
  QHash<QString, QString> out;
  if (artworkDir.isEmpty()) return out;
  QFile f(provenanceFilePath(artworkDir));
  if (!f.exists() || !f.open(QIODevice::ReadOnly)) return out;
  QJsonParseError err{};
  const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    qCWarning(lcArtProvenance) << "ignoring unreadable provenance record" << f.fileName()
                               << err.errorString();
    return out;
  }
  const QJsonObject assets = doc.object().value(QStringLiteral("assets")).toObject();
  for (auto it = assets.constBegin(); it != assets.constEnd(); ++it) {
    const QString md5 = it.value().toObject().value(QStringLiteral("md5")).toString().toLower();
    if (!it.key().isEmpty() && !md5.isEmpty()) out.insert(it.key(), md5);
  }
  return out;
}

bool recordHashes(const QString &artworkDir, const QHash<QString, QString> &hashes) {
  if (hashes.isEmpty()) return true;
  if (artworkDir.isEmpty()) return false;
  // Read-merge-write: the record covers every asset under _shared/, and one
  // job only knows about its own.
  QHash<QString, QString> merged = loadRecordedHashes(artworkDir);
  for (auto it = hashes.constBegin(); it != hashes.constEnd(); ++it) {
    if (!it.key().isEmpty() && !it.value().isEmpty()) merged.insert(it.key(), it.value().toLower());
  }
  QJsonObject assets;
  for (auto it = merged.constBegin(); it != merged.constEnd(); ++it) {
    assets.insert(it.key(), QJsonObject{{QStringLiteral("md5"), it.value()}});
  }
  const QJsonObject root{{QStringLiteral("version"), kRecordVersion},
                         {QStringLiteral("assets"), assets}};
  if (!QDir().mkpath(sharedDir(artworkDir))) return false;
  const bool ok = PathUtils::atomicWriteFile(provenanceFilePath(artworkDir),
                                             QJsonDocument(root).toJson(QJsonDocument::Indented));
  if (!ok) {
    qCWarning(lcArtProvenance) << "could not write provenance record under" << artworkDir;
  }
  return ok;
}

bool recordWrittenFiles(const QString &artworkDir, const QStringList &absolutePaths) {
  QHash<QString, QString> hashes;
  for (const QString &path : absolutePaths) {
    const QString rel = sharedRelativePath(artworkDir, path);
    if (rel.isEmpty()) continue;
    const QString md5 = ScrapeAssetDedup::hashLocalFile(path).md5Hex;
    if (!md5.isEmpty()) hashes.insert(rel, md5);
  }
  return recordHashes(artworkDir, hashes);
}

FetchPlan planFetches(const QList<MediaAsset> &assets, const QString &artworkDir) {
  FetchPlan plan;
  if (artworkDir.isEmpty()) {
    plan.fetch = assets;
    return plan;
  }
  const QHash<QString, QString> recorded = loadRecordedHashes(artworkDir);
  for (const MediaAsset &asset : assets) {
    const QString catalogMd5 = asset.catalogMd5.toLower();
    if (catalogMd5.isEmpty()) {
      plan.fetch.append(asset); // nothing to compare against
      continue;
    }
    const QString onDisk = ScrapeAssetDedup::findExistingSharedAsset(asset, {artworkDir});
    if (onDisk.isEmpty()) {
      plan.fetch.append(asset); // not there yet
      continue;
    }
    const QString diskMd5 = ScrapeAssetDedup::hashLocalFile(onDisk).md5Hex;
    if (diskMd5.isEmpty()) {
      plan.fetch.append(asset); // unreadable: let the write path deal with it
      continue;
    }
    const QString rel = sharedRelativePath(artworkDir, onDisk);
    if (diskMd5 == catalogMd5) {
      plan.upToDatePaths.append(onDisk);
      if (!rel.isEmpty()) plan.confirmed.insert(rel, diskMd5);
      continue;
    }
    if (!rel.isEmpty() && recorded.value(rel) == diskMd5) {
      plan.fetch.append(asset); // ours, and the catalogue has moved on
      continue;
    }
    plan.keptLocalPaths.append(onDisk); // the user's own art — never clobber it
  }
  return plan;
}

} // namespace Scraper::ArtProvenance
