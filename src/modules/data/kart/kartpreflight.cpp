#include "kartpreflight.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QStandardPaths>

#include "kartlaunchertrust.h"

namespace KartPreflight {

LauncherCheck classifyLauncherPath(const QString &path) {
  const QString trimmed = path.trimmed();
  if (trimmed.isEmpty()) return LauncherCheck::EmptyPath;

  const QFileInfo info(trimmed);
  if (info.isAbsolute()) {
    if (!info.exists()) return LauncherCheck::Missing;
    if (!info.isExecutable()) return LauncherCheck::NotExecutable;
    return LauncherCheck::Ok;
  }

  // Relative path — try PATH-resolution first, then a literal lookup against
  // the working directory. We deliberately do not fall back to the kart's
  // own bundle directory because the import target may move; the user can
  // re-pick the launcher post-import if it lived next to the bundle.
  const QString resolved = QStandardPaths::findExecutable(trimmed);
  if (!resolved.isEmpty()) return LauncherCheck::Ok;
  if (info.exists()) return info.isExecutable() ? LauncherCheck::Ok : LauncherCheck::NotExecutable;
  return LauncherCheck::Missing;
}

QString launcherCheckLabel(LauncherCheck check) {
  switch (check) {
  case LauncherCheck::Ok:
    return QCoreApplication::translate("KartPreflight", "OK");
  case LauncherCheck::Missing:
    return QCoreApplication::translate("KartPreflight", "Missing");
  case LauncherCheck::NotExecutable:
    return QCoreApplication::translate("KartPreflight", "Not executable");
  case LauncherCheck::EmptyPath:
    return QCoreApplication::translate("KartPreflight", "Empty path");
  }
  return QString{};
}

PreflightReport buildReport(const KartManifest::Manifest &manifest,
                            const QSet<QString> &trustedLauncherPaths,
                            const QSet<QString> &existingCollectionNames) {
  PreflightReport out;

  const CollectionConfig &cfg = manifest.collectionConfig;
  out.collectionName = manifest.name.isEmpty() ? cfg.name : manifest.name;
  out.collectionType = cfg.type;
  out.itemCount = manifest.items.size();
  out.launcherCount = cfg.launcher.launcherCount();

  // Detect artwork overrides / metadata presence by walking the items list.
  for (const KartManifest::Item &item : manifest.items) {
    if (!item.artworkPath.trimmed().isEmpty()) out.hasArtworkOverrides = true;
    const auto &md = item.metadata;
    if (!md.title.isEmpty() || !md.description.isEmpty() || md.rating > 0 ||
        !md.releaseDate.isEmpty() || !md.developer.isEmpty() || !md.publisher.isEmpty()) {
      out.hasMetadata = true;
    }
    if (out.hasArtworkOverrides && out.hasMetadata) break;
  }

  // Launcher path checks. Primary + additional are reported with friendly
  // labels; per-item launcher overrides are skipped because they're indices
  // into the same launcher list (already checked).
  auto pushIssue = [&out](const QString &label, const QString &path, LauncherCheck reason) {
    LauncherIssue issue;
    issue.label = label;
    issue.path = path;
    issue.reason = reason;
    out.launcherIssues.append(issue);
  };

  const LauncherProfile &lp = cfg.launcher;
  // EmptyPath isn't an issue — the user may be setting up a stub collection
  // that gets a launcher post-import. Only Missing / NotExecutable are
  // surfaced because those will fail at first launch.
  auto shouldReport = [](LauncherCheck c) {
    return c == LauncherCheck::Missing || c == LauncherCheck::NotExecutable;
  };
  const LauncherCheck primary = classifyLauncherPath(lp.launcherPath);
  if (shouldReport(primary)) {
    QString label = QCoreApplication::translate("KartPreflight", "Primary launcher");
    if (!lp.launcherName.trimmed().isEmpty()) {
      label += QStringLiteral(" (") + lp.launcherName.trimmed() + QStringLiteral(")");
    }
    pushIssue(label, lp.launcherPath, primary);
  }
  for (int i = 0; i < lp.additionalLaunchers.size(); ++i) {
    const LauncherConfig &alt = lp.additionalLaunchers.at(i);
    const LauncherCheck check = classifyLauncherPath(alt.launcherPath);
    if (!shouldReport(check)) continue;
    QString label =
        QCoreApplication::translate("KartPreflight", "Additional launcher %1").arg(i + 1);
    if (!alt.name.trimmed().isEmpty()) {
      label += QStringLiteral(" (") + alt.name.trimmed() + QStringLiteral(")");
    }
    pushIssue(label, alt.launcherPath, check);
  }

  // Kartend-kxqqf: the launcher block an imported bundle carries decides what
  // gets executed on the first Launch click, so every field of it is reported
  // — not just the ones an allowlist happens to dislike. The extraction root
  // isn't known yet at preflight time (nothing has been written to disk),
  // hence the empty third argument: the bundled-payload escalation is applied
  // again post-extract by the import gate.
  out.launcherTrust = kart::collectLauncherTrustFindings(cfg, trustedLauncherPaths, QString());

  // Icon / placeholder paths keep the plain allowlist treatment; the launcher
  // fields they used to share this list with now come through launcherTrust.
  out.suspiciousPaths = kart::collectSuspiciousAssetPaths(cfg);

  // Name-conflict probe — case-insensitive compare against the supplied set
  // of lowercase names.
  if (!out.collectionName.trimmed().isEmpty() &&
      existingCollectionNames.contains(out.collectionName.trimmed().toLower())) {
    out.nameConflicts = true;
  }

  return out;
}

} // namespace KartPreflight
