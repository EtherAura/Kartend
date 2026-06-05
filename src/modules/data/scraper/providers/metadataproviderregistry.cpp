// Built-in metadata provider registry. The curated list lives here so
// adding a new Stage-1 URL provider is a single-line edit in `builtIn()`
// and adding a Stage-2 API provider is just one more `make_unique<...>`
// line below the URL block.
#include "metadataproviderregistry.h"

#include <QHash>

#include "metadataprovider.h"
#include "musicbrainzprovider.h"
#include "openlibraryprovider.h"
#include "screenscraperprovider.h"
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

std::unique_ptr<MetadataProvider> makeUrl(const char *id, const char *name, const QStringList &cats,
                                          const char *tpl) {
  return std::make_unique<WebSearchProvider>(QString::fromLatin1(id), QString::fromLatin1(name),
                                             cats, QString::fromLatin1(tpl));
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
  std::vector<std::unique_ptr<MetadataProvider>> list;

  // ── Games (3) ────────────────────────────────────────────────────
  // ScreenScraper.fr uses the API-backed provider when accessors are
  // supplied (so user-supplied dev/user creds + per-collection
  // systemeid come through); URL fallback capability stays via
  // WebSearch. MobyGames + IGDB are URL-only.
  list.push_back(std::make_unique<ScreenScraperProvider>(settingsAccessor, collectionAccessor));
  list.push_back(
      makeUrl("mobygames", "MobyGames", {CAT_GAMES}, "https://www.mobygames.com/search/?q=%1"));
  list.push_back(makeUrl("igdb", "IGDB", {CAT_GAMES}, "https://www.igdb.com/search?type=1&q=%1"));

  // ── Video (2) ────────────────────────────────────────────────────
  // TMDB uses the API-backed provider when a settings accessor is
  // supplied (so the user-supplied token comes through); URL fallback
  // capability stays via WebSearch. IMDb has no usable open API and
  // remains URL-only.
  list.push_back(std::make_unique<TmdbProvider>(settingsAccessor));
  list.push_back(makeUrl("imdb", "IMDb", {CAT_VIDEO}, "https://www.imdb.com/find/?q=%1"));

  // ── Audio (2) ────────────────────────────────────────────────────
  // MusicBrainz uses the full API-backed provider (search URL still
  // available via WebSearch capability for the "Look up online" menu).
  // Discogs stays URL-only until the auth-backed integration ships.
  list.push_back(std::make_unique<MusicBrainzProvider>());
  list.push_back(
      makeUrl("discogs", "Discogs", {CAT_AUDIO}, "https://www.discogs.com/search/?q=%1"));

  // ── Reference (2) ────────────────────────────────────────────────
  // Open Library uses the full API-backed provider (search URL still
  // available via WebSearch capability for the "Look up online" menu).
  // Google Books stays URL-only — its API requires a project key and
  // is intentionally deferred behind a follow-up.
  list.push_back(std::make_unique<OpenLibraryProvider>());
  list.push_back(makeUrl("googlebooks", "Google Books", {CAT_REFERENCE},
                         "https://www.google.com/search?tbm=bks&q=%1"));

  return list;
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

} // namespace MetadataProviderRegistry
