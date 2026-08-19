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

} // namespace WikidataLogoParser
