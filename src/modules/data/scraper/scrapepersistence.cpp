// On-Apply persistence for ScrapeResultDialog. Pure file-IO + DB-call
// orchestration so it stays testable with a fake IDatabaseManager and
// a temp-dir artworkDirectory.
#include "scrapepersistence.h"

#include <optional>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QString>
#include <QStringList>

#include <QUrl>

#include <QDebug>
#include <QLoggingCategory>

#include "idatabasemanager.h"
#include "itemartwork.h"
#include "itemmetadata.h"

#include <QSqlDatabase>

// Default to warning — the qCDebug lines below fire once per item
// during a scrape (50K-item batches produce 50K+ stderr writes, each
// a synchronous main-thread flush, which freezes the UI). Opt in via
// `KARTEND_LOG_RULES=kartend.scrape.debug=true` when diagnosing.
Q_LOGGING_CATEGORY(lcScrape, "kartend.scrape", QtWarningMsg)

namespace Scraper {

namespace {

constexpr int MAX_REPORTED_FAILURES = 8;

bool writeBytesAtomically(const QString &filePath, const QByteArray &bytes) {
  // Caller already mkpath'd the parent dir; this just streams the
  // bytes. QFile is fine here — these payloads are small images, and
  // a partial write of a JPG just shows up as a corrupt thumbnail
  // (which the user can fix by re-running the scrape).
  QFile f(filePath);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  if (f.write(bytes) != bytes.size()) {
    f.close();
    return false;
  }
  f.close();
  return true;
}

bool isStandardArtworkType(const QString &type) {
  return ItemArtworkStore::isStandardType(type);
}

/// Coarse classification of a scraped media asset. Drives which
/// collection directory the file lands in (artwork vs video vs
/// manual) and which file extension we default to when the URL
/// doesn't carry one.
enum class MediaKind { Image, Video, Manual };

MediaKind kindForType(const QString &type) {
  const QString lower = type.trimmed().toLower();
  if (lower == QLatin1String("video")) return MediaKind::Video;
  if (lower == QLatin1String("manual")) return MediaKind::Manual;
  // Everything else (front / box / screenshot / fanart / marquee /
  // logo / mixrbv1-2 / custom user types) is treated as an image.
  // ScreenScraper's "trailer" is *technically* a video but ships
  // rarely and falls through here for now; can be added once a
  // provider start surfacing it.
  return MediaKind::Image;
}

/// Picks the on-disk extension for a written asset. Honours the URL
/// suffix when present so an MP4 stays MP4 and a WebM stays WebM;
/// falls back to a sensible per-kind default for asset URLs that
/// don't carry a recognisable extension.
QString extensionForAsset(const QUrl &url, MediaKind kind) {
  // QUrl::path() preserves the URL-decoded path including any
  // querystring-free trailing segment — exactly the part QFileInfo
  // can suffix-extract from. Filtered through a short whitelist so a
  // pathological `.../foo.php` URL doesn't end up as a ".php" file.
  const QString rawExt = QFileInfo(url.path()).suffix().toLower();
  static const QSet<QString> kImageExts{"png", "jpg", "jpeg", "webp", "gif", "bmp"};
  static const QSet<QString> kVideoExts{"mp4", "webm", "mkv", "mov", "avi"};
  static const QSet<QString> kDocExts{"pdf", "epub", "cbz", "cbr", "djvu"};
  switch (kind) {
  case MediaKind::Image:
    return kImageExts.contains(rawExt) ? rawExt : QStringLiteral("png");
  case MediaKind::Video:
    return kVideoExts.contains(rawExt) ? rawExt : QStringLiteral("mp4");
  case MediaKind::Manual:
    return kDocExts.contains(rawExt) ? rawExt : QStringLiteral("pdf");
  }
  Q_UNREACHABLE();
}

QString mergeCustomFields(const QString &existingJson, const QHash<QString, QString> &additions) {
  // Parse existing customFields JSON (may be empty), merge additions
  // (additions win), serialise back. Keeps user-entered keys that the
  // scrape didn't touch.
  QJsonObject merged;
  if (!existingJson.trimmed().isEmpty()) {
    const auto doc = QJsonDocument::fromJson(existingJson.toUtf8());
    if (doc.isObject()) {
      merged = doc.object();
    }
  }
  for (auto it = additions.constBegin(); it != additions.constEnd(); ++it) {
    if (it.key().trimmed().isEmpty()) continue;
    merged.insert(it.key(), it.value());
  }
  if (merged.isEmpty()) {
    return {};
  }
  return QString::fromUtf8(QJsonDocument(merged).toJson(QJsonDocument::Compact));
}

/// Priority order for picking a fallback primary cover when the
/// scrape didn't return an explicit "front" asset. The persistence
/// layer mirrors the highest-ranked written image into the flat
/// `{artworkDirectory}/{baseName}.<ext>` slot so the grid tile and
/// the details-pane top preview always have something to show.
/// Lower = higher priority. Types not in this table never serve as
/// the primary cover (e.g. logos, custom user types).
int coverFallbackPriority(const QString &type) {
  const QString lower = type.toLower();
  if (lower == QLatin1String("front")) return 0;
  if (lower == QLatin1String("box")) return 1;
  if (lower == QLatin1String("box-3d")) return 2;
  if (lower == QLatin1String("mixrbv1")) return 3;
  if (lower == QLatin1String("mixrbv2")) return 4;
  if (lower == QLatin1String("screenshot")) return 5;
  if (lower == QLatin1String("title")) return 6;
  if (lower == QLatin1String("fanart")) return 7;
  if (lower == QLatin1String("marquee")) return 8;
  return -1; // "not a candidate" sentinel
}

} // namespace

MediaWriteResult writeMediaFiles(const QString &artworkDirectory, const QString &baseName,
                                 const QList<PendingMediaWrite> &media, RescrapeMode rescrapeMode) {
  MediaWriteResult result;
  // Per-asset re-scrape gate. Returns true if the destFile should be
  // written; false if the existing file already satisfies the policy.
  // For UpdateChanged the gate also reports back which of the two
  // (existing/new) the disk should hold — comparing raw bytes catches
  // both content changes AND simple re-encodes (a re-scraped JPEG
  // saved at a different quality will look "changed" — desired).
  // Reason channel for the per-asset gate. nullopt = "write it";
  // non-empty = "skip with this human-readable reason" so the
  // surrounding loop can record it in firstFailures alongside the
  // mediaSkipped counter. Otherwise the caller saw N skipped with no
  // explanation — for the common FillMissing case that's the entire
  // point of the run (don't redo existing files), and the user
  // deserves to see that explicitly rather than a silent counter.
  auto evaluateAsset = [rescrapeMode](const QString &destFile,
                                      const QByteArray &incoming) -> std::optional<QString> {
    if (rescrapeMode == RescrapeMode::Overwrite) return std::nullopt;
    QFileInfo fi(destFile);
    if (!fi.exists()) return std::nullopt;
    if (rescrapeMode == RescrapeMode::FillMissing) {
      return QStringLiteral("FillMissing — kept existing file at %1").arg(destFile);
    }
    // UpdateChanged: read existing bytes, byte-compare against the
    // incoming payload. Heavy files (videos, manuals) read the entire
    // existing copy off disk — that's the documented cost of this
    // mode and surfaces as the "Update changed (slow)" UI hint.
    QFile f(destFile);
    if (!f.open(QIODevice::ReadOnly)) return std::nullopt; // can't read = treat as missing
    const QByteArray existing = f.readAll();
    if (existing == incoming) {
      return QStringLiteral("UpdateChanged — bytes match existing file at %1").arg(destFile);
    }
    return std::nullopt;
  };

  // ── Media writes ─────────────────────────────────────────────────
  // Single-root layout: everything lives under {artworkDirectory}/.
  // The on-disk subdir is derived from the asset's MediaKind:
  //   image → <type>/   (or "box" for the cross-provider "front" tag)
  //   video → video/
  //   manual → manual/
  // Plus a flat-root mirror at {artworkDirectory}/{baseName}.<ext>
  // for the "front" primary cover so the grid auto-discovers it.
  // File extensions are inferred from each asset's source URL with
  // a per-kind whitelist (default png / mp4 / pdf).
  // Track the best-available cover candidate for the flat-root
  // mirror so we can fall back when the scrape didn't return an
  // explicit "front" (e.g. an SS game with only box-3D / mixrbv* /
  // screenshot art, or a provider that uses a different cover tag).
  // Lower coverFallbackPriority wins; -1 means "not a candidate".
  // Tracks the on-disk path of the best-priority cover so the
  // post-loop mirror can re-read its bytes — works for both
  // newly-written assets AND ones that evaluateAsset skipped because
  // the file already existed (FillMissing / UpdateChanged matched
  // bytes). The skipped case is critical: without it a skipped
  // `front` lets a freshly-written `box-3d` (priority 2) claim the
  // mirror and swap the grid thumbnail.
  int bestMirrorPriority = -1;
  QString bestMirrorSourcePath;
  QString bestMirrorExt;

  if (!artworkDirectory.isEmpty() && !baseName.isEmpty()) {
    const QDir artRoot(artworkDirectory);
    for (const auto &write : media) {
      if (write.bytes.isEmpty() || write.asset.type.isEmpty()) {
        ++result.mediaSkipped;
        if (result.firstFailures.size() < MAX_REPORTED_FAILURES) {
          result.firstFailures.append(
              QStringLiteral("%1: empty bytes or type").arg(write.asset.type));
        }
        continue;
      }
      const MediaKind kind = kindForType(write.asset.type);
      const QString ext = extensionForAsset(write.asset.url, kind);

      // Per-kind subdirectory name under artworkDirectory. "front"
      // is the cross-provider primary-cover tag and is a standard
      // gallery type in its own right — files at `{artwork}/front/`
      // surface in the sidebar gallery as "Front Cover"; the
      // primary-cover mirror at the flat root is handled below via
      // the coverFallbackPriority pass.
      QString subdir;
      switch (kind) {
      case MediaKind::Video:
        subdir = QStringLiteral("video");
        break;
      case MediaKind::Manual:
        subdir = QStringLiteral("manual");
        break;
      case MediaKind::Image:
        subdir = write.asset.type;
        break;
      }
      // Group/Company-scoped assets land in `_shared/` and use the
      // scope key (groupid / companyid) as the filename. This stops
      // the same theme-background or publisher-logo from being
      // duplicated once per game that happens to share the group.
      // Per-game assets keep the current layout.
      const bool sharedScope = kind == MediaKind::Image &&
                               write.asset.scope != Scraper::MediaScope::Game &&
                               !write.asset.scopeKey.isEmpty();
      const QString destDir = sharedScope ? artRoot.filePath(QStringLiteral("_shared/") + subdir)
                                          : artRoot.filePath(subdir);
      if (!QDir().mkpath(destDir)) {
        ++result.mediaSkipped;
        if (result.firstFailures.size() < MAX_REPORTED_FAILURES) {
          result.firstFailures.append(
              QStringLiteral("%1: could not create %2").arg(write.asset.type, destDir));
        }
        continue;
      }
      // Shared filename: `<scope>_<scopeKey>.<ext>`. Per-game keeps
      // `{baseName}.{ext}`.
      const QString sharedScopePrefix = write.asset.scope == Scraper::MediaScope::Group
                                            ? QStringLiteral("group_")
                                            : QStringLiteral("company_");
      const QString destFile =
          sharedScope ? QDir(destDir).filePath(sharedScopePrefix + write.asset.scopeKey +
                                               QLatin1Char('.') + ext)
                      : QDir(destDir).filePath(baseName + QLatin1Char('.') + ext);
      if (auto reason = evaluateAsset(destFile, write.bytes); reason.has_value()) {
        // Existing asset satisfies the active re-scrape policy
        // (FillMissing → file present; UpdateChanged → bytes match).
        // Still counts as "handled" so the user gets accurate
        // mediaWritten + mediaSkipped totals. Reason gets recorded
        // in firstFailures so the dialog can surface it to the user
        // instead of an opaque "N skipped".
        ++result.mediaSkipped;
        if (result.firstFailures.size() < MAX_REPORTED_FAILURES) {
          result.firstFailures.append(QStringLiteral("%1: %2").arg(write.asset.type, *reason));
        }
        // Skipped-because-exists still contributes to the mirror
        // ranking: the file *is* on disk and a higher-priority cover
        // must beat any newly-written lower-priority one. Shared-scope
        // assets are excluded — the per-item mirror keys off baseName,
        // not group/company keys.
        if (kind == MediaKind::Image && !sharedScope) {
          const int prio = coverFallbackPriority(write.asset.type);
          if (prio >= 0 && (bestMirrorPriority < 0 || prio < bestMirrorPriority)) {
            bestMirrorPriority = prio;
            bestMirrorSourcePath = destFile;
            bestMirrorExt = ext;
          }
        }
        continue;
      }
      if (!writeBytesAtomically(destFile, write.bytes)) {
        ++result.mediaSkipped;
        if (result.firstFailures.size() < MAX_REPORTED_FAILURES) {
          result.firstFailures.append(
              QStringLiteral("%1: write failed (%2)").arg(write.asset.type, destFile));
        }
        continue;
      }
      ++result.mediaWritten;
      result.writtenPaths.append(destFile);
      // One-line diagnostic for the scrape pipeline so the user can
      // verify what's being written + where by setting
      // `QT_LOGGING_RULES=kartend.scrape.debug=true`. Includes the
      // raw asset type (pre-kindForType) so unexpected provider tags
      // are visible.
      const char *kindLabel = kind == MediaKind::Video    ? "video"
                              : kind == MediaKind::Manual ? "manual"
                                                          : "image";
      qCDebug(lcScrape) << "wrote" << kindLabel << "type=" << write.asset.type
                        << "bytes=" << write.bytes.size() << "->" << destFile;

      if (kind == MediaKind::Image) {
        // Track the best-available cover candidate for the post-loop
        // mirror write. "front" (priority 0) always wins; anything
        // with priority -1 (logos, custom user types) is skipped.
        // Shared-scope images are excluded — see the skip branch above.
        if (!sharedScope) {
          const int prio = coverFallbackPriority(write.asset.type);
          if (prio >= 0 && (bestMirrorPriority < 0 || prio < bestMirrorPriority)) {
            bestMirrorPriority = prio;
            bestMirrorSourcePath = destFile;
            bestMirrorExt = ext;
          }
        }

        // Non-standard typed artwork (e.g. MusicBrainz "back") needs
        // an item_artwork row pointing at the file; otherwise the
        // sidebar gallery's standard-type discovery skips it.
        // Standard types (front / box / screenshot / title /
        // marquee / fanart / logo) are auto-discovered by
        // subdirectory and don't need a row. Collected here; the
        // DB phase (saveScrapedMetadata) persists the rows — this
        // function stays thread-safe and DB-free.
        if (!isStandardArtworkType(write.asset.type)) {
          result.nonStandardArtwork.append({write.asset.type, destFile});
        }
      }
      // Video / Manual: no item_artwork row. The details pane's
      // video preview and manual button discover the file by
      // basename under {artworkDirectory}/video|manual/.
    }

    // Write the flat-root primary-cover mirror once, picking the
    // best-priority image we saw above. "front" always wins; when
    // it isn't present, box → box-3D → mixrbv* → screenshot etc.
    // provide a sensible fallback so the grid tile and details-pane
    // top preview are never blank after a successful scrape. Bytes
    // come from the on-disk source so a skipped-because-exists cover
    // still mirrors correctly without re-downloading.
    if (bestMirrorPriority >= 0 && !bestMirrorSourcePath.isEmpty()) {
      QFile src(bestMirrorSourcePath);
      QByteArray mirrorBytes;
      if (src.open(QIODevice::ReadOnly)) {
        mirrorBytes = src.readAll();
      }
      if (!mirrorBytes.isEmpty()) {
        const QString primary = artRoot.filePath(baseName + QLatin1Char('.') + bestMirrorExt);
        // Same re-scrape gate as per-asset writes above. The flat-root
        // mirror is a derivative of the cover, so it follows the same
        // policy — FillMissing keeps an existing mirror file even if
        // the user re-scrapes with a different cover; Overwrite always
        // refreshes it.
        const auto mirrorReason = evaluateAsset(primary, mirrorBytes);
        if (!mirrorReason.has_value()) {
          writeBytesAtomically(primary, mirrorBytes);
          qCDebug(lcScrape) << "wrote primary-cover mirror prio=" << bestMirrorPriority << "from"
                            << bestMirrorSourcePath << "->" << primary;
        } else {
          qCDebug(lcScrape) << "skipped primary-cover mirror:" << *mirrorReason;
        }
      } else {
        qCDebug(lcScrape) << "primary-cover mirror source unreadable:" << bestMirrorSourcePath;
      }
    } else {
      qCDebug(lcScrape) << "no primary-cover mirror written; mediaCount=" << media.size();
    }
  }

  return result;
}

namespace {

/// Pure merge: existing × scraped → merged. Shared between the
/// IDatabaseManager-backed save path and the worker-thread direct path
/// so both stay byte-for-byte compatible. "Pick non-empty" preserves
/// user-entered values when the scrape returns an empty slot;
/// customFields merge with scrape-wins for shared keys.
ItemMetadataStore::ItemMetadata mergeScrapedIntoExisting(ItemMetadataStore::ItemMetadata existing,
                                                         const QString &collectionUuid,
                                                         const QString &sourcePath,
                                                         const ScrapedItem &scraped) {
  auto pickNonEmpty = [](const QString &scraped, const QString &existing) {
    return scraped.trimmed().isEmpty() ? existing : scraped;
  };
  existing.collectionUuid = collectionUuid;
  existing.path = sourcePath;
  existing.title = pickNonEmpty(scraped.title, existing.title);
  existing.description = pickNonEmpty(scraped.description, existing.description);
  existing.genre = pickNonEmpty(scraped.genre, existing.genre);
  existing.developer = pickNonEmpty(scraped.developer, existing.developer);
  existing.publisher = pickNonEmpty(scraped.publisher, existing.publisher);
  existing.releaseDate = pickNonEmpty(scraped.releaseDate, existing.releaseDate);
  existing.contentRating = pickNonEmpty(scraped.contentRating, existing.contentRating);
  existing.players = pickNonEmpty(scraped.players, existing.players);
  if (scraped.runtimeSeconds >= 0) {
    existing.runtimeSeconds = scraped.runtimeSeconds;
  }
  existing.tags = pickNonEmpty(scraped.tagsJson, existing.tags);
  existing.customFields = mergeCustomFields(existing.customFields, scraped.customFields);
  if (!scraped.sourceProviderId.isEmpty()) {
    existing.source = scraped.sourceProviderId;
  }
  return existing;
}

} // namespace

bool saveScrapedMetadata(IDatabaseManager *databaseManager, const QString &collectionUuid,
                         const QString &sourcePath, const ScrapedItem &scraped,
                         const NonStandardArtworkList &nonStandardArtwork) {
  if (!databaseManager) return false;

  // Save one item_artwork row per non-standard write the file phase
  // recorded. Standard types (front/box/screenshot/...) are auto-
  // discovered by subdirectory and skip this list. Failures inside
  // saveItemArtwork are logged via DatabaseManager's error channel —
  // we don't fail the metadata save on them.
  for (const auto &entry : nonStandardArtwork) {
    ItemArtworkStore::ItemArtwork row;
    row.collectionUuid = collectionUuid;
    row.path = sourcePath;
    row.artworkType = entry.first;
    row.manualPath = entry.second;
    databaseManager->saveItemArtwork(row);
  }

  if (collectionUuid.isEmpty() || sourcePath.isEmpty()) return false;

  auto existing = databaseManager->loadItemMetadata(collectionUuid, sourcePath);
  const auto merged =
      mergeScrapedIntoExisting(std::move(existing), collectionUuid, sourcePath, scraped);
  return databaseManager->saveItemMetadata(merged);
}

bool saveScrapedMetadataDirect(QSqlDatabase &db, const QString &collectionUuid,
                               const QString &sourcePath, const ScrapedItem &scraped,
                               const NonStandardArtworkList &nonStandardArtwork) {
  if (!db.isValid() || !db.isOpen()) return false;

  // Same artwork-row pass as the IDatabaseManager path. Failures are
  // logged via ErrorUtils inside ItemArtworkStore::save and don't fail
  // the overall write — partial artwork rows are better than dropping
  // the whole metadata save for one bad row.
  for (const auto &entry : nonStandardArtwork) {
    ItemArtworkStore::ItemArtwork row;
    row.collectionUuid = collectionUuid;
    row.path = sourcePath;
    row.artworkType = entry.first;
    row.manualPath = entry.second;
    auto res = ItemArtworkStore::save(db, row);
    if (res.isError()) {
      ErrorUtils::logError(res.error());
    }
  }

  if (collectionUuid.isEmpty() || sourcePath.isEmpty()) return false;

  auto existingResult = ItemMetadataStore::load(db, collectionUuid, sourcePath);
  ItemMetadataStore::ItemMetadata existing;
  if (existingResult.isError()) {
    ErrorUtils::logError(existingResult.error());
    // Fall through with an empty stub so a transient read failure
    // doesn't drop the scraped data on the floor — the merge will
    // treat every existing field as empty and save scrape-as-is.
    existing.collectionUuid = collectionUuid;
    existing.path = sourcePath;
  } else {
    existing = existingResult.value();
  }

  const auto merged =
      mergeScrapedIntoExisting(std::move(existing), collectionUuid, sourcePath, scraped);
  auto saveResult = ItemMetadataStore::save(db, merged);
  if (saveResult.isError()) {
    ErrorUtils::logError(saveResult.error());
    return false;
  }
  return true;
}

ApplyResult applyScrapedItem(IDatabaseManager *databaseManager, const QString &collectionUuid,
                             const QString &sourcePath, const QString &artworkDirectory,
                             const QString &baseName, const ScrapedItem &scraped,
                             const QList<PendingMediaWrite> &media, RescrapeMode rescrapeMode) {
  // Synchronous composer for callers that don't need the phases
  // split (the right-click single-item path + the test suite).
  // BatchScrapeRunner uses the underlying primitives directly so the
  // file-I/O phase can run on a worker thread.
  const MediaWriteResult writes = writeMediaFiles(artworkDirectory, baseName, media, rescrapeMode);
  ApplyResult result;
  result.mediaWritten = writes.mediaWritten;
  result.mediaSkipped = writes.mediaSkipped;
  result.writtenPaths = writes.writtenPaths;
  result.firstFailures = writes.firstFailures;
  result.metadataSaved = saveScrapedMetadata(databaseManager, collectionUuid, sourcePath, scraped,
                                             writes.nonStandardArtwork);
  return result;
}

} // namespace Scraper
