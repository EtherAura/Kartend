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

QList<SuspiciousKartPath> collectInTreeLauncherPaths(const CollectionConfig &cfg,
                                                     const QString &extractedRoot) {
  QList<SuspiciousKartPath> out;
  if (extractedRoot.isEmpty()) return out;
  // Canonicalize the extraction root so symlinks/.. in either side compare
  // on equal footing. Fall back to the cleaned absolute path when the dir
  // doesn't exist yet (it always should post-extract, but be defensive).
  const QString rootAbs = QFileInfo(extractedRoot).canonicalFilePath().isEmpty()
                              ? QDir::cleanPath(QFileInfo(extractedRoot).absoluteFilePath())
                              : QFileInfo(extractedRoot).canonicalFilePath();
  const QString rootPrefix = rootAbs + QLatin1Char('/');
  auto isInside = [&](const QString &path) {
    if (path.isEmpty()) return false;
    const QFileInfo fi(path);
    // Resolve through symlinks when the target exists (a bundled script does);
    // otherwise clean the absolute path so "<root>/../etc/x" can't masquerade
    // as in-tree and, conversely, "<root>/bin/x" is still detected pre-launch.
    const QString canon = fi.canonicalFilePath();
    const QString abs = canon.isEmpty() ? QDir::cleanPath(fi.absoluteFilePath()) : canon;
    return abs == rootAbs || abs.startsWith(rootPrefix);
  };
  auto check = [&](const QString &field, const QString &path) {
    if (isInside(path)) out.append({field, path});
  };
  check(QStringLiteral("launcher.launcherPath"), cfg.launcher.launcherPath);
  for (int i = 0; i < cfg.launcher.additionalLaunchers.size(); ++i) {
    check(QStringLiteral("additionalLaunchers[%1].launcherPath").arg(i),
          cfg.launcher.additionalLaunchers[i].launcherPath);
  }
  return out;
}

} // namespace kart
