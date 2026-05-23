#ifndef SMARTFILTER_H
#define SMARTFILTER_H

#include <QString>
#include <QStringList>

#include "errorutils.h"

class QJsonObject;

/// Serializable filter spec that drives a smart playlist's per-open
/// rebuild. A single `Filter` carries a discriminated `Kind` plus the
/// per-kind parameters; the SmartPlaylistEvaluator translates one of these
/// into a SQL query against the items table on the QueryManager worker
/// connection. The JSON shape produced by `toJson` is what gets stored in
/// `playlists.smart_filter` and what's emitted in v2 of the JSON export
/// format — keep it stable.
namespace SmartFilter {

enum class Kind {
  RecentlyLaunched, // params.limit
  TopPlayed,        // params.limit
  NeverPlayed,      // params.limit
  ByExtension,      // params.extensions
  HasArtwork,       // no params (all items with non-empty artwork_path)
  ByDateAdded,      // params.days (recency window — items added within last N days)
  Pinned,           // no params — items where item_metadata.is_pinned = 1
  Hidden,           // no params — items where item_metadata.is_hidden = 1
  ContinueLater,    // no params — items where item_metadata.continue_later = 1
  ByCollection,     // params.collectionUuid (single collection)
  ByTitleSearch,    // params.titleSearch (substring match on items.name)
  MissingArtwork,   // no params (items.artwork_path NULL or empty)
  Favorite,         // no params — items present in the reserved Favorites playlist
};

struct Filter {
  Kind kind = Kind::RecentlyLaunched;
  /// Result-set cap for the three counted criteria. Clamped to [1, 1000]
  /// downstream by the matching UsageStatsStore helper.
  int limit = 50;
  /// Lowercased extensions (without leading dot) for ByExtension. Empty
  /// list yields zero matches.
  QStringList extensions;
  /// Recency window in days for ByDateAdded. Items with a date_added
  /// epoch within the last N days (relative to query time) match. Clamped
  /// to [1, 3650] downstream so a stray hand-edit can't make the playlist
  /// scan the entire history of the universe.
  int days = 30;
  /// Collection uuid for the ByCollection kind. Empty means "no collection
  /// selected" — the evaluator returns no matches rather than every item,
  /// so an unsaved dialog state can't accidentally surface the whole library.
  QString collectionUuid;
  /// Needle for the ByTitleSearch kind. Compared via LIKE %?% against the
  /// items.name column. Empty means no matches (same rationale as above).
  QString titleSearch;
};

/// String tag used in the JSON payload's "kind" field. Stable across
/// versions so persisted filters survive renames of the C++ enum.
[[nodiscard]] QString kindToTag(Kind kind);

/// Parses a tag string back into the enum. Returns an error when the tag
/// is unknown so callers can flag forward-compatibility issues
/// (a smart playlist created on a newer build with a kind we don't
/// recognise yet).
[[nodiscard]] ErrorUtils::Result<Kind> tagToKind(const QString &tag);

/// Builds a JSON object: { "kind": "<tag>", "limit": N, "extensions": [...] }.
/// Fields irrelevant to the kind are still emitted with default values so
/// the schema stays predictable for tooling.
[[nodiscard]] QJsonObject toJson(const Filter &filter);

/// Inverse of `toJson`. Returns an error on missing/invalid kind tag or
/// malformed payload; default values otherwise.
[[nodiscard]] ErrorUtils::Result<Filter> fromJson(const QJsonObject &obj);

/// Convenience: serialize directly to/from a compact JSON string. Empty
/// string in `fromJsonString` returns an error so callers can distinguish
/// "no filter persisted" from "filter is corrupt".
[[nodiscard]] QString toJsonString(const Filter &filter);
[[nodiscard]] ErrorUtils::Result<Filter> fromJsonString(const QString &json);

/// Short user-facing label like "Recently launched (50)" or
/// "Has artwork". Used by the create dialog summary and the sidebar
/// tooltip — translated via QCoreApplication::translate.
[[nodiscard]] QString humanLabel(const Filter &filter);

} // namespace SmartFilter

#endif // SMARTFILTER_H
