// Built-in metadata provider registry. The curated list lives here as a
// static spec table (`providerSpecs()`): adding a Stage-1 URL provider is
// one data row, adding a Stage-2 API provider is one row with a `makeApi`
// factory. `builtIn()` constructs the full list from the table;
// `claimLookupProvider()` selects on the table's static metadata and
// constructs only the chosen provider.
#include "metadataproviderregistry.h"

#include <QHash>

#include "collection/collectionconfig.h"
#include "metadatalookupprovider.h"
#include "metadataprovider.h"
#include "musicbrainzprovider.h"
#include "openlibraryprovider.h"
#include "screenscraperprovider.h"
#include "steamstoreprovider.h"
#include "tmdbprovider.h"
#include "websearchprovider.h"

namespace MetadataProviderRegistry {

namespace {

/// Canonical category tags. Providers list these (or synonyms — see
/// normaliseCategory) so menu-grouping stays consistent.
constexpr const char *CAT_GAMES = "games";
constexpr const char *CAT_VIDEO = "video";
constexpr const char *CAT_AUDIO = "audio";
constexpr const char *CAT_REFERENCE = "reference";

/// Synonym table: anything the user might have typed in
/// `CollectionConfig::type` → canonical tag. The matcher only does
/// case-insensitive equality on the normalised values, so a one-line
/// entry per synonym keeps the rule trivial to reason about.
QString synonymOf(const QString &raw) {
  static const QHash<QString, QString> table = {
      {"game", "games"},
      {"games", "games"},
      {"rom", "games"},
      {"roms", "games"},
      {"emulator", "games"},
      {"emulators", "games"},
      {"video", "video"},
      {"videos", "video"},
      {"movie", "video"},
      {"movies", "video"},
      {"film", "video"},
      {"films", "video"},
      {"tv", "video"},
      {"tvshow", "video"},
      {"show", "video"},
      {"shows", "video"},
      {"audio", "audio"},
      {"music", "audio"},
      {"song", "audio"},
      {"songs", "audio"},
      {"album", "audio"},
      {"albums", "audio"},
      {"podcast", "audio"},
      {"podcasts", "audio"},
      {"reference", "reference"},
      {"book", "reference"},
      {"books", "reference"},
      {"document", "reference"},
      {"documents", "reference"},
      {"pdf", "reference"},
      {"pdfs", "reference"},
      {"manual", "reference"},
      {"manuals", "reference"},
      {"comic", "reference"},
      {"comics", "reference"},
  };
  return table.value(raw, QString());
}

/// One row per built-in provider, in display order. The URL-only rows are
/// fully described by their row (`urlTemplate` set, `makeApi` null) — the
/// WebSearchProvider instance is constructed FROM the row, so their metadata
/// cannot drift. The API rows (`makeApi` set) mirror the class's own id() /
/// categories() / MetadataLookup capability so claimLookupProvider() can pick
/// a provider WITHOUT constructing the whole registry first; that mirror is
/// pinned by test_metadataproviderregistry.cpp
/// (claimLookupProvider_specMetadataMatchesBuiltIn).
struct ProviderSpec {
  const char *id;
  /// Display label — consumed only for URL rows (API rows read theirs from
  /// the class), so it may be null on API rows.
  const char *displayName;
  QStringList categories;
  /// Whether the constructed provider advertises Capability::MetadataLookup
  /// (always false for URL rows — WebSearchProvider is WebSearch-only).
  bool lookupCapable;
  /// Search-URL template for a WebSearchProvider row; null for API rows.
  const char *urlTemplate;
  /// Factory for API-backed rows; null for URL rows. Captureless so the
  /// table stays constexpr-friendly plain data.
  std::unique_ptr<MetadataProvider> (*makeApi)(const GeneralSettingsAccessor &,
                                               const CollectionAccessor &);
};

const std::vector<ProviderSpec> &providerSpecs() {
  static const std::vector<ProviderSpec> specs = [] {
    std::vector<ProviderSpec> list;

    // ── Games (3) ────────────────────────────────────────────────────
    // ScreenScraper.fr uses the API-backed provider when accessors are
    // supplied (so user-supplied dev/user creds + per-collection
    // systemeid come through); URL fallback capability stays via
    // WebSearch. MobyGames + IGDB are URL-only.
    list.push_back(
        {.id = "screenscraper",
         .displayName = nullptr,
         .categories = {CAT_GAMES},
         .lookupCapable = true,
         .urlTemplate = nullptr,
         .makeApi = [](const GeneralSettingsAccessor &settings,
                       const CollectionAccessor &collection) -> std::unique_ptr<MetadataProvider> {
           return std::make_unique<ScreenScraperProvider>(settings, collection);
         }});
    list.push_back({.id = "mobygames",
                    .displayName = "MobyGames",
                    .categories = {CAT_GAMES},
                    .lookupCapable = false,
                    .urlTemplate = "https://www.mobygames.com/search/?q=%1",
                    .makeApi = nullptr});
    list.push_back({.id = "igdb",
                    .displayName = "IGDB",
                    .categories = {CAT_GAMES},
                    .lookupCapable = false,
                    .urlTemplate = "https://www.igdb.com/search?type=1&q=%1",
                    .makeApi = nullptr});
    // Steam storefront (Kartend-ksjx0): key-less; descriptions, screenshots,
    // backgrounds, trailers. Placed AFTER screenscraper so category
    // auto-selection keeps ScreenScraper the games default — launcher-import
    // Steam collections pin scraperProviderId="steam" explicitly, and the
    // provider resolves .kartlink stubs to exact appids (no name matching).
    list.push_back({.id = "steam",
                    .displayName = nullptr,
                    .categories = {CAT_GAMES},
                    .lookupCapable = true,
                    .urlTemplate = nullptr,
                    .makeApi = [](const GeneralSettingsAccessor &,
                                  const CollectionAccessor &) -> std::unique_ptr<MetadataProvider> {
                      return std::make_unique<SteamStoreProvider>();
                    }});

    // ── Video (2) ────────────────────────────────────────────────────
    // TMDB uses the API-backed provider when a settings accessor is
    // supplied (so the user-supplied token comes through); URL fallback
    // capability stays via WebSearch. IMDb has no usable open API and
    // remains URL-only.
    list.push_back(
        {.id = "tmdb",
         .displayName = nullptr,
         .categories = {CAT_VIDEO},
         .lookupCapable = true,
         .urlTemplate = nullptr,
         .makeApi = [](const GeneralSettingsAccessor &settings,
                       const CollectionAccessor &collection) -> std::unique_ptr<MetadataProvider> {
           return std::make_unique<TmdbProvider>(settings, collection);
         }});
    list.push_back({.id = "imdb",
                    .displayName = "IMDb",
                    .categories = {CAT_VIDEO},
                    .lookupCapable = false,
                    .urlTemplate = "https://www.imdb.com/find/?q=%1",
                    .makeApi = nullptr});

    // ── Audio (2) ────────────────────────────────────────────────────
    // MusicBrainz uses the full API-backed provider (search URL still
    // available via WebSearch capability for the "Look up online" menu).
    // Discogs stays URL-only until the auth-backed integration ships.
    list.push_back({.id = "musicbrainz",
                    .displayName = nullptr,
                    .categories = {CAT_AUDIO},
                    .lookupCapable = true,
                    .urlTemplate = nullptr,
                    .makeApi = [](const GeneralSettingsAccessor &,
                                  const CollectionAccessor &) -> std::unique_ptr<MetadataProvider> {
                      return std::make_unique<MusicBrainzProvider>();
                    }});
    list.push_back({.id = "discogs",
                    .displayName = "Discogs",
                    .categories = {CAT_AUDIO},
                    .lookupCapable = false,
                    .urlTemplate = "https://www.discogs.com/search/?q=%1",
                    .makeApi = nullptr});

    // ── Reference (2) ────────────────────────────────────────────────
    // Open Library uses the full API-backed provider (search URL still
    // available via WebSearch capability for the "Look up online" menu).
    // Google Books stays URL-only — its API requires a project key and
    // is intentionally deferred behind a follow-up.
    list.push_back({.id = "openlibrary",
                    .displayName = nullptr,
                    .categories = {CAT_REFERENCE},
                    .lookupCapable = true,
                    .urlTemplate = nullptr,
                    .makeApi = [](const GeneralSettingsAccessor &,
                                  const CollectionAccessor &) -> std::unique_ptr<MetadataProvider> {
                      return std::make_unique<OpenLibraryProvider>();
                    }});
    list.push_back({.id = "googlebooks",
                    .displayName = "Google Books",
                    .categories = {CAT_REFERENCE},
                    .lookupCapable = false,
                    .urlTemplate = "https://www.google.com/search?tbm=bks&q=%1",
                    .makeApi = nullptr});

    return list;
  }();
  return specs;
}

std::unique_ptr<MetadataProvider> constructFromSpec(const ProviderSpec &spec,
                                                    const GeneralSettingsAccessor &settingsAccessor,
                                                    const CollectionAccessor &collectionAccessor) {
  if (spec.makeApi) {
    return spec.makeApi(settingsAccessor, collectionAccessor);
  }
  return std::make_unique<WebSearchProvider>(QString::fromLatin1(spec.id),
                                             QString::fromLatin1(spec.displayName), spec.categories,
                                             QString::fromLatin1(spec.urlTemplate));
}

/// forCategory()'s match rule, applied to a spec row: an empty normalised
/// category matches every row (untagged collection → full menu) and an empty
/// row category list means "applies everywhere".
bool specMatchesCategory(const ProviderSpec &spec, const QString &normalisedCategory) {
  if (normalisedCategory.isEmpty() || spec.categories.isEmpty()) {
    return true;
  }
  for (const QString &cat : spec.categories) {
    if (cat.compare(normalisedCategory, Qt::CaseInsensitive) == 0) {
      return true;
    }
  }
  return false;
}

/// Selection half of claimLookupProvider(): override id first (falling
/// through to the category path when the id is unknown or names a
/// non-lookup provider — same as the old ScraperController picker), then
/// the first lookup-capable category match in display order.
const ProviderSpec *selectLookupSpec(const QString &overrideId, const QString &collectionType) {
  const auto &specs = providerSpecs();
  if (!overrideId.isEmpty()) {
    for (const auto &spec : specs) {
      if (overrideId == QLatin1String(spec.id)) {
        if (spec.lookupCapable) return &spec;
        break;
      }
    }
  }
  const QString normalised = normaliseCategory(collectionType);
  for (const auto &spec : specs) {
    if (spec.lookupCapable && specMatchesCategory(spec, normalised)) return &spec;
  }
  return nullptr;
}

} // namespace

QString normaliseCategory(const QString &raw) {
  const QString lower = raw.trimmed().toLower();
  if (lower.isEmpty()) {
    return {};
  }
  // If the raw value is already a canonical tag, no synonym lookup
  // needed. Otherwise fall back to the synonym table; if neither hits
  // we return the raw lowercase value so a user's custom tag like
  // "homebrew" still matches a provider that opted into that exact
  // tag in its `categories()` list.
  if (lower == CAT_GAMES || lower == CAT_VIDEO || lower == CAT_AUDIO || lower == CAT_REFERENCE) {
    return lower;
  }
  const QString synonym = synonymOf(lower);
  return synonym.isEmpty() ? lower : synonym;
}

std::vector<std::unique_ptr<MetadataProvider>>
builtIn(const GeneralSettingsAccessor &settingsAccessor,
        const CollectionAccessor &collectionAccessor) {
  const auto &specs = providerSpecs();
  std::vector<std::unique_ptr<MetadataProvider>> list;
  list.reserve(specs.size());
  for (const auto &spec : specs) {
    list.push_back(constructFromSpec(spec, settingsAccessor, collectionAccessor));
  }
  return list;
}

std::shared_ptr<MetadataLookupProvider>
claimLookupProvider(const CollectionConfig &cfg, const GeneralSettingsAccessor &settingsAccessor,
                    const CollectionAccessor &collectionAccessor) {
  const ProviderSpec *spec =
      selectLookupSpec(cfg.scraperOverrides.scraperProviderId.trimmed(), cfg.type);
  if (!spec) return nullptr;
  std::unique_ptr<MetadataProvider> built =
      constructFromSpec(*spec, settingsAccessor, collectionAccessor);
  // Belt-and-braces re-check on the live instance: the row's `lookupCapable`
  // is static metadata, so if a class ever drops the MetadataLookup
  // capability (or stops deriving MetadataLookupProvider) return null rather
  // than a provider that can't run a scrape. The registry drift test keeps
  // these branches dead in practice.
  if (!built || !built->capabilities().testFlag(MetadataProvider::Capability::MetadataLookup)) {
    return nullptr;
  }
  auto *typed = dynamic_cast<MetadataLookupProvider *>(built.get());
  if (!typed) return nullptr;
  // Aliasing constructor: ownership rides the MetadataProvider control block
  // (virtual dtor, so deletion through the base is fine); the exposed
  // pointer is the downcast lookup view.
  std::shared_ptr<MetadataProvider> owner(std::move(built));
  return std::shared_ptr<MetadataLookupProvider>(std::move(owner), typed);
}

QList<ScraperChoice> scrapingProviders() {
  QList<ScraperChoice> out;
  for (const auto &p : builtIn()) {
    if (p->capabilities().testFlag(MetadataProvider::Capability::MetadataLookup)) {
      out.append({p->id(), p->displayName()});
    }
  }
  return out;
}

QString defaultScraperForType(const QString &collectionType) {
  const QString normalised = normaliseCategory(collectionType);
  if (normalised.isEmpty()) {
    return {};
  }
  const auto all = builtIn();
  for (MetadataProvider *p : forCategory(all, normalised)) {
    // forCategory with a non-empty category never yields the
    // "applies-everywhere" wildcard (no built-in provider has empty
    // categories()), so every hit here genuinely matches the type.
    if (p->capabilities().testFlag(MetadataProvider::Capability::MetadataLookup)) {
      return p->id();
    }
  }
  return {};
}

QList<MetadataProvider *> forCategory(const std::vector<std::unique_ptr<MetadataProvider>> &all,
                                      const QString &category) {
  const QString normalised = normaliseCategory(category);
  QList<MetadataProvider *> out;
  out.reserve(static_cast<int>(all.size()));
  if (normalised.isEmpty()) {
    // Untagged collection — surface every provider so the user can
    // still drive lookups. Grouping by category is handled by the
    // caller (context menu) via each provider's `categories()`.
    for (const auto &p : all) {
      out.append(p.get());
    }
    return out;
  }
  for (const auto &p : all) {
    // Empty categories() means "applies everywhere" — those always pass.
    if (p->categories().isEmpty()) {
      out.append(p.get());
      continue;
    }
    for (const QString &cat : p->categories()) {
      if (cat.compare(normalised, Qt::CaseInsensitive) == 0) {
        out.append(p.get());
        break;
      }
    }
  }
  return out;
}

QList<MetadataProvider *> forEntity(const std::vector<std::unique_ptr<MetadataProvider>> &all,
                                    Scraper::ScrapeEntityType entityType) {
  QList<MetadataProvider *> out;
  out.reserve(static_cast<int>(all.size()));
  for (const auto &p : all) {
    if (p->supportedEntities().contains(entityType)) {
      out.append(p.get());
    }
  }
  return out;
}

} // namespace MetadataProviderRegistry
