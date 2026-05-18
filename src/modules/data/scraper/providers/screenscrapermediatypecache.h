#ifndef SCREENSCRAPERMEDIATYPECACHE_H
#define SCREENSCRAPERMEDIATYPECACHE_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include "errorutils.h"

/// Runtime cache for ScreenScraper.fr's mediasJeuListe.php asset-type
/// catalog.
///
/// SS adds new media types every year (bezel-16-9, mixrbv1/2,
/// box-texture, etc.) and the parser used to hardcode the
/// canonical-tag mapping. Pulling the catalog from the live API at
/// runtime means new SS types appear in the scrape dialog without a
/// Kartend release. Mirrors the systemesListe cache module — pure
/// JSON parser + on-disk read/write + age check, with the network
/// fetch living in the provider so this module stays testable
/// without a QApplication.
namespace ScreenScraperMediaTypeCache {

/// One entry from SS's media-type catalog. SS exposes the catalog
/// keyed on `type` (`box-2D`, `screenshot`, `bezel-16-9`, ...);
/// `category` buckets the type ('jeu' = per-game asset, 'support' =
/// per-support, etc.) and `fileFormat` is the default download
/// extension. Localised display name is picked at parse time from
/// the per-locale entries SS supplies.
struct MediaType {
  /// Canonical SS tag, lowercased (`box-2d`, `screenshot`,
  /// `bezel-16-9`, ...). The parser uses this to bucket assets.
  QString type;
  /// Human-readable label (English when available, else the first
  /// non-empty locale).
  QString displayName;
  /// SS category bucket: 'jeu' (per-game), 'group' (per-genre/family),
  /// 'compagnie' (per-publisher/developer), 'support' (per-medium).
  /// Drives the asset's MediaScope at parse time without us having
  /// to grep the URL path.
  QString category;
  /// Default extension SS serves (png/jpg/mp4/pdf). Used as the
  /// fallback when the URL doesn't carry a recognisable suffix.
  QString fileFormat;
  /// True when SS may serve different files per region (so the parser
  /// should pick the user's preferred region rather than collapsing).
  bool multiRegions = false;
  /// True when SS may serve different files per support (disc/cart),
  /// same caveat as multiRegions.
  bool multiSupports = false;
};

/// Cache TTL — 30 days. Same rationale as the systems cache: the
/// catalog grows infrequently and the existing entries don't change
/// shape, so a long TTL minimises noise without missing meaningful
/// updates. Users can force a refresh by deleting the file.
constexpr int CACHE_TTL_DAYS = 30;

/// Canonical on-disk path for the cached catalog. Lives under
/// `QStandardPaths::CacheLocation/kartend/screenscraper-mediatypes.json`.
[[nodiscard]] QString defaultCachePath();

/// Parse SS's mediasJeuListe.php response (or our own cached JSON,
/// same shape) into the typed MediaType list. Defensive about field
/// presence — SS occasionally drops fields on rare types; missing
/// entries default to empty/false, never erroring out the whole
/// catalog. Empty `medias` array is a successful empty list, not
/// an error.
[[nodiscard]] ErrorUtils::Result<QList<MediaType>> parseMediaTypesResponse(const QByteArray &json);

/// Read the cached catalog from disk. Returns empty (non-error) when
/// the file doesn't exist; returns an error only on actual parse
/// failures so callers can distinguish "not cached yet" from "cache
/// got corrupted".
[[nodiscard]] ErrorUtils::Result<QList<MediaType>> loadCachedMediaTypes(const QString &filePath);

/// Write the catalog to disk. Creates intermediate directories.
/// Returns false (with a logged error) when the write fails — the
/// in-memory catalog is still usable for the current session.
[[nodiscard]] bool saveMediaTypes(const QString &filePath, const QList<MediaType> &mediaTypes);

/// True when the cache file at `filePath` is older than CACHE_TTL_DAYS
/// or missing.
[[nodiscard]] bool isCacheStale(const QString &filePath);

/// Look up a media type by its canonical tag. Returns nullptr when
/// no entry matches. The lookup is case-insensitive (SS's `type`
/// field is mixed case in some entries — `box-2D` vs `bezel-16-9`).
[[nodiscard]] const MediaType *find(const QList<MediaType> &mediaTypes, const QString &type);

} // namespace ScreenScraperMediaTypeCache

#endif // SCREENSCRAPERMEDIATYPECACHE_H
