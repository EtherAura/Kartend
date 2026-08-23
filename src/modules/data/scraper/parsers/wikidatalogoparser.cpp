// Wikidata logo lookup wire shape (Kartend-czna3). Pure functions only —
// see the header for the provider/parser split.
#include "wikidatalogoparser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QUrlQuery>

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace WikidataLogoParser {

namespace {

constexpr const char *WIKIDATA_API = "https://www.wikidata.org/w/api.php";
constexpr const char *COMMONS_FILEPATH = "https://commons.wikimedia.org/wiki/Special:FilePath/";

ErrorUtils::Result<QJsonObject> rootObject(const QByteArray &json, const char *context) {
  QJsonParseError err;
  const auto doc = QJsonDocument::fromJson(json, &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Wikidata response is not valid JSON",
                               context)
        .withDetails(err.errorString());
  }
  return doc.object();
}

} // namespace

QUrl buildSearchUrl(const QString &name) {
  const QString trimmed = name.trimmed();
  if (trimmed.isEmpty()) return {};
  QUrl url(QString::fromLatin1(WIKIDATA_API));
  QUrlQuery q;
  q.addQueryItem(QStringLiteral("action"), QStringLiteral("wbsearchentities"));
  q.addQueryItem(QStringLiteral("search"), trimmed);
  q.addQueryItem(QStringLiteral("language"), QStringLiteral("en"));
  q.addQueryItem(QStringLiteral("type"), QStringLiteral("item"));
  q.addQueryItem(QStringLiteral("limit"), QStringLiteral("1"));
  q.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
  url.setQuery(q);
  return url;
}

QUrl buildClaimsUrl(const QString &entityId) {
  if (entityId.isEmpty()) return {};
  QUrl url(QString::fromLatin1(WIKIDATA_API));
  QUrlQuery q;
  q.addQueryItem(QStringLiteral("action"), QStringLiteral("wbgetclaims"));
  q.addQueryItem(QStringLiteral("entity"), entityId);
  q.addQueryItem(QStringLiteral("property"), QStringLiteral("P154"));
  q.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
  url.setQuery(q);
  return url;
}

QUrl buildLogoFileUrl(const QString &filename) {
  if (!isSafeLogoFilename(filename)) return {};
  // Commons canonicalises spaces to underscores; doing it here keeps the
  // request cacheable under one form. Percent-encoding via QUrl.
  QString canonical = filename;
  canonical.replace(QLatin1Char(' '), QLatin1Char('_'));
  return QUrl(QString::fromLatin1(COMMONS_FILEPATH) +
              QString::fromUtf8(QUrl::toPercentEncoding(canonical)));
}

bool isSafeLogoFilename(const QString &filename) {
  if (filename.isEmpty() || filename.size() > 255) return false;
  if (filename.contains(QLatin1Char('/')) || filename.contains(QLatin1Char('\\'))) return false;
  if (filename.contains(QLatin1String(".."))) return false;
  const int dot = filename.lastIndexOf(QLatin1Char('.'));
  if (dot <= 0 || dot == filename.size() - 1) return false; // needs "<base>.<ext>"
  return true;
}

ErrorUtils::Result<QString> parseEntitySearch(const QByteArray &json) {
  auto root = rootObject(json, "WikidataLogoParser::parseEntitySearch");
  if (root.isError()) return root.error();
  const QJsonArray hits = root.value().value(QStringLiteral("search")).toArray();
  if (hits.isEmpty()) return QString(); // no entity — benign, caller maps to not-found
  return hits.first().toObject().value(QStringLiteral("id")).toString();
}

ErrorUtils::Result<QString> parseLogoClaim(const QByteArray &json) {
  auto root = rootObject(json, "WikidataLogoParser::parseLogoClaim");
  if (root.isError()) return root.error();
  const QJsonArray claims = root.value()
                                .value(QStringLiteral("claims"))
                                .toObject()
                                .value(QStringLiteral("P154"))
                                .toArray();
  for (const auto &claim : claims) {
    const QJsonValue value = claim.toObject()
                                 .value(QStringLiteral("mainsnak"))
                                 .toObject()
                                 .value(QStringLiteral("datavalue"))
                                 .toObject()
                                 .value(QStringLiteral("value"));
    // P154 is a Commons-media property: the value is the bare filename
    // string. Tolerate an object shape defensively.
    QString filename = value.isString() ? value.toString()
                                        : value.toObject().value(QStringLiteral("id")).toString();
    filename = filename.trimmed();
    if (filename.startsWith(QLatin1String("File:"), Qt::CaseInsensitive)) {
      filename = filename.mid(5).trimmed();
    }
    if (isSafeLogoFilename(filename)) return filename;
  }
  return QString(); // entity exists but has no usable logo claim — benign
}

// ── Entity DATA additions (Kartend-445su) ────────────────────────────────

namespace {

/// First claim value walker shared by the entity-data fields. Returns the
/// raw datavalue.value for the first claim of @p property, or an undefined
/// QJsonValue when the entity has none.
QJsonValue firstClaimValue(const QJsonObject &claims, const QString &property) {
  const QJsonArray arr = claims.value(property).toArray();
  if (arr.isEmpty()) return {};
  return arr.first()
      .toObject()
      .value(QStringLiteral("mainsnak"))
      .toObject()
      .value(QStringLiteral("datavalue"))
      .toObject()
      .value(QStringLiteral("value"));
}

} // namespace

