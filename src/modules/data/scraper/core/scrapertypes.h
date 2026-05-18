#ifndef SCRAPERTYPES_H
#define SCRAPERTYPES_H

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

/// Where a piece of media lives in the provider's data model — used by
/// the persistence layer to route group/company-scoped art into a
/// `_shared/` directory instead of duplicating it per game. Drives
/// both the on-disk path (per-game vs shared) and the dialog's
/// pre-fetch dedup that lets a second game with the same `groupid`
/// skip the network entirely.
enum class MediaScope {
  Game,    ///< Per-game asset — ScreenScraper mediaJeu.php and friends.
  Group,   ///< Per-genre/theme/family/style — ScreenScraper mediaGroup.php.
  Company, ///< Per-publisher/developer — ScreenScraper mediaCompagnie.php.
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

} // namespace Scraper

// Required for queued cross-thread invocation of ScrapeWriteWorker::performWrite
// (Q_ARG copies the argument via Qt's metatype system). Declared here next to
// the type definition so every TU that can see ScrapedItem also sees the
// metatype declaration, avoiding "specialization after instantiation" errors.
Q_DECLARE_METATYPE(Scraper::ScrapedItem)

#endif // SCRAPERTYPES_H
