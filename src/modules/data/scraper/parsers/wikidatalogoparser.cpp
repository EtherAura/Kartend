// Wikidata logo lookup wire shape (Kartend-czna3). Pure functions only —
// see the header for the provider/parser split.
#include <algorithm>

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
  q.addQueryItem(QStringLiteral("limit"), QStringLiteral("7"));
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

ErrorUtils::Result<QList<SearchHit>> parseEntitySearchHits(const QByteArray &json) {
  auto root = rootObject(json, "WikidataLogoParser::parseEntitySearchHits");
  if (root.isError()) return root.error();
  const QJsonArray hits = root.value().value(QStringLiteral("search")).toArray();
  QList<SearchHit> out;
  for (const auto &h : hits) {
    const QJsonObject obj = h.toObject();
    SearchHit hit;
    hit.id = obj.value(QStringLiteral("id")).toString();
    hit.label = obj.value(QStringLiteral("label")).toString();
    hit.description = obj.value(QStringLiteral("description")).toString();
    hit.matchText =
        obj.value(QStringLiteral("match")).toObject().value(QStringLiteral("text")).toString();
    if (!hit.id.isEmpty()) out.append(hit);
  }
  return out;
}

QStringList searchQueryLadder(const QString &name) {
  const QString full = name.trimmed();
  QStringList queries;
  if (full.isEmpty()) return queries;
  queries.append(full);
  // Fragment separators seen in real library names ("Famicom - Nintendo
  // Entertainment System", "Mega Drive - Genesis"). Whole-word separators
  // only — a hyphenated single word is not compound.
  static const QStringList separators = {QStringLiteral(" - "), QStringLiteral(" – "),
                                         QStringLiteral(": "), QStringLiteral(" / ")};
  QStringList fragments{full};
  for (const QString &sep : separators) {
    QStringList next;
    for (const QString &f : fragments) next.append(f.split(sep));
    fragments = next;
  }
  for (const QString &fragment : fragments) {
    const QString clean = fragment.trimmed();
    if (clean.size() < 3) continue;
    if (queries.contains(clean, Qt::CaseInsensitive)) continue;
    queries.append(clean);
  }
  return queries;
}

