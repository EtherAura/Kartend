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

QString tr(const char *s) {
  return QCoreApplication::translate("SmartFilter", s);
}

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

QString humanLabel(const Filter &filter) {
  switch (filter.kind) {
  case Kind::RecentlyLaunched:
    return tr("Recently launched (top %1)").arg(filter.limit);
  case Kind::TopPlayed:
    return tr("Most played (top %1)").arg(filter.limit);
  case Kind::NeverPlayed:
    return tr("Never launched (top %1)").arg(filter.limit);
  case Kind::ByExtension:
    return filter.extensions.isEmpty()
               ? tr("By extension (none)")
               : tr("By extension: %1").arg(filter.extensions.join(QStringLiteral(", ")));
  case Kind::HasArtwork:
    return tr("Has artwork");
  case Kind::ByDateAdded:
    return tr("Added in last %1 days").arg(filter.days);
  }
  return tr("Unknown filter");
}

} // namespace SmartFilter
