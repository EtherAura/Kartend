#include "collectionplatformbackgrounds.h"

#include <QDir>

#include "collection/collectionconfig.h"
#include "pathutils.h"

namespace CollectionPlatformBackgrounds {

QString sharedRoot(const CollectionConfig &cfg) {
  // Same expansion the scrape launchers apply before building entity jobs
  // (ScraperController / ScrapeResultDialogUnified → buildEntityJobs).
  const QString artworkDir = PathUtils::validateAndExpandPath(cfg.artworkDirectory, cfg.name);
  if (artworkDir.isEmpty()) return {};
  return QDir(artworkDir).filePath(QStringLiteral("_shared"));
}

int systemId(const CollectionConfig &cfg, const QList<ScreenScraperSystems::System> &systems) {
  // buildEntityJobs never queues a platform job for a playlist: it spans
  // whatever its rules match and has no single platform.
  if (cfg.isPlaylist) return 0;
  const int id = ScreenScraperSystems::resolveForCollection(
      cfg.scraperOverrides.screenscraperSystemId, cfg.name, cfg.type, cfg.extensions, systems);
  // fetchEntity treats a non-positive id as "could not determine the system"
  // and writes nothing, so there is nothing of its to list either.
  return id > 0 ? id : 0;
}

QList<PlatformBackgroundCandidates::Candidate>
forCollection(const CollectionConfig &cfg, const QList<ScreenScraperSystems::System> &systems) {
  const int id = systemId(cfg, systems);
  if (id <= 0) return {};
  const QString root = sharedRoot(cfg);
  if (root.isEmpty()) return {};
  return PlatformBackgroundCandidates::enumerate(root, QString::number(id));
}

} // namespace CollectionPlatformBackgrounds
