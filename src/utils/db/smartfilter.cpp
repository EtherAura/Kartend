// Smart-playlist filter spec serialization. Lives in src/utils/db/ because
// its sole consumer is the QueryManager smart-playlist branch + the
// PlaylistManager smart_filter column round-trip; no widget or runtime
// dependencies.
#include "smartfilter.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace SmartFilter {

namespace {

constexpr const char *KEY_KIND = "kind";
constexpr const char *KEY_LIMIT = "limit";
constexpr const char *KEY_EXTENSIONS = "extensions";
constexpr const char *KEY_DAYS = "days";

constexpr const char *TAG_RECENT = "recently_launched";
constexpr const char *TAG_TOP = "top_played";
constexpr const char *TAG_NEVER = "never_played";
constexpr const char *TAG_BY_EXT = "by_extension";
constexpr const char *TAG_HAS_ARTWORK = "has_artwork";
constexpr const char *TAG_BY_DATE_ADDED = "by_date_added";
constexpr const char *TAG_PINNED = "pinned";
constexpr const char *TAG_HIDDEN = "hidden";
constexpr const char *TAG_CONTINUE_LATER = "continue_later";
constexpr const char *TAG_BY_COLLECTION = "by_collection";
constexpr const char *TAG_BY_TITLE_SEARCH = "by_title_search";
constexpr const char *TAG_MISSING_ARTWORK = "missing_artwork";
constexpr const char *TAG_FAVORITE = "favorite";

constexpr const char *KEY_COLLECTION_UUID = "collection_uuid";
constexpr const char *KEY_TITLE_SEARCH = "title_search";

} // namespace

QString kindToTag(Kind kind) {
  switch (kind) {
  case Kind::RecentlyLaunched:
    return QString::fromLatin1(TAG_RECENT);
  case Kind::TopPlayed:
    return QString::fromLatin1(TAG_TOP);
  case Kind::NeverPlayed:
    return QString::fromLatin1(TAG_NEVER);
  case Kind::ByExtension:
    return QString::fromLatin1(TAG_BY_EXT);
  case Kind::HasArtwork:
    return QString::fromLatin1(TAG_HAS_ARTWORK);
  case Kind::ByDateAdded:
    return QString::fromLatin1(TAG_BY_DATE_ADDED);
  case Kind::Pinned:
    return QString::fromLatin1(TAG_PINNED);
  case Kind::Hidden:
    return QString::fromLatin1(TAG_HIDDEN);
  case Kind::ContinueLater:
    return QString::fromLatin1(TAG_CONTINUE_LATER);
  case Kind::ByCollection:
    return QString::fromLatin1(TAG_BY_COLLECTION);
  case Kind::ByTitleSearch:
    return QString::fromLatin1(TAG_BY_TITLE_SEARCH);
  case Kind::MissingArtwork:
    return QString::fromLatin1(TAG_MISSING_ARTWORK);
  case Kind::Favorite:
    return QString::fromLatin1(TAG_FAVORITE);
  }
  // Unreachable in well-formed code; return a sentinel rather than
  // unhandled-switch UB so round-trip tests catch a missing case at the
  // first failure rather than later.
  return QStringLiteral("invalid");
}

ErrorUtils::Result<Kind> tagToKind(const QString &tag) {
  if (tag == QLatin1String(TAG_RECENT)) return Kind::RecentlyLaunched;
  if (tag == QLatin1String(TAG_TOP)) return Kind::TopPlayed;
  if (tag == QLatin1String(TAG_NEVER)) return Kind::NeverPlayed;
  if (tag == QLatin1String(TAG_BY_EXT)) return Kind::ByExtension;
  if (tag == QLatin1String(TAG_HAS_ARTWORK)) return Kind::HasArtwork;
  if (tag == QLatin1String(TAG_BY_DATE_ADDED)) return Kind::ByDateAdded;
  if (tag == QLatin1String(TAG_PINNED)) return Kind::Pinned;
  if (tag == QLatin1String(TAG_HIDDEN)) return Kind::Hidden;
  if (tag == QLatin1String(TAG_CONTINUE_LATER)) return Kind::ContinueLater;
  if (tag == QLatin1String(TAG_BY_COLLECTION)) return Kind::ByCollection;
  if (tag == QLatin1String(TAG_BY_TITLE_SEARCH)) return Kind::ByTitleSearch;
  if (tag == QLatin1String(TAG_MISSING_ARTWORK)) return Kind::MissingArtwork;
  if (tag == QLatin1String(TAG_FAVORITE)) return Kind::Favorite;
  return ErrorContext::error(ErrorCode::InvalidArgument, "Unknown smart-filter kind tag",
                             "SmartFilter::tagToKind")
      .withDetails(QStringLiteral("Tag: '%1'").arg(tag));
}

QJsonObject toJson(const Filter &filter) {
  QJsonObject o;
  o[KEY_KIND] = kindToTag(filter.kind);
  o[KEY_LIMIT] = filter.limit;
  o[KEY_DAYS] = filter.days;
  QJsonArray exts;
  for (const QString &e : filter.extensions) {
    exts.append(e);
  }
  o[KEY_EXTENSIONS] = exts;
  o[KEY_COLLECTION_UUID] = filter.collectionUuid;
  o[KEY_TITLE_SEARCH] = filter.titleSearch;
  return o;
}

ErrorUtils::Result<Filter> fromJson(const QJsonObject &obj) {
  if (!obj.contains(KEY_KIND)) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "smart_filter JSON missing 'kind' field",
                               "SmartFilter::fromJson");
  }
  auto kindResult = tagToKind(obj.value(KEY_KIND).toString());
  if (kindResult.isError()) {
    return kindResult.error();
  }
  Filter f;
  f.kind = kindResult.value();
  // Fall back to 50 when missing rather than zero — a zero limit would
  // make every counted-criterion playlist appear empty, which looks like
  // a bug to the user.
  f.limit = obj.value(KEY_LIMIT).toInt(50);
  // Days defaults to 30 for the same reason — a zero window would make
  // ByDateAdded match nothing instantly. 30 days is the canonical
  // "what's new" lookback in most music/video apps.
  f.days = obj.value(KEY_DAYS).toInt(30);
  if (obj.contains(KEY_EXTENSIONS)) {
    const QJsonArray arr = obj.value(KEY_EXTENSIONS).toArray();
    for (const auto &v : arr) {
      const QString s = v.toString().trimmed().toLower();
      if (!s.isEmpty()) {
        f.extensions.append(s.startsWith('.') ? s.mid(1) : s);
      }
    }
  }
  // Older smart_filter rows pre-date these fields — toString() returns an
  // empty QString for missing keys, which is also the "no value" sentinel
  // the evaluator checks (empty -> returns 0 matches), so the absence is
  // honest rather than producing a "matches everything" surprise.
  f.collectionUuid = obj.value(KEY_COLLECTION_UUID).toString();
  f.titleSearch = obj.value(KEY_TITLE_SEARCH).toString();
  return f;
}

QString toJsonString(const Filter &filter) {
  return QString::fromUtf8(QJsonDocument(toJson(filter)).toJson(QJsonDocument::Compact));
}

ErrorUtils::Result<Filter> fromJsonString(const QString &json) {
  if (json.trimmed().isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "smart_filter JSON is empty",
                               "SmartFilter::fromJsonString");
  }
  QJsonParseError err;
  const auto doc = QJsonDocument::fromJson(json.toUtf8(), &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "smart_filter JSON is malformed",
                               "SmartFilter::fromJsonString")
        .withDetails(err.errorString());
  }
  return fromJson(doc.object());
}

} // namespace SmartFilter
