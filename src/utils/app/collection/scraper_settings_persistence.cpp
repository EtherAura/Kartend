#include "scraper_settings_persistence.h"

#include "settingskeys.h"

namespace keys = kartend::settings::keys;

namespace ScraperSettingsPersistence {

void loadOptions(QSettings &settings, ScraperOptions &opts) {
  opts.preset = static_cast<ScraperPreset>(
      settings.value(keys::kPreset, static_cast<int>(ScraperPreset::Balanced)).toInt());
  opts.mediaMaxDimension = qBound(0, settings.value(keys::kMediaMaxDimension, 1024).toInt(), 8192);
  opts.mediaConcurrency = qBound(1, settings.value(keys::kMediaConcurrency, 2).toInt(), 16);
  opts.mediaThrottleMs = qBound(0, settings.value(keys::kMediaThrottleMs, 100).toInt(), 5000);
  opts.batchItemConcurrency = qBound(1, settings.value(keys::kBatchItemConcurrency, 4).toInt(), 16);
  opts.rescrapeMode = static_cast<ScraperRescrapeMode>(
      settings.value(keys::kRescrapeMode, static_cast<int>(ScraperRescrapeMode::FillMissing))
          .toInt());
  // Clamp 0..365 — defensive against hand-edited INIs. 0 disables the time
  // gate (skip every already-scraped item); 365 is a year.
  opts.skipRecentScrapeDays =
      qBound(0, settings.value(keys::kSkipRecentScrapeDays, 30).toInt(), 365);
  opts.preferJpgOutput = settings.value(keys::kPreferJpgOutput, false).toBool();
  opts.scrapeAutoResume = settings.value(keys::kScrapeAutoResume, false).toBool();
  opts.scrapeLogging = settings.value(keys::kScrapeLogging, false).toBool();
  opts.preferredScraperRegion =
      settings.value(keys::kPreferredRegion, QStringLiteral("us")).toString().trimmed().toLower();
  // Enum ints come back as QVariant; default to the in-memory struct defaults
  // (Always, TrustScraperFirst) so a fresh install picks the historical
  // behaviour. Clamping guards a hand-edited INI from an out-of-range value.
  const int rawHashMode =
      settings
          .value(keys::kScraperHashMode, static_cast<int>(ScraperOptions::ScraperHashMode::Always))
          .toInt();
  opts.hashMode = static_cast<ScraperOptions::ScraperHashMode>(qBound(0, rawHashMode, 2));
  opts.maxHashableSizeMB =
      qBound(1, settings.value(keys::kScraperMaxHashableSizeMB, 4096).toInt(), 65536);
  const int rawRegionSource =
      settings
          .value(keys::kScraperRegionSource,
                 static_cast<int>(ScraperOptions::ScraperRegionSource::TrustScraperFirst))
          .toInt();
  opts.regionSource =
      static_cast<ScraperOptions::ScraperRegionSource>(qBound(0, rawRegionSource, 2));
  opts.datLibraryPath = settings.value(keys::kDatLibraryPath, QString()).toString().trimmed();
}

void saveOptions(QSettings &settings, const ScraperOptions &opts) {
  settings.setValue(keys::kPreset, static_cast<int>(opts.preset));
  settings.setValue(keys::kMediaMaxDimension, opts.mediaMaxDimension);
  settings.setValue(keys::kMediaConcurrency, opts.mediaConcurrency);
  settings.setValue(keys::kMediaThrottleMs, opts.mediaThrottleMs);
  settings.setValue(keys::kBatchItemConcurrency, opts.batchItemConcurrency);
  settings.setValue(keys::kRescrapeMode, static_cast<int>(opts.rescrapeMode));
  settings.setValue(keys::kSkipRecentScrapeDays, opts.skipRecentScrapeDays);
  settings.setValue(keys::kPreferJpgOutput, opts.preferJpgOutput);
  settings.setValue(keys::kScrapeAutoResume, opts.scrapeAutoResume);
  settings.setValue(keys::kScrapeLogging, opts.scrapeLogging);
  settings.setValue(keys::kPreferredRegion, opts.preferredScraperRegion);
  settings.setValue(keys::kScraperHashMode, static_cast<int>(opts.hashMode));
  settings.setValue(keys::kScraperMaxHashableSizeMB, opts.maxHashableSizeMB);
  settings.setValue(keys::kScraperRegionSource, static_cast<int>(opts.regionSource));
  settings.setValue(keys::kDatLibraryPath, opts.datLibraryPath);
}

} // namespace ScraperSettingsPersistence
