#ifndef SMARTFILTER_H
#define SMARTFILTER_H

#include <QList>
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

/// How a FilterSet's rules combine (Kartend-r5dbe).
enum class MatchMode {
  All, ///< Intersection — an item must satisfy every rule.
  Any, ///< Union — an item must satisfy at least one rule.
};

/// One or more rules plus how they combine. A smart playlist held exactly
/// one `Filter` until composition landed; a FilterSet with a single rule is
/// that same playlist, and serialises to byte-identical JSON, so nothing
/// already on disk changes shape until the user actually adds a rule.
struct FilterSet {
  MatchMode match = MatchMode::All;
  /// Evaluated in order. Never empty after a successful parse — an empty
  /// `rules` array is rejected rather than silently matching everything.
  QList<Filter> rules;
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

/// FilterSet round-trip. Distinct names rather than overloads because these
/// differ from the single-Filter pair only in return type.
///
/// The wire format is a superset of the single-filter one: a set of one rule
/// emits exactly the legacy object, and a set of several ALSO mirrors its
/// first rule into the legacy top-level fields before adding "match" and
/// "rules". That mirroring is what lets an older build — and the call sites
/// that still parse a single Filter to validate a spec — read a multi-rule
/// playlist as its first rule instead of failing outright.
[[nodiscard]] QJsonObject setToJson(const FilterSet &set);
[[nodiscard]] ErrorUtils::Result<FilterSet> setFromJson(const QJsonObject &obj);
[[nodiscard]] QString setToJsonString(const FilterSet &set);
[[nodiscard]] ErrorUtils::Result<FilterSet> setFromJsonString(const QString &json);

} // namespace SmartFilter

#endif // SMARTFILTER_H