QUrl buildEntityDataUrl(const QString &entityId) {
  if (entityId.isEmpty()) return {};
  QUrl url(QString::fromLatin1(WIKIDATA_API));
  QUrlQuery q;
  q.addQueryItem(QStringLiteral("action"), QStringLiteral("wbgetentities"));
  q.addQueryItem(QStringLiteral("ids"), entityId);
  q.addQueryItem(QStringLiteral("props"), QStringLiteral("claims|sitelinks|descriptions"));
  q.addQueryItem(QStringLiteral("languages"), QStringLiteral("en"));
  q.addQueryItem(QStringLiteral("sitefilter"), QStringLiteral("enwiki"));
  q.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
  url.setQuery(q);
  return url;
}

QUrl buildLabelUrl(const QString &entityId) {
  if (entityId.isEmpty()) return {};
  QUrl url(QString::fromLatin1(WIKIDATA_API));
  QUrlQuery q;
  q.addQueryItem(QStringLiteral("action"), QStringLiteral("wbgetentities"));
  q.addQueryItem(QStringLiteral("ids"), entityId);
  q.addQueryItem(QStringLiteral("props"), QStringLiteral("labels"));
  q.addQueryItem(QStringLiteral("languages"), QStringLiteral("en"));
  q.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
  url.setQuery(q);
  return url;
}

QUrl buildWikipediaSummaryUrl(const QString &title) {
  const QString trimmed = title.trimmed();
  if (trimmed.isEmpty()) return {};
  // Path-encode the title (spaces → underscores per Wikipedia convention;
  // QUrl percent-encodes the rest). The REST endpoint answers plain JSON.
  QString path = trimmed;
  path.replace(QLatin1Char(' '), QLatin1Char('_'));
  QUrl url(QStringLiteral("https://en.wikipedia.org/api/rest_v1/page/summary/"));
  url.setPath(url.path() + path);
  return url;
}

ErrorUtils::Result<EntityData> parseEntityData(const QByteArray &json) {
  auto root = rootObject(json, "WikidataLogoParser::parseEntityData");
  if (root.isError()) return root.error();
  const QJsonObject entities = root.value().value(QStringLiteral("entities")).toObject();
  if (entities.isEmpty()) {
    return EntityData{}; // no entity in the response — benign sparse result
  }
  const QJsonObject entity = entities.begin().value().toObject();
  const QJsonObject claims = entity.value(QStringLiteral("claims")).toObject();

  EntityData data;
  // P154 logo — same value shape and the same safety validation as
  // parseLogoClaim (the filename becomes a URL path segment).
  {
    const QJsonValue value = firstClaimValue(claims, QStringLiteral("P154"));
    QString filename = value.isString() ? value.toString()
                                        : value.toObject().value(QStringLiteral("id")).toString();
    filename = filename.trimmed();
    if (filename.startsWith(QLatin1String("File:"), Qt::CaseInsensitive)) {
      filename = filename.mid(5).trimmed();
    }
    if (isSafeLogoFilename(filename)) data.logoFilename = filename;
  }
  // P176 manufacturer — an entity reference; the label needs its own hop.
  data.manufacturerId = firstClaimValue(claims, QStringLiteral("P176"))
                            .toObject()
                            .value(QStringLiteral("id"))
                            .toString();
  // P571 inception — a Wikidata time value ("+1988-10-29T00:00:00Z").
  // Wikidata is inconsistent about precision, so only the year is trusted.
  {
    const QString time = firstClaimValue(claims, QStringLiteral("P571"))
                             .toObject()
                             .value(QStringLiteral("time"))
                             .toString();
    // "+YYYY-…" / "-YYYY-…": take the sign-stripped leading year digits.
    int i = 0;
    while (i < time.size() && (time[i] == QLatin1Char('+') || time[i] == QLatin1Char('-'))) ++i;
    QString year;
    while (i < time.size() && time[i].isDigit()) year.append(time[i++]);
    if (year.size() == 4) data.inceptionYear = year;
  }
  data.description = entity.value(QStringLiteral("descriptions"))
                         .toObject()
                         .value(QStringLiteral("en"))
                         .toObject()
                         .value(QStringLiteral("value"))
                         .toString()
                         .trimmed();
  data.enwikiTitle = entity.value(QStringLiteral("sitelinks"))
                         .toObject()
                         .value(QStringLiteral("enwiki"))
                         .toObject()
                         .value(QStringLiteral("title"))
                         .toString()
                         .trimmed();
  return data;
}

ErrorUtils::Result<QString> parseEntityLabel(const QByteArray &json) {
  auto root = rootObject(json, "WikidataLogoParser::parseEntityLabel");
  if (root.isError()) return root.error();
  const QJsonObject entities = root.value().value(QStringLiteral("entities")).toObject();
  if (entities.isEmpty()) return QString();
  return entities.begin()
      .value()
      .toObject()
      .value(QStringLiteral("labels"))
      .toObject()
      .value(QStringLiteral("en"))
      .toObject()
      .value(QStringLiteral("value"))
      .toString()
      .trimmed();
}

ErrorUtils::Result<QString> parseWikipediaSummary(const QByteArray &json) {
  auto root = rootObject(json, "WikidataLogoParser::parseWikipediaSummary");
  if (root.isError()) return root.error();
  // A disambiguation page's extract is a list of unrelated meanings — worse
  // than no description. The REST endpoint types it explicitly.
  if (root.value().value(QStringLiteral("type")).toString() == QLatin1String("disambiguation")) {
    return QString();
  }
  return root.value().value(QStringLiteral("extract")).toString().trimmed();
}

} // namespace WikidataLogoParser
