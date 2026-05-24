#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>

#include "kartmanager.h"

namespace kart {

QList<SuspiciousKartPath> collectSuspiciousKartPaths(const CollectionConfig &cfg,
                                                     const QSet<QString> &trustedLauncherPaths) {
  QList<SuspiciousKartPath> out;
  const QString home = QDir::homePath();
  const QStringList allowedRoots = {home, QStringLiteral("/usr/bin"),
                                    QStringLiteral("/usr/local/bin"), QStringLiteral("/opt")};
  auto isPathAllowed = [&](const QString &path) {
    const QString abs = QFileInfo(path).absoluteFilePath();
    for (const QString &root : allowedRoots) {
      if (abs.startsWith(root + QLatin1Char('/')) || abs == root) {
        return true;
      }
    }
    return false;
  };
  auto check = [&](const QString &field, const QString &path, bool launcherField) {
    if (path.isEmpty()) return;
    if (isPathAllowed(path)) return;
    if (launcherField && trustedLauncherPaths.contains(path)) {
      return;
    }
    out.append({field, path});
  };
  check(QStringLiteral("launcher.launcherPath"), cfg.launcher.launcherPath, /*launcherField=*/true);
  for (int i = 0; i < cfg.launcher.additionalLaunchers.size(); ++i) {
    check(QStringLiteral("additionalLaunchers[%1].launcherPath").arg(i),
          cfg.launcher.additionalLaunchers[i].launcherPath, /*launcherField=*/true);
  }
  check(QStringLiteral("collectionIcon"), cfg.collectionIcon, /*launcherField=*/false);
  check(QStringLiteral("placeholderArtwork"), cfg.placeholderArtwork, /*launcherField=*/false);
  return out;
}

} // namespace kart
