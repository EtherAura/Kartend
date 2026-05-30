#ifndef KARTEND_UTILS_APP_COLLECTION_SCRAPER_SETTINGS_H
#define KARTEND_UTILS_APP_COLLECTION_SCRAPER_SETTINGS_H

// Leaf struct peeled out of GeneralSettings (Kartend-q1w6). Scraper
// credentials + performance/behavior options. The preset + re-scrape enums
// and ScraperOptions (formerly nested inside GeneralSettings) are relocated
// here as top-level types. ScraperOptions keeps its value-equality operators
// because it is the sole settings domain with a hot-reload change signal.

#include <QHash>
#include <QString>

// Speed/quality presets and re-scrape policy. The four numeric fields
// are tied to a preset combo (Fastest / Balanced / BestQuality /
// Custom); switching to Custom unlocks them. RescrapeMode controls
// how an item already in the DB is treated on a second scrape and
// applies per-asset (an existing cover skips, a new fanart still
// downloads). UpdateChanged downloads bytes anyway to compare, so
// it's intentionally the slowest mode; the UI surfaces this.
enum class ScraperPreset { Fastest = 0, Balanced = 1, BestQuality = 2, Custom = 3 };
enum class ScraperRescrapeMode {
  Overwrite = 0,     // always replace existing files + rows
  FillMissing = 1,   // only download / write what's not on disk
  UpdateChanged = 2, // download all, compare bytes, write only if different
  Skip = 3,          // skip the whole item if any metadata exists
};

struct ScraperOptions {
  ScraperPreset preset = ScraperPreset::Balanced;
  int mediaMaxDimension = 1024; // px; 0 = full resolution
  // Default 2: empirical sweet spot for the SS CDN on typical home
  // connections. Higher values (6-8) caused individual replies to
  // stretch from seconds to minutes as TCP fairness collapsed
  // between competing streams to the same host. With HTTP/2 enabled
  // (httpclient.cpp send()) streams multiplex over one connection,
  // sidestepping that collapse — so users on a healthy network can
  // safely bump this up.
  int mediaConcurrency = 2;  // 1..16
  int mediaThrottleMs = 100; // 0..1000
  // Number of items that can be scraped *in parallel* during a batch
  // scrape. Default 4 matches Skyscraper's `--threads 4`. Each item
  // still pays its own media-concurrency budget, so the total
  // in-flight network requests is `batchItemConcurrency *
  // mediaConcurrency` — keep this product sane for your link.
  // 1 = strictly serial (the legacy behavior).
  int batchItemConcurrency = 4; // 1..16
  ScraperRescrapeMode rescrapeMode = ScraperRescrapeMode::FillMissing;
  // Refresh window (in days) used by the rescrape modes that
  // pre-filter the queue (Skip and FillMissing) to decide whether
  // an already-covered item should be re-scraped this run.
  //   0      = no time gate; skip every covered item (the legacy
  //            behaviour, preserved for users that never want to
  //            refresh).
  //   N > 0  = skip items whose last scrape (item_metadata.updated_at,
  //            or the sidecar JSON's mtime when there is no DB row)
  //            falls within the last N days. Items covered longer
  //            ago become eligible to refresh. Overwrite and
  //            UpdateChanged ignore this — they visit every item by
  //            design.
  // Range clamped 0..365 by SettingsManager. Default 30 — long enough
  // to let users page through a quota-limited backlog over multiple
  // days without re-paying for items just scraped, short enough that
  // a monthly refresh works without manual intervention.
  int skipRecentScrapeDays = 30;
  // Stamp `outputformat=jpg` onto image media URLs so SS re-encodes
  // assets as smaller (lossy) JPGs. Only sensible on the Fastest
  // preset where bandwidth dominates fidelity; the preset toggles
  // this field automatically, but a Custom user can flip it on too.
  bool preferJpgOutput = false;
  // When true, an interrupted scrape (process exit / crash mid-batch
  // with a `pending-scrape.json` snapshot on disk) silently resumes
  // on next launch instead of surfacing the modal Resume / Discard
  // prompt. Default off so first-time users see the prompt and learn
  // the recovery path; power users running unattended overnight
  // batches flip this on so a crash + relaunch self-heals without a
  // dialog blocking the resume. Consumed by MainWindow's startup
  // hook around ScraperService::loadPendingState (Kartend-1uvp).
  bool scrapeAutoResume = false;
  // When true, scrape diagnostic logging is enabled: the kartend.scrape*
  // logging categories are raised to debug+info verbosity and their
  // output is teed to a size-capped `scrape.log` in the app config
  // directory. Default off. A GUI build has no visible stderr, so this
  // is the only way for a user to capture what a scrape did when it
  // misbehaves (a crash, a stuck resume). Consumed by ScrapeLogger,
  // toggled from SettingsManager load/save.
  bool scrapeLogging = false;
  // Fallback region for ScreenScraper's region-keyed fields (title,
  // release date, box art), as an SS region shortname ("us", "eu",
  // "jp", "wor", ...). Each scraped item first honours its own
  // matched-ROM region so a Japanese cart keeps its Japanese title
  // and art; this value only backstops items whose region has no
  // entry. Default "us" preserves the historical behaviour.
  QString preferredScraperRegion = QStringLiteral("us");

