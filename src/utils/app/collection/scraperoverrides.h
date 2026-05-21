#ifndef KARTEND_UTILS_APP_COLLECTION_SCRAPEROVERRIDES_H
#define KARTEND_UTILS_APP_COLLECTION_SCRAPEROVERRIDES_H

// Leaf struct extracted from collectionutils.h (Kartend-0yz3 step 5).
// Carrier for the per-collection scraper / DAT overrides. Kept in its
// own translation-unit-input so scrape-side providers can take a
// `const ScraperOverrides &` without dragging in CollectionConfig +
// UIConstants. collectionutils.h re-includes this header so existing
// callers compile unchanged.

#include <QString>
#include <QStringList>

/// Per-collection scraper / DAT overrides. Pinning a scraper provider,
/// overriding ScreenScraper's system id, and the DAT-file priority list
/// all live here. Originally flat fields on CollectionConfig; bundled
/// here so the scrape-side providers can take a `const ScraperOverrides &`
/// instead of reaching into the whole CollectionConfig. INI keys +
/// kart-manifest JSON keys round-trip unchanged.
struct ScraperOverrides {
  /// ScreenScraper.fr systemeid override. -1 = unset; the SS provider
  /// then runs its autodetect heuristic.
  int screenscraperSystemId = -1;
  /// When true and the scraped item is an archive (.zip / .7z / etc.),
  /// the SS provider extracts the archive and hashes the inner ROM
  /// rather than the outer archive bytes.
  bool screenscraperHashArchive = true;
  /// Paths to DAT files (No-Intro / Redump / TOSEC Logiqx, or MAME
  /// listxml) used for offline ROM ID. Walked in list order on every
  /// scrape — the first hash hit wins.
  QStringList datFilePaths;
  /// Metadata-scraper provider override for this collection. Empty
  /// means "automatic" — the scrape resolves a provider by matching the
  /// collection `type`. A non-empty value pins that exact scraper.
  QString scraperProviderId;

  bool operator==(const ScraperOverrides &other) const {
    return screenscraperSystemId == other.screenscraperSystemId &&
           screenscraperHashArchive == other.screenscraperHashArchive &&
           datFilePaths == other.datFilePaths && scraperProviderId == other.scraperProviderId;
  }
  bool operator!=(const ScraperOverrides &other) const { return !(*this == other); }
};

#endif
