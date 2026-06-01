// Batch warmer that walks a collection's media tree and writes procedural
// placeholder PNGs for items missing artwork. The render path delegates to
// ItemWidget::buildPlaceholderTile so warmed pixmaps look identical to the
// live grid's lazy fallback — no second rendering pipeline to keep in sync.
#include "placeholderwarmer.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTemporaryFile>

#include "artworkutils.h"
#include "collection/collectionconfig.h"
#include "pathutils.h"

namespace PlaceholderWarmer {

namespace {

constexpr int MAX_REPORTED_FAILURES = 10;

QString tr(const char *src) {
  return QCoreApplication::translate("PlaceholderWarmer", src);
}

/// Returns the artwork directory the warmer should target. The Artwork-tab
/// panel passes the live line-edit value so unsaved edits still drive the
/// warm; an empty override means "use the saved CollectionConfig field".
QString resolveArtworkDir(const CollectionConfig &collection, const QString &override_) {
  return override_.isEmpty() ? collection.artworkDirectory : override_;
}

/// Lowercased, dot-stripped extension set the iterator filter compares
/// against. Collection extensions are stored as ".ext" sometimes and "ext"
/// other times depending on how the user typed them; normalize both shapes
/// so the warmer matches whatever the live scan would.
QSet<QString> normalizedExtensionSet(const QStringList &raw) {
  QSet<QString> out;
  out.reserve(raw.size());
  for (const QString &ext : raw) {
    QString trimmed = ext.trimmed().toLower();
    if (trimmed.startsWith('.')) {
      trimmed.remove(0, 1);
    }
    if (!trimmed.isEmpty()) {
      out.insert(trimmed);
    }
  }
  return out;
}

/// Probe artwork directory writability by creating + deleting a unique
/// temp file. Cheaper and more reliable than QFileInfo::isWritable() which
/// returns true for read-only mount points on some platforms.
bool isDirectoryWritable(const QString &dirPath) {
  QTemporaryFile probe(dirPath + QStringLiteral("/.kartend-warmer-probe-XXXXXX"));
  probe.setAutoRemove(true);
  return probe.open();
}

} // namespace

PreflightResult preflight(const CollectionConfig &collection,
                          const QString &artworkDirectoryOverride) {
  PreflightResult out;

  if (collection.mediaDirectory.isEmpty()) {
    out.error = PreflightError::EmptyMediaDirectory;
    out.humanMessage = tr("This collection has no media folder configured. Set "
                          "Configuration → Media Directory first.");
    return out;
  }
  const QString artworkPath = resolveArtworkDir(collection, artworkDirectoryOverride);
  if (artworkPath.isEmpty()) {
    out.error = PreflightError::EmptyArtworkDirectory;
    out.humanMessage = tr("This collection has no artwork folder configured. Set "
                          "Artwork → Artwork (or pick a folder via Browse) first.");
    return out;
  }

  out.resolvedMediaDirectory =
      PathUtils::validateAndExpandPath(collection.mediaDirectory, collection.name);
  if (out.resolvedMediaDirectory.isEmpty() || !QFileInfo(out.resolvedMediaDirectory).isDir()) {
    out.error = PreflightError::MediaDirectoryMissing;
    out.humanMessage =
        tr("Media folder does not exist or is not a directory:\n%1").arg(collection.mediaDirectory);
    return out;
  }

  // The artwork dir may not exist yet (fresh collection, configured but
  // never populated) — use the expansion variant that doesn't reject
  // not-yet-created paths so we can mkpath() it on demand below.
  out.resolvedArtworkDirectory =
      PathUtils::expandPathWithoutExistenceCheck(artworkPath, collection.name);
  if (out.resolvedArtworkDirectory.isEmpty() || !QDir(out.resolvedArtworkDirectory).isAbsolute()) {
    out.error = PreflightError::ArtworkDirectoryNotWritable;
    out.humanMessage = tr("Artwork folder is not a valid absolute path:\n%1").arg(artworkPath);
    return out;
  }
  if (!QDir().mkpath(out.resolvedArtworkDirectory)) {
    out.error = PreflightError::ArtworkDirectoryNotWritable;
    out.humanMessage = tr("Could not create artwork folder:\n%1").arg(out.resolvedArtworkDirectory);
    return out;
  }
  if (!isDirectoryWritable(out.resolvedArtworkDirectory)) {
    out.error = PreflightError::ArtworkDirectoryNotWritable;
    out.humanMessage = tr("Artwork folder is not writable:\n%1").arg(out.resolvedArtworkDirectory);
    return out;
  }

  if (collection.extensions.isEmpty()) {
    out.error = PreflightError::NoExtensionsConfigured;
    out.humanMessage = tr("This collection has no media file extensions configured. Set "
                          "Configuration → Allowed Extensions first.");
    return out;
  }

  return out;
}

Result exportMissingPlaceholders(const CollectionConfig &collection,
                                 const QString &artworkDirectoryOverride, int tileWidth,
                                 int tileHeight, int cornerRadius, const TileFactory &tileFactory,
                                 const ProgressFn &onProgress, const CancelFn &isCancelled) {
  Result result;

  const auto pre = preflight(collection, artworkDirectoryOverride);
  if (pre.error != PreflightError::None) {
    // Preflight is the gate; entering here without it is a caller bug.
    // Bail with a single failure entry so the UI summary still says
    // something rather than printing zeros across the board.
    result.itemsFailed = 1;
    result.firstFailures.append(pre.humanMessage);
    return result;
  }

  const QSet<QString> wantedExts = normalizedExtensionSet(collection.extensions);
  const QString artworkDir = pre.resolvedArtworkDirectory;

  QDirIterator it(pre.resolvedMediaDirectory, QDir::Files | QDir::NoDotAndDotDot,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    // Kartend-qe9a: bail early when the caller (worker run behind a progress
    // dialog) requests cancellation; the counts so far stand.
    if (isCancelled && isCancelled()) {
      result.cancelled = true;
      break;
    }
    const QString fullPath = it.next();
    const QFileInfo fi(fullPath);
    const QString ext = fi.suffix().toLower();
    if (!wantedExts.contains(ext)) {
      continue;
    }
    ++result.itemsScanned;
    // Report progress periodically (not per item) so a worker run can drive a
    // progress UI without flooding it with cross-thread updates.
    if (onProgress && (result.itemsScanned % 16) == 0) {
      onProgress(result.itemsScanned, result.itemsExported);
    }

    // Skip if any image with the matching base/full name already exists in
    // the artwork dir — the resolver would pick that up before our PNG, so
    // overwriting here would be either a no-op (same path) or destructive
    // (clobbering a real cover).
    const QString existing = ArtworkUtils::findArtworkForFile(fi.fileName(), artworkDir);
    if (!existing.isEmpty()) {
      ++result.itemsAlreadyHadArtwork;
      continue;
    }

    // Use the live render path so warmed PNGs are pixel-identical to the
    // grid's lazy fallback; no second source of truth for placeholder look.
    const QImage tile = tileFactory ? tileFactory(tileWidth, tileHeight, cornerRadius) : QImage();
    if (tile.isNull()) {
      ++result.itemsFailed;
      if (result.firstFailures.size() < MAX_REPORTED_FAILURES) {
        result.firstFailures.append(tr("%1: placeholder render returned null image").arg(fullPath));
      }
      continue;
    }

    // Always .png — matches the resolver's first lookup attempt so the
    // warmed file is hit on the next render without rescanning extensions.
    const QString outPath = artworkDir + QLatin1Char('/') + fi.completeBaseName() + ".png";
    if (!tile.save(outPath, "PNG")) {
      ++result.itemsFailed;
      if (result.firstFailures.size() < MAX_REPORTED_FAILURES) {
        result.firstFailures.append(tr("%1: failed to write %2").arg(fullPath, outPath));
      }
      continue;
    }
    ++result.itemsExported;
  }

  if (onProgress) {
    onProgress(result.itemsScanned, result.itemsExported);
  }
  return result;
}

} // namespace PlaceholderWarmer