  // Hash-mode policy for ROM identification (Kartend-ou0a).
  //   Always:    always try to hash, even multi-GB disc images. SS hash-ID
  //              is the most accurate match path but the slowest for big
  //              archives (PS2 .zip can spend 5-10 min extracting).
  //   SizeGated: hash only when the source is ≤ maxHashableSizeMB. Files
  //              over the limit skip straight to filename-based matching.
  //              The default 4096 MB covers most PS1/PS2 ISOs in zip and
  //              excludes Blu-ray-sized images that would never extract
  //              in a sane timeout.
  //   Never:     never hash. Filename-only matching. Useful when the user
  //              trusts their filenames and doesn't want the extract cost.
  enum class ScraperHashMode { Always = 0, SizeGated = 1, Never = 2 };
  ScraperHashMode hashMode = ScraperHashMode::Always;
  int maxHashableSizeMB = 4096;

  // Region-source policy for ScreenScraper match disambiguation
  // (Kartend-ou0a).
  //   TrustScraperFirst:      SS's matched-ROM region wins; a No-Intro
  //                           tag in the filename ("(Japan)") is the
  //                           fallback ONLY when hash-ID didn't narrow
  //                           the candidate. Current default behaviour.
  //   FilenameWhenAvailable:  the filename's region tag always preempts
  //                           SS's matched-ROM region. Use when SS's
  //                           per-file region tagging is unreliable
  //                           (multi-region ROM records, hash collisions).
  //   ScraperOnly:            never look at the filename. Pre-Kartend-ou0a
  //                           behaviour. Use when filenames are noisy and
  //                           SS hashes are trusted.
  enum class ScraperRegionSource {
    TrustScraperFirst = 0,
    FilenameWhenAvailable = 1,
    ScraperOnly = 2,
  };
  ScraperRegionSource regionSource = ScraperRegionSource::TrustScraperFirst;

  // Value-equality for the per-domain hot-reload signal: the change
  // emit in SettingsManager::saveGeneralSettings only fires when this
  // returns false, so a Save that didn't actually touch ScraperOptions
  // doesn't wake the background scraper consumers.
  bool operator==(const ScraperOptions &other) const {
    return preset == other.preset && mediaMaxDimension == other.mediaMaxDimension &&
           mediaConcurrency == other.mediaConcurrency && mediaThrottleMs == other.mediaThrottleMs &&
           batchItemConcurrency == other.batchItemConcurrency &&
           rescrapeMode == other.rescrapeMode &&
           skipRecentScrapeDays == other.skipRecentScrapeDays &&
           preferJpgOutput == other.preferJpgOutput && scrapeAutoResume == other.scrapeAutoResume &&
           scrapeLogging == other.scrapeLogging &&
           preferredScraperRegion == other.preferredScraperRegion && hashMode == other.hashMode &&
           maxHashableSizeMB == other.maxHashableSizeMB && regionSource == other.regionSource;
  }
  bool operator!=(const ScraperOptions &other) const { return !(*this == other); }
};

struct ScraperSettings {
  // Per-provider key/value blobs — one inner QHash per provider id
  // (e.g. "tmdb", "screenscraper") keyed on the credential field name
  // ("api_token", "dev_id", "user_password"). Persisted as
  // `[Scrapers]` INI keys of the shape `<provider>/<field>=<value>`.
  // Empty inner hash = "not configured"; the provider is responsible
  // for surfacing a "please set credentials in Settings → Scrapers"
  // error message when its required keys are missing.
  QHash<QString, QHash<QString, QString>> credentials;
  // Scraper performance + behavior options. Sole settings domain with a
  // value-equality operator because it drives the scraperOptionsChanged
  // hot-reload signal.
  ScraperOptions options;
};

#endif // KARTEND_UTILS_APP_COLLECTION_SCRAPER_SETTINGS_H