QString pickEntityForCollection(const QList<SearchHit> &hits, const QString &collectionType,
                                const QString &query, bool preferCompany) {
  if (hits.isEmpty()) return {};
  static const QStringList gamesWords = {
      QStringLiteral("video game"), QStringLiteral("game console"), QStringLiteral("games console"),
      QStringLiteral("console"),    QStringLiteral("handheld"),     QStringLiteral("arcade")};
  static const QStringList videoWords = {QStringLiteral("film"), QStringLiteral("movie"),
                                         QStringLiteral("television"), QStringLiteral("studio"),
                                         QStringLiteral("media franchise")};
  static const QStringList musicWords = {
      QStringLiteral("band"),  QStringLiteral("musician"), QStringLiteral("record label"),
      QStringLiteral("music"), QStringLiteral("composer"), QStringLiteral("orchestra")};
  static const QStringList genericWords = {
      QStringLiteral("company"),      QStringLiteral("corporation"), QStringLiteral("conglomerate"),
      QStringLiteral("manufacturer"), QStringLiteral("brand"),       QStringLiteral("publisher")};
  // Entities that can never be a media collection's subject — a bare
  // eponym like "Sony" otherwise wins on exactness alone (live field
  // report: the Sony shell scraped as "male given name").
  static const QStringList junkWords = {QStringLiteral("given name"), QStringLiteral("family name"),
                                        QStringLiteral("surname"), QStringLiteral("disambiguation"),
                                        QStringLiteral("wikimedia")};

  const QString type = collectionType.toLower();
  QStringList media;
  if (type.contains(QLatin1String("game"))) {
    media = gamesWords;
  } else if (type.contains(QLatin1String("video")) || type.contains(QLatin1String("film"))) {
    media = videoWords;
  } else if (type.contains(QLatin1String("audio")) || type.contains(QLatin1String("music"))) {
    media = musicWords;
  } else {
    // Unknown / inherited-blank type (subcollections often leave it empty):
    // accept any media vocabulary rather than guessing wrong.
    media = gamesWords + videoWords + musicWords;
  }
  // A SHELL collection ("Sony" holding the PlayStations) is named after the
  // company, so company vocabulary outranks the media words — otherwise the
  // one console hit in the list wins and the shell scrapes as its own
  // child's platform. Leaf collections keep media-first ("Saturn" IS the
  // console, not Sega).
  const QStringList &primary = preferCompany ? genericWords : media;
  const QStringList &secondary = preferCompany ? media : genericWords;

  const auto matches = [](const SearchHit &hit, const QStringList &words) {
    const QString haystack = (hit.label + QLatin1Char(' ') + hit.description).toLower();
    for (const QString &w : words) {
      if (haystack.contains(w)) return true;
    }
    return false;
  };
  const QString wanted = query.trimmed();
  const QString wantedPrefix = wanted + QLatin1Char(' ');
  int bestScore = 0;
  QString bestId;
  for (const SearchHit &hit : hits) {
    if (matches(hit, junkWords)) continue;
    int score = 0;
    const bool exact =
        !wanted.isEmpty() && (hit.matchText.compare(wanted, Qt::CaseInsensitive) == 0 ||
                              hit.label.compare(wanted, Qt::CaseInsensitive) == 0);
    const bool prefixed =
        !wanted.isEmpty() && (hit.matchText.startsWith(wantedPrefix, Qt::CaseInsensitive) ||
                              hit.label.startsWith(wantedPrefix, Qt::CaseInsensitive));
    if (exact) {
      score += 10;
    } else if (prefixed) {
      score += 6;
    }
    if (matches(hit, primary)) {
      score += 5;
    } else if (matches(hit, secondary)) {
      score += 2;
    }
    if (score > bestScore) {
      bestScore = score;
      bestId = hit.id;
    }
  }
  return bestId;
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

QStringList EntityData::referencedEntityIds() const {
  QStringList ids;
  QStringList all{manufacturerId, countryId, developerId,   publisherId, genreId,
                  cpuId,          gpuId,     predecessorId, successorId};
  all.append(partOfIds);
  for (const QString &id : all) {
    if (!id.isEmpty() && !ids.contains(id)) ids.append(id);
  }
  return ids;
}

QUrl buildLabelsUrl(const QStringList &entityIds) {
  QStringList clean;
  for (const QString &id : entityIds) {
    if (!id.isEmpty() && !clean.contains(id)) clean.append(id);
  }
  if (clean.isEmpty()) return {};
  QUrl url(QString::fromLatin1(WIKIDATA_API));
  QUrlQuery q;
  q.addQueryItem(QStringLiteral("action"), QStringLiteral("wbgetentities"));
  q.addQueryItem(QStringLiteral("ids"), clean.join(QLatin1Char('|')));
  q.addQueryItem(QStringLiteral("props"), QStringLiteral("labels"));
  q.addQueryItem(QStringLiteral("languages"), QStringLiteral("en"));
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
  // Kartend-6i10t: the wider fact set. Entity references carry ids only —
  // the provider resolves labels for all of them in one batched hop.
  const auto refId = [&claims](const char *prop) {
    return firstClaimValue(claims, QLatin1String(prop))
        .toObject()
        .value(QStringLiteral("id"))
        .toString();
  };
  data.countryId = refId("P495"); // country of origin (works, franchises)
  if (data.countryId.isEmpty()) data.countryId = refId("P17"); // plain country
  data.developerId = refId("P178");
  data.publisherId = refId("P123");
  data.genreId = refId("P136");
  {
    // P856 official website — a plain URL string; keep only http(s) ones so
    // a vandalised claim can't smuggle an odd scheme into a clickable label.
    const QString site = firstClaimValue(claims, QStringLiteral("P856")).toString().trimmed();
    const QUrl parsed(site);
    if (parsed.isValid() &&
        (parsed.scheme() == QLatin1String("http") || parsed.scheme() == QLatin1String("https"))) {
      data.websiteUrl = site;
    }
  }
  // Kartend-5b5r1: the game-platform spec sheet (property ids verified
  // against live console entities). All entity references; labels resolve
  // in the provider's batched hop.
  data.cpuId = refId("P880");
  data.gpuId = refId("P2560");
  data.predecessorId = refId("P155");
  data.successorId = refId("P156");
  for (const auto &claim : claims.value(QStringLiteral("P361")).toArray()) {
    const QString id = claim.toObject()
                           .value(QStringLiteral("mainsnak"))
                           .toObject()
                           .value(QStringLiteral("datavalue"))
                           .toObject()
                           .value(QStringLiteral("value"))
                           .toObject()
                           .value(QStringLiteral("id"))
                           .toString();
    if (!id.isEmpty()) data.partOfIds.append(id);
  }
  {
    // P2664 units sold — a quantity ("+49100000"); strip the sign, keep
    // digits only so the display side can format it.
    QString amount = firstClaimValue(claims, QStringLiteral("P2664"))
                         .toObject()
                         .value(QStringLiteral("amount"))
                         .toString();
    amount.remove(QLatin1Char('+'));
    if (!amount.isEmpty() && amount.count(QLatin1Char('-')) == 0 &&
        std::all_of(amount.cbegin(), amount.cend(), [](QChar c) { return c.isDigit(); })) {
      data.unitsSold = amount;
    }
  }
  {
    // P577 publication date — multi-valued per region; the EARLIEST year is
    // the console's release year. P2669 closes the production span.
    const auto yearOfTime = [](const QString &time) -> QString {
      int i = 0;
      while (i < time.size() && (time[i] == QLatin1Char('+') || time[i] == QLatin1Char('-'))) ++i;
      QString year;
      while (i < time.size() && time[i].isDigit()) year.append(time[i++]);
      return year.size() == 4 ? year : QString();
    };
    for (const auto &claim : claims.value(QStringLiteral("P577")).toArray()) {
      const QString year = yearOfTime(claim.toObject()
                                          .value(QStringLiteral("mainsnak"))
                                          .toObject()
                                          .value(QStringLiteral("datavalue"))
                                          .toObject()
                                          .value(QStringLiteral("value"))
                                          .toObject()
                                          .value(QStringLiteral("time"))
                                          .toString());
      if (!year.isEmpty() && (data.publicationYear.isEmpty() || year < data.publicationYear)) {
        data.publicationYear = year;
      }
    }
    data.discontinuedYear = yearOfTime(firstClaimValue(claims, QStringLiteral("P2669"))
                                           .toObject()
                                           .value(QStringLiteral("time"))
                                           .toString());
  }
  {
    // P18 image — the console photograph; same path-safety validation as
    // the logo (the filename becomes a URL path segment).
    const QJsonValue value = firstClaimValue(claims, QStringLiteral("P18"));
    QString filename = value.isString() ? value.toString()
                                        : value.toObject().value(QStringLiteral("id")).toString();
    filename = filename.trimmed();
    if (filename.startsWith(QLatin1String("File:"), Qt::CaseInsensitive)) {
      filename = filename.mid(5).trimmed();
    }
    if (isSafeLogoFilename(filename)) data.photoFilename = filename;
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

ErrorUtils::Result<QHash<QString, QString>> parseEntityLabels(const QByteArray &json) {
  auto root = rootObject(json, "WikidataLogoParser::parseEntityLabels");
  if (root.isError()) return root.error();
  const QJsonObject entities = root.value().value(QStringLiteral("entities")).toObject();
  QHash<QString, QString> labels;
  for (auto it = entities.begin(); it != entities.end(); ++it) {
    const QString label = it.value()
                              .toObject()
                              .value(QStringLiteral("labels"))
                              .toObject()
                              .value(QStringLiteral("en"))
                              .toObject()
                              .value(QStringLiteral("value"))
                              .toString()
                              .trimmed();
    if (!label.isEmpty()) labels.insert(it.key(), label);
  }
  return labels;
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
