// Pure skip/coverage decisions for the batch scraper's Skip / FillMissing
// pre-filter. See the header for the extraction rationale; the bodies moved
// verbatim from BatchScrapeRunner with the runner's members threaded through
// ScrapeSkipContext / parameters instead.
#include "scrapeskipdecision.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

namespace Scraper {

bool coreScrapedFieldsComplete(const ItemMetadataStore::ItemMetadata &md) {
  // See the header: FillMissing treats these six fields as "the scraped metadata".
  // All must be non-empty; a partially-scraped row is not complete and stays
  // queued to fill its gaps (Kartend-em3jc). contentRating and players are
  // deliberately excluded — providers most often leave them blank, so requiring
  // them would re-scrape nearly every item on every FillMissing run.
  return !md.title.isEmpty() && !md.description.isEmpty() && !md.genre.isEmpty() &&
         !md.developer.isEmpty() && !md.publisher.isEmpty() && !md.releaseDate.isEmpty();
}

bool decideScrapeSkip(const SkipDecisionInputs &in) {
  if (in.mode == Scraper::RescrapeMode::Skip) {
    // Skip mode: any metadata marker is enough — "if scraped, leave it alone."
    // The refresh window optionally releases stale items back for a re-scrape.
    return in.metaPresent && in.metaWithinWindow;
  }
  // FillMissing: only burn a request when at least one ticked field is missing
  // or stale. If every ticked field (metadata + each wanted media type) is
  // covered and within the window, there is nothing to fetch — skip. "Metadata
  // covered" means COMPLETE (every core scraped field populated), not merely
  // present: a partially-scraped row stays queued to fill its gaps (Kartend-em3jc).
  bool fullyCovered = true;
  if (in.writeMetadata && (!in.metaComplete || !in.metaWithinWindow)) {
    fullyCovered = false;
  }
  if (fullyCovered && !in.allWantedMediaCovered) {
    fullyCovered = false;
  }
  // Media-only runs still honour the refresh window via any available
  // timestamp (sidecar mtime or DB row).
  if (fullyCovered && !in.writeMetadata && in.metaPresent && !in.metaWithinWindow) {
    fullyCovered = false;
  }
  return fullyCovered;
}

MediaCoverageIndex buildMediaCoverageIndex(const QString &artworkDir,
                                           const QSet<QString> &wantedTypes,
                                           bool sidecarCheckPossible) {
  // Build a basename-set per wanted type so the per-item check is an O(1) hash
  // lookup instead of a directory scan per item. Files in each type's subdir
  // are indexed by lowercase complete base name (the runner uses
  // completeBaseName when assembling per-item paths, so the key roundtrips).
  MediaCoverageIndex index;
  if (sidecarCheckPossible) {
    for (const QString &type : wantedTypes) {
      const QString subdir = QDir(artworkDir).filePath(type);
      QDir d(subdir);
      if (!d.exists()) continue;
      QSet<QString> bases;
      const auto files = d.entryList(QDir::Files | QDir::NoDotAndDotDot);
      bases.reserve(files.size());
      for (const QString &f : files) {
        bases.insert(QFileInfo(f).completeBaseName().toLower());
      }
      // operator[] returns T& so the assignment is a real move; QHash's
      // (const Key&, const T&) insert overload would have copied.
      index.presentByType[type] = std::move(bases);
    }
  }
  // `front` also mirrors to the flat artwork directory ({base}.<ext>) for the
  // grid tile — that is the slot the auto-discoverer reads. Treat either
  // location as "front covered".
  if (sidecarCheckPossible && wantedTypes.contains(QStringLiteral("front"))) {
    static const QStringList kImageGlobs = {QStringLiteral("*.png"),  QStringLiteral("*.jpg"),
                                            QStringLiteral("*.jpeg"), QStringLiteral("*.webp"),
                                            QStringLiteral("*.gif"),  QStringLiteral("*.bmp")};
    QDir d(artworkDir);
    const auto files = d.entryList(kImageGlobs, QDir::Files | QDir::NoDotAndDotDot);
    index.frontFlatBases.reserve(files.size());
    for (const QString &f : files) {
      index.frontFlatBases.insert(QFileInfo(f).completeBaseName().toLower());
    }
  }
  return index;
}

bool shouldSkipScrapedItem(const QString &path, const ScrapeSkipContext &ctx) {
  // Helper: decide whether the metadata "slot" is covered for an item.
  // Returns one of three states so the caller can apply the time-window
  // gate consistently for both DB and on-disk evidence.
  struct MetaPresence {
    bool present = false;
    bool hasTimestamp = false;
    QDateTime timestampUtc;
  };
  auto metadataPresenceFor = [&](const QString &p, const QString &baseName) -> MetaPresence {
    MetaPresence out;
    if (ctx.dbCheckPossible) {
      const auto md = ctx.metadataByPath.value(p);
      if (!md.source.isEmpty()) {
        out.present = true;
        // ItemMetadataStore writes updated_at via
        // QDateTime::currentDateTimeUtc().toString(Qt::ISODate), which
        // emits UTC values without the "Z" suffix; fromString() on
        // that returns a LocalTime-spec datetime carrying UTC values.
        // Appending the "Z" before parsing makes ISODate tag the
        // result as UTC directly — avoids the Qt 6.5-deprecated
        // setTimeSpec(Qt::UTC) re-label while staying portable to
        // CI's Qt 6.4.
        QString tsStr = md.updatedAt;
        if (!tsStr.isEmpty() && !tsStr.endsWith(QLatin1Char('Z'))) {
          tsStr.append(QLatin1Char('Z'));
        }
        const QDateTime ts = QDateTime::fromString(tsStr, Qt::ISODate);
        if (ts.isValid()) {
          out.hasTimestamp = true;
          out.timestampUtc = ts;
        }
      }
    }
    if (!out.present && ctx.sidecarCheckPossible && !baseName.isEmpty()) {
      const QString sidecar =
          QDir(ctx.artworkDir)
              .filePath(QStringLiteral("metadata/") + baseName + QStringLiteral(".json"));
      const QFileInfo fi(sidecar);
      if (fi.exists() && fi.isFile()) {
        out.present = true;
        const QDateTime mtime = fi.lastModified().toUTC();
        if (mtime.isValid()) {
          out.hasTimestamp = true;
          out.timestampUtc = mtime;
        }
      }
    }
    return out;
  };

  // Helper: per-type media-on-disk check using the pre-built indexes.
  auto typeCoveredFor = [&](const QString &baseNameLower, const QString &type) {
    if (type == QStringLiteral("front") && ctx.frontFlatBases.contains(baseNameLower)) {
      return true;
    }
    return ctx.presentByType.value(type).contains(baseNameLower);
  };

  // Helper: apply the window to a presence record. "Within window"
  // means the saved timestamp is at-or-after the cutoff. Items with
  // no readable timestamp keep the safe behaviour (preserve the
  // skip) — the user can clear the row or delete the file if they
  // really want a refresh.
  auto withinWindow = [&](const MetaPresence &mp) {
    if (!ctx.hasWindow) return true;
    if (!mp.hasTimestamp) return true;
    return mp.timestampUtc >= ctx.cutoff;
  };

  const QString baseName = QFileInfo(path).completeBaseName();
  const QString baseNameLower = baseName.toLower();
  const MetaPresence meta = metadataPresenceFor(path, baseName);

  // FillMissing metadata completeness: the metadata slot counts as covered only
  // when every core scraped field is populated, so a partially-scraped row stays
  // queued to fill its gaps (Kartend-em3jc). Only the DB row carries the field
  // values; without a DB (sidecar-only run) we cannot inspect fields, so fall
  // back to plain presence to preserve that path's behaviour.
  const bool metaComplete = ctx.dbCheckPossible
                                ? coreScrapedFieldsComplete(ctx.metadataByPath.value(path))
                                : meta.present;

  // FillMissing media coverage: every wanted media type must already be on
  // disk for the item to count as fully covered (the metadata + window halves
  // are folded in by decideScrapeSkip).
  bool allWantedMediaCovered = true;
  for (const QString &type : ctx.wantedTypes) {
    if (!typeCoveredFor(baseNameLower, type)) {
      allWantedMediaCovered = false;
      break;
    }
  }

  return decideScrapeSkip({ctx.mode, ctx.writeMetadata, meta.present, metaComplete,
                           withinWindow(meta), allWantedMediaCovered});
}

} // namespace Scraper
