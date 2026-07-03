#ifndef METADATAPROVIDERREGISTRY_H
#define METADATAPROVIDERREGISTRY_H

#include <functional>
#include <memory>
#include <vector>

#include <QList>
#include <QString>

#include "scrapertypes.h"

class MetadataLookupProvider;
class MetadataProvider;
struct CollectionConfig;
struct GeneralSettings;

/// Central registry of metadata / artwork lookup providers.
///
/// Stage 1 ships nine URL-only providers (3 games + 2 video + 2 music +
/// 2 reference); each is a WebSearchProvider instance with a fixed
/// search-URL template. Stage-2 API providers register here too once
/// their follow-up work lands; they just need to subclass
/// MetadataProvider and be added to the `builtIn()` list. The registry is intentionally
/// compile-time — there is no plugin loader, no dynamic registration, no DLL surface. New providers
/// ship inside the binary.
namespace MetadataProviderRegistry {

/// Function returning the live GeneralSettings, used by API providers
/// that need to read user-supplied credentials at request time. May
/// be null (or return null) — providers that need credentials will
/// surface a "not configured" error in that case.
using GeneralSettingsAccessor = std::function<const GeneralSettings *()>;

/// Function returning the active CollectionConfig for the next scrape.
/// Used by ScreenScraper to read the per-collection systemeid override
/// (or fall back to autodetect when -1). May be null (or return null)
/// — providers fall back to a system-agnostic query in that case.
using CollectionAccessor = std::function<const CollectionConfig *()>;

/// All built-in providers in display order. Caller owns the result.
/// Each call returns a fresh vector (cheap — these are small structs).
/// `std::vector` rather than `QList` because QList copies on grow and
/// MetadataProvider subclasses are owned via `std::unique_ptr` (move-only).
///
/// The optional accessors are wired into providers that need
/// per-request state. Call sites without easy access (e.g. unit
/// tests) can omit them — the affected providers behave as if their
/// credential blob / collection context is empty.
[[nodiscard]] std::vector<std::unique_ptr<MetadataProvider>>
builtIn(const GeneralSettingsAccessor &settingsAccessor = {},
        const CollectionAccessor &collectionAccessor = {});

/// Filter a list of providers by category. Case-insensitive match
/// against each provider's `categories()` list, with a small synonym
/// table baked in so the free-form `CollectionConfig::type` doesn't
/// have to be one of the canonical tags ("game" matches "games", etc.).
///
/// Passing an empty `category` returns every provider in the input —
/// useful for untagged collections that want the full menu.
///
/// Returned pointers are non-owning views into the caller-supplied
/// container — they stay valid until the unique_ptrs in `all` are destroyed.
[[nodiscard]] QList<MetadataProvider *>
forCategory(const std::vector<std::unique_ptr<MetadataProvider>> &all, const QString &category);

/// Filter providers to those that advertise support for `entityType` in
/// supportedEntities() (Kartend-ckepd.2). The resolution step for a non-game
/// entity scrape: a Platform scrape resolves only providers that can scrape a
/// Platform. Today every provider supports only `Game`, so a non-Game query
/// returns an empty list until a provider opts in (Kartend-ckepd.4/.5).
/// Non-owning views, same lifetime contract as forCategory().
[[nodiscard]] QList<MetadataProvider *>
forEntity(const std::vector<std::unique_ptr<MetadataProvider>> &all,
          Scraper::ScrapeEntityType entityType);

/// Resolve and construct ONLY the metadata-lookup provider a collection
/// scrape should use — the selection counterpart to forCategory() for the
/// batch/interactive scrape paths. An explicit
/// `CollectionConfig::scraperOverrides.scraperProviderId` wins when it names
/// a lookup-capable provider; otherwise the first lookup-capable provider
/// whose category matches the collection `type` is used (an empty type
/// matches everything, mirroring forCategory()'s full-menu behaviour).
/// Returns null when nothing usable matches.
///
/// Selection runs on the registry's static per-provider metadata, so the
/// non-matching siblings are never constructed — a multi-collection batch
/// that rebuilds its provider per collection switch no longer builds and
/// discards the whole registry (including a full ScreenScraperProvider) per
/// call. The accessors are wired into the one constructed provider exactly
/// as builtIn() would wire them; because the collection accessor typically
/// closes over one collection, the returned provider is fresh per call and
/// must not be reused for a different collection.
[[nodiscard]] std::shared_ptr<MetadataLookupProvider>
claimLookupProvider(const CollectionConfig &cfg,
                    const GeneralSettingsAccessor &settingsAccessor = {},
                    const CollectionAccessor &collectionAccessor = {});

/// Normalise a `CollectionConfig::type` value into the canonical
/// lowercase tag used by `forCategory()`. Exposed because the context
/// menu's "Look up online" submenu shows a different label per category
/// group and needs the normalised value to drive grouping.
[[nodiscard]] QString normaliseCategory(const QString &raw);

/// A scraping-capable provider as offered in the collection dialogs'
/// scraper picker — the stable `id` is what gets persisted into
/// `CollectionConfig::scraperProviderId`, `displayName` is the label.
struct ScraperChoice {
  QString id;
  QString displayName;
};

/// Every built-in provider that can actually run a metadata scrape
/// (i.e. advertises the `MetadataLookup` capability), in registry
/// display order. URL-only "look up online" providers are excluded —
/// pinning one as a collection's scraper would be a no-op. Needs no
/// accessors: id / displayName / capabilities are static per provider.
[[nodiscard]] QList<ScraperChoice> scrapingProviders();

/// Provider id of the first scraping-capable provider whose category
/// matches @p collectionType (after synonym normalisation), or an empty
/// string when the type resolves to no provider — untagged, an image
/// collection, or an unmatched custom tag. Drives the creation dialog's
/// "media type → scraper" auto-association.
[[nodiscard]] QString defaultScraperForType(const QString &collectionType);

} // namespace MetadataProviderRegistry

#endif // METADATAPROVIDERREGISTRY_H
