#include "steamstoreparser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTextDocumentFragment>

#include "steamappinfo.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace SteamStoreParser {

namespace {

/// Store descriptions arrive as HTML fragments (entities, <br>, markup).
/// The sidebar renders plain text, so flatten here.
QString htmlToPlainText(const QString &html) {
  if (html.isEmpty()) {
    return {};
  }
  return QTextDocumentFragment::fromHtml(html).toPlainText().trimmed();
}

QString joinedStrings(const QJsonArray &array) {
  QStringList parts;
  for (const auto &value : array) {
    const QString entry = value.toString().trimmed();
    if (!entry.isEmpty()) {
      parts.append(entry);
    }
  }
  return parts.join(QStringLiteral(", "));
}

/// Description labels of {id, description} rows joined ", ".
QString joinedDescriptions(const QJsonArray &array) {
  QStringList parts;
  for (const auto &value : array) {
    const QString entry =
        value.toObject().value(QStringLiteral("description")).toString().trimmed();
    if (!entry.isEmpty() && !parts.contains(entry)) {
      parts.append(entry);
    }
  }
  return parts.join(QStringLiteral(", "));
}

void appendImageAsset(QList<Scraper::MediaAsset> &media, const QString &type, const QString &label,
                      const QString &url) {
  if (url.isEmpty()) {
    return;
  }
  Scraper::MediaAsset asset;
  asset.type = type;
  asset.label = label;
  asset.url = QUrl(url);
  media.append(asset);
}

} // namespace

auto parseStoreSearch(const QByteArray &body)
    -> ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> {
  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    return ErrorContext::error(ErrorCode::NetworkRequestFailed,
                               "Steam store search response is not valid JSON",
                               "SteamStoreParser::parseStoreSearch")
        .withDetails(parseError.errorString());
  }
  QList<Scraper::ScrapeCandidate> candidates;
  const QJsonArray items = doc.object().value(QStringLiteral("items")).toArray();
  for (const auto &value : items) {
    const QJsonObject item = value.toObject();
    Scraper::ScrapeCandidate candidate;
    candidate.displayName = item.value(QStringLiteral("name")).toString();
    // id arrives as a JSON number.
    const qint64 appId = item.value(QStringLiteral("id")).toVariant().toLongLong();
    if (candidate.displayName.isEmpty() || appId <= 0) {
      continue;
    }
    candidate.providerSpecificId = QString::number(appId);
    candidate.subtitle = QStringLiteral("Steam app %1").arg(appId);
    candidate.thumbnailUrl = QUrl(item.value(QStringLiteral("tiny_image")).toString());
    candidates.append(candidate);
  }
  return candidates;
}

