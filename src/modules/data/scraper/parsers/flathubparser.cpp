#include "flathubparser.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTextDocumentFragment>
#include <QTimeZone>

#include "parserlimits.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace FlathubParser {

namespace {

/// AppStream descriptions arrive as HTML fragments (<p>, <ul>, entities).
/// The sidebar renders plain text, so flatten here — same treatment as the
/// Steam store parser.
QString htmlToPlainText(const QString &html) {
  if (html.isEmpty()) {
    return {};
  }
  return QTextDocumentFragment::fromHtml(html).toPlainText().trimmed();
}

/// categories[] joined ", ", minus the redundant "Game" umbrella entry —
/// every entry in a games collection carries it, so as a genre it says
/// nothing ("Game, StrategyGame" → "StrategyGame").
QString genreFromCategories(const QJsonArray &categories) {
  ScraperParsers::BoundedUniqueStrings parts;
  for (const auto &value : categories) {
    const QString entry = value.toString().trimmed();
    if (entry.compare(QStringLiteral("Game"), Qt::CaseInsensitive) == 0) {
      continue;
    }
    if (!parts.add(entry)) {
      break; // sink full — stop walking an array the response sized
    }
  }
  return parts.join(QStringLiteral(", "));
}

/// releases[] carries {version, timestamp} newest-first; the first entry's
/// unix timestamp becomes an ISO date. Flathub occasionally serves the
/// timestamp as a string, so read it through QVariant.
QString releaseDateFromReleases(const QJsonArray &releases) {
  if (releases.isEmpty()) {
    return {};
  }
  const qint64 timestamp =
      releases.first().toObject().value(QStringLiteral("timestamp")).toVariant().toLongLong();
  if (timestamp <= 0) {
    return {};
  }
  return QDateTime::fromSecsSinceEpoch(timestamp, QTimeZone::utc()).date().toString(Qt::ISODate);
}

} // namespace

auto parseAppstream(const QByteArray &body, const QString &appId)
    -> ErrorUtils::Result<Scraper::ScrapedItem> {
  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
  if (parseError.error != QJsonParseError::NoError) {
    return ErrorContext::error(ErrorCode::NetworkRequestFailed,
                               "Flathub appstream response is not valid JSON",
                               "FlathubParser::parseAppstream")
        .withDetails(parseError.errorString());
  }
  // Unknown ids normally 404 before this parser runs; a 200 with a JSON
  // null / non-object body (as the API answers some delisted refs) is the
  // in-band equivalent — a routine not-found, not an error.
  if (!doc.isObject() || doc.object().isEmpty()) {
    return ErrorContext::error(ErrorCode::RemoteResourceNotFound,
                               "Flathub has no appstream entry for this app",
                               "FlathubParser::parseAppstream")
        .withDetails(appId);
  }
  const QJsonObject data = doc.object();

  Scraper::ScrapedItem item;
  item.sourceProviderId = QStringLiteral("flathub");
  item.title = data.value(QStringLiteral("name")).toString().trimmed();
  item.description = htmlToPlainText(data.value(QStringLiteral("description")).toString());
  if (item.description.isEmpty()) {
    item.description = data.value(QStringLiteral("summary")).toString().trimmed();
  }
  item.developer = data.value(QStringLiteral("developer_name")).toString().trimmed();
  item.genre = genreFromCategories(data.value(QStringLiteral("categories")).toArray());
  item.releaseDate = releaseDateFromReleases(data.value(QStringLiteral("releases")).toArray());
  const QString license = data.value(QStringLiteral("project_license")).toString().trimmed();
  if (!license.isEmpty()) {
    item.customFields.insert(QStringLiteral("License"), license);
  }
  return item;
}

} // namespace FlathubParser
