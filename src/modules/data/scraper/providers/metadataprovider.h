#ifndef METADATAPROVIDER_H
#define METADATAPROVIDER_H

#include <QFlags>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

#include "scrapertypes.h"

/// Abstract base for metadata / artwork lookup providers.
///
/// Stage 1 (what this file currently supports): web-search providers
/// that just return a URL the user can open in their browser. Stage 2+:
/// API providers that asynchronously fetch structured metadata +
/// downloadable media. The Capability flags below already reserve the
/// surface for those follow-ups so the API providers can subclass
/// MetadataProvider without breaking Stage-1 consumers.
class MetadataProvider {
public:
  enum class Capability {
    /// Provider can return a search URL for a query. Stage 1 baseline —
    /// the context-menu "Look up online" submenu uses this exclusively.
    WebSearch = 0x1,
    /// (Future) Provider can asynchronously fetch structured metadata
    /// (title / description / genre / etc.) for an item. Stage 2.
    MetadataLookup = 0x2,
    /// (Future) Provider can return downloadable media URLs (cover,
    /// screenshot, fanart, etc.) for a matched item. Stage 2.
    MediaFetch = 0x4,
  };
  Q_DECLARE_FLAGS(Capabilities, Capability)

  virtual ~MetadataProvider() = default;

  /// Stable id used to persist "which provider did this metadata come
  /// from" in ItemMetadata.source and to remember per-collection
  /// provider preferences. Must be lowercase ASCII, no whitespace —
  /// e.g. "screenscraper", "mobygames", "tmdb".
  [[nodiscard]] virtual QString id() const = 0;

  /// Pretty label for the context-menu / settings-panel UI.
  [[nodiscard]] virtual QString displayName() const = 0;

  /// Lowercase category tags this provider applies to. The
  /// CollectionConfig::type free-form label is normalised and matched
  /// against this list, so a provider can advertise itself for several
  /// near-synonyms (e.g. TMDB lists {"video","movies","tv"}). An empty
  /// list means "applies to every collection regardless of type".
  [[nodiscard]] virtual QStringList categories() const = 0;

  /// What this provider can actually do. Stage 1 providers return only
  /// `WebSearch`; Stage 2+ providers will add `MetadataLookup` /
  /// `MediaFetch`.
  [[nodiscard]] virtual Capabilities capabilities() const = 0;

  /// Which catalog-entity types this provider can scrape. Defaults to
  /// `{Game}` — the per-file unit every provider supports today. The
  /// entity-scraping sub-tasks (Kartend-ckepd) override this on providers
  /// that can also scrape Platform / Collection / Category art so the
  /// registry can resolve an entity-type-aware provider. Returning a list
  /// (not flags) keeps the door open for entity-type-specific ordering.
  [[nodiscard]] virtual QList<Scraper::ScrapeEntityType> supportedEntities() const {
    return {Scraper::ScrapeEntityType::Game};
  }

  /// Returns a search URL for the given query when `WebSearch` is in
  /// `capabilities()`. Implementations should percent-encode the
  /// query. Default falls back to an invalid QUrl so non-web providers
  /// don't have to override.
  [[nodiscard]] virtual QUrl searchUrl(const QString &query) const {
    Q_UNUSED(query);
    return {};
  }
};

Q_DECLARE_OPERATORS_FOR_FLAGS(MetadataProvider::Capabilities)

#endif // METADATAPROVIDER_H