auto parseAppDetails(const QByteArray &body, const QString &appId)
    -> ErrorUtils::Result<Scraper::ScrapedItem> {
  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    return ErrorContext::error(ErrorCode::NetworkRequestFailed,
                               "Steam appdetails response is not valid JSON",
                               "SteamStoreParser::parseAppDetails")
        .withDetails(parseError.errorString());
  }
  const QJsonObject wrapper = doc.object().value(appId).toObject();
  if (!wrapper.value(QStringLiteral("success")).toBool(false)) {
    // Delisted/region-locked/unknown apps come back HTTP 200 with
    // success:false — a routine not-found, not an error.
    return ErrorContext::error(ErrorCode::RemoteResourceNotFound,
                               "Steam store has no entry for this app",
                               "SteamStoreParser::parseAppDetails")
        .withDetails(appId);
  }
  const QJsonObject data = wrapper.value(QStringLiteral("data")).toObject();

  Scraper::ScrapedItem item;
  item.sourceProviderId = QStringLiteral("steam");
  item.title = data.value(QStringLiteral("name")).toString();
  item.description = htmlToPlainText(data.value(QStringLiteral("short_description")).toString());
  item.genre = joinedDescriptions(data.value(QStringLiteral("genres")).toArray());
  item.developer = joinedStrings(data.value(QStringLiteral("developers")).toArray());
  item.publisher = joinedStrings(data.value(QStringLiteral("publishers")).toArray());
  item.releaseDate = data.value(QStringLiteral("release_date"))
                         .toObject()
                         .value(QStringLiteral("date"))
                         .toString();
  // required_age is a number for most entries but a string for some.
  const int requiredAge = data.value(QStringLiteral("required_age")).toVariant().toInt();
  if (requiredAge > 0) {
    item.contentRating = QStringLiteral("%1+").arg(requiredAge);
  }
  // categories[] carries the same numeric ids appinfo's category_NN flags
  // use, so the local taxonomy mapper produces an identical players line.
  QList<int> categoryIds;
  const QJsonArray categories = data.value(QStringLiteral("categories")).toArray();
  for (const auto &value : categories) {
    const int id = value.toObject().value(QStringLiteral("id")).toVariant().toInt();
    if (id > 0) {
      categoryIds.append(id);
    }
  }
  item.players = SteamAppInfo::playersDescription(categoryIds);
  const int metacritic = data.value(QStringLiteral("metacritic"))
                             .toObject()
                             .value(QStringLiteral("score"))
                             .toVariant()
                             .toInt();
  if (metacritic > 0) {
    item.customFields.insert(QStringLiteral("Metacritic"), QString::number(metacritic));
  }

  // Media. Types stay within mediatypecheckboxbuilder's curated key set:
  // front / screenshot / background / video.
  appendImageAsset(item.media, QStringLiteral("front"), QStringLiteral("Header capsule"),
                   data.value(QStringLiteral("header_image")).toString());
  const QJsonArray screenshots = data.value(QStringLiteral("screenshots")).toArray();
  if (!screenshots.isEmpty()) {
    // One per type: writeMediaFiles names files {baseName}.{ext} per
    // subdir, so additional screenshots would overwrite the first.
    appendImageAsset(item.media, QStringLiteral("screenshot"), QStringLiteral("Screenshot"),
                     screenshots.first().toObject().value(QStringLiteral("path_full")).toString());
  }
  QString background = data.value(QStringLiteral("background_raw")).toString();
  if (background.isEmpty()) {
    background = data.value(QStringLiteral("background")).toString();
  }
  appendImageAsset(item.media, QStringLiteral("background"), QStringLiteral("Store background"),
                   background);
  const QJsonArray movies = data.value(QStringLiteral("movies")).toArray();
  if (!movies.isEmpty()) {
    const QJsonObject movie = movies.first().toObject();
    // Steam migrated trailers to adaptive streaming: current payloads carry
    // dash_av1 / dash_h264 / hls_h264 manifest URLs and NO `mp4` object, so
    // reading only mp4.* silently yielded no trailer. Manifests aren't a
    // playable single file for the video-preview slot, but the legacy
    // per-movie progressive mp4 is still served, so derive it from the movie
    // id. Older payloads that still carry mp4.* keep priority.
    const QJsonObject mp4 = movie.value(QStringLiteral("mp4")).toObject();
    QString movieUrl = mp4.value(QStringLiteral("max")).toString();
    if (movieUrl.isEmpty()) {
      movieUrl = mp4.value(QStringLiteral("480")).toString();
    }
    if (movieUrl.isEmpty()) {
      const qint64 movieId = movie.value(QStringLiteral("id")).toVariant().toLongLong();
      if (movieId > 0) {
        movieUrl = QStringLiteral("https://video.akamai.steamstatic.com/store_trailers/%1/"
                                  "movie480.mp4")
                       .arg(movieId);
      }
    }
    if (!movieUrl.isEmpty()) {
      Scraper::MediaAsset trailer;
      trailer.type = QStringLiteral("video");
      trailer.label = movie.value(QStringLiteral("name")).toString();
      if (trailer.label.isEmpty()) {
        trailer.label = QStringLiteral("Trailer");
      }
      trailer.url = QUrl(movieUrl);
      item.media.append(trailer);
    }
  }
  return item;
}

} // namespace SteamStoreParser
