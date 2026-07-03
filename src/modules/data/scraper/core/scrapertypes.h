#ifndef SCRAPERTYPES_H
#define SCRAPERTYPES_H

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QUrl>

/// Common data shapes shared by every API-based MetadataLookupProvider
/// implementation. Kept in their own header so the abstract provider
/// API and the concrete providers (MusicBrainz today; ScreenScraper /
/// TMDB / Open Library in follow-ups) all work against the same types
/// without circular includes.
namespace Scraper {

/// Upper bound on the per-run failure messages retained for the scrape-error
/// details view — high enough to stay diagnostically complete for a
/// badly-misconfigured run, bounded so a pathological all-failing run can't grow
/// the list without limit. Shared by the persistence, batch-runner, and service
/// layers that append to a run's firstFailures so the bound can't drift
/// (Kartend-ubmkx).
inline constexpr int kMaxReportedFailures = 1000;

/// Where a piece of media lives in the provider's data model — used by
/// the persistence layer to route group/company-scoped art into a
/// `_shared/` directory instead of duplicating it per game. Drives
/// both the on-disk path (per-game vs shared) and the dialog's
/// pre-fetch dedup that lets a second game with the same `groupid`
/// skip the network entirely.
enum class MediaScope {
  Game,     ///< Per-game asset — ScreenScraper mediaJeu.php and friends.
  Group,    ///< Per-genre/theme/family/style — ScreenScraper mediaGroup.php.
  Company,  ///< Per-publisher/developer — ScreenScraper mediaCompagnie.php.
  Platform, ///< Per-platform/system art — ScreenScraper mediaSysteme.php (console
            ///< logo / illustration). Routed to `_shared/` like Group/Company;
            ///< scopeKey is the systemeid (Kartend-ckepd.3).
};

/// Filename prefix for a `_shared`-scoped media asset, used as
/// `_shared/<type>/<prefix><scopeKey>.<ext>`. Empty for Game (per-game layout,
/// never `_shared`). Centralised so the file-write router and the pre-download
/// dedup probe can't drift apart as scopes are added (Kartend-ckepd.3). The
/// switch has no default, so -Wswitch flags a newly-added scope.
[[nodiscard]] inline QString sharedScopePrefix(MediaScope scope) {
  switch (scope) {
  case MediaScope::Group:
    return QStringLiteral("group_");
  case MediaScope::Company:
    return QStringLiteral("company_");
  case MediaScope::Platform:
    return QStringLiteral("platform_");
  case MediaScope::Game:
    return {};
  }
  return {};
}

/// What kind of catalog entity a scrape targets. The default unit is a single
/// game/media item keyed by its file path; the entity-scraping work
/// (Kartend-ckepd) extends scraping to platform/collection/category-level art
/// and metadata that aren't tied to one file. Scaffolding only today — the
/// non-Game members are wired up by the follow-up sub-tasks.
enum class ScrapeEntityType {
  Game,       ///< A single media item, identified by its file path (the default unit).
  Platform,   ///< A console/system entity — e.g. a ScreenScraper systemeid → console art/logo.
  Collection, ///< A whole collection — collection logo / background / description.
  Category,   ///< A category/genre grouping.
};

/// Identifies the subject of a scrape. `Game` scrapes still flow through the
/// file-path-based runner; the other entity types carry an opaque identity key
/// the resolving provider understands (a ScreenScraper systemeid for Platform,
/// the collection uuid for Collection/Category) plus the owning collection
/// index so results route onto the right config / artwork directory.
struct EntityScrapeTarget {
  ScrapeEntityType type = ScrapeEntityType::Game;
  /// Provider-understood identity: a file path for Game, a systemeid for
  /// Platform, a collection uuid for Collection/Category.
  QString identity;
  /// Index of the collection this entity belongs to (-1 = unset). Lets the
  /// persistence layer route results onto the right CollectionConfig.
  int collectionIndex = -1;
};

/// Which CollectionConfig art slot an entity-scrape asset feeds once written.
/// Declared per MediaAsset by the provider that built it, so the generic
/// entity coordinator never has to know a provider's type-string vocabulary —
/// a second entity-capable provider tags its own assets instead of mimicking
/// ScreenScraper's canonical names.
enum class EntityArtRole {
  None,       ///< Not config-wired (the default; correct for all game-scope assets).
  Logo,       ///< Feeds headerLogoImage + collectionIcon.
  Background, ///< Feeds backgroundImage.
};

/// One downloadable media asset attached to a scraped item — typically
/// a cover/screenshot/fanart/marquee/video URL plus enough metadata
/// for the result dialog to label and group it.
struct MediaAsset {
  /// Free-form lowercase tag identifying the kind of media. Provider-
  /// specific (MusicBrainz uses "front" / "back"; ScreenScraper uses
  /// "box-2D" / "screenshot" / etc.). The result dialog buckets media
  /// by type via this string.
  QString type;
  /// User-visible label for the result dialog ("Front cover", "Box art").
  QString label;
  QUrl url;
  /// Scope of the asset — `Game` is the default and means per-game
  /// storage; `Group`/`Company` route the asset into `_shared/` and
  /// let the dialog skip duplicate downloads across games. Providers
  /// that don't expose group-level art leave this at `Game`.
  MediaScope scope = MediaScope::Game;
  /// Stable identifier for the scope bucket — empty for `Game` (the
  /// game's basename is the key); the SS groupid or companyid string
  /// for `Group` / `Company`. Used to name the shared file:
  ///   `_shared/<type>/group_<scopeKey>.<ext>` etc.
  QString scopeKey;
  /// Config-wiring role for entity scrapes — see EntityArtRole. Assets left
  /// at `None` are still downloaded and written, just never wired into the
  /// collection's config fields.
  EntityArtRole entityRole = EntityArtRole::None;
  /// Preference order WITHIN a role when several assets share it — lower
  /// wins (e.g. a wheel logo at 0 beats its monochrome fallback at 1). Only
  /// consulted for assets whose role is not `None`.
  int entityRolePriority = 0;
};

/// One match candidate returned by a provider's `lookup()`. Carries
/// just enough to render a row in the result-dialog candidate list and
/// (via `providerSpecificId`) re-fetch the full detail later.
struct ScrapeCandidate {
  /// Pretty title for the candidate row (typically "Artist — Album"
  /// for music, "Game Name (Year)" for games, etc.).
  QString displayName;
  /// Sub-line caption with year / region / format info, when the
  /// provider returns enough to disambiguate. Empty when not.
  QString subtitle;
  /// Provider-specific id used to re-fetch the full record via
  /// `fetchDetail()` (MusicBrainz MBID, ScreenScraper jeuid, TMDB id,
  /// etc.). Opaque to the result dialog.
  QString providerSpecificId;
  /// Optional thumbnail URL the result dialog can preview while the
  /// user picks. Invalid QUrl when no thumb is available.
  QUrl thumbnailUrl;
  /// Provider-supplied confidence score (0-100). Higher is better;
  /// candidates are sorted descending by this. Negative = unknown.
  int matchScore = -1;
};

/// Fully-detailed scrape result for a chosen ScrapeCandidate. Pre-
/// mapped onto Kartend's ItemMetadata typed columns (title /
/// description / etc.) plus a `customFields` payload for provider-
/// specific overflow. The media list is the union of every available
/// asset for the item; the result dialog filters down to what the
/// user actually wants to download.
struct ScrapedItem {
  QString title;
  QString description;
  QString genre;
  QString developer;
  QString publisher;
  QString releaseDate;
  QString contentRating;
  QString players;
  /// Negative = unset. Mirrors ItemMetadata::runtimeSeconds.
  int runtimeSeconds = -1;
  /// JSON array string suitable for ItemMetadata::tags ("[...]").
  QString tagsJson;
  /// Provider-specific fields the typed columns don't cover. Caller
  /// merges into ItemMetadata::customFields via parseCustomFields /
  /// serializeCustomFields.
  QHash<QString, QString> customFields;
  /// Origin tag persisted into ItemMetadata::source ("musicbrainz",
  /// "screenscraper", etc.).
  QString sourceProviderId;
  QList<MediaAsset> media;
};

/// Snapshot of the provider's per-account request quota, surfaced live
/// in the scrape dialog and used to halt the batch when the daily
/// allowance runs out. `valid` is false for providers that don't
/// expose a quota (everything except ScreenScraper today) and for a
/// default-constructed instance — consumers must check it before
/// rendering any field. The daily / KO (failed-lookup) counters mirror
/// ScreenScraper's `requeststoday` / `maxrequestsperday` and
/// `requestskotoday` / `maxrequestskoperday`; `resetAtUtc` is the next
/// 00:00 UTC, when SS rolls the per-day counters back to zero.
struct QuotaStatus {
  bool valid = false;
  int dailyUsed = 0;
  int dailyMax = 0;
  int koUsed = 0;
  int koMax = 0;
  QDateTime resetAtUtc;
};

} // namespace Scraper

// Required for queued cross-thread invocation of ScrapeWriteWorker::performWrite
// (Q_ARG copies the argument via Qt's metatype system). Declared here next to
// the type definition so every TU that can see ScrapedItem also sees the
// metatype declaration, avoiding "specialization after instantiation" errors.
Q_DECLARE_METATYPE(Scraper::ScrapedItem)
// EntityScrapeTarget rides in a CollectionJob and (later) queued signals when an
// entity scrape is dispatched, so it needs the metatype like ScrapedItem above
// (Kartend-ckepd.1).
Q_DECLARE_METATYPE(Scraper::EntityScrapeTarget)

#endif // SCRAPERTYPES_H
