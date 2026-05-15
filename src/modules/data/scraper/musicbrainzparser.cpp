// JSON parsers for MusicBrainz responses. Reference shapes:
//   Search:  https://musicbrainz.org/ws/2/release/?query=...&fmt=json
//   Detail:  https://musicbrainz.org/ws/2/release/<mbid>?inc=...&fmt=json
//   Cover:   https://coverartarchive.org/release/<mbid>/{front,back}-500.jpg
// Cover Art Archive URLs are well-known patterns — no JSON round-trip
// needed to construct the front/back cover links.
#include "musicbrainzparser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStringList>

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace MusicBrainzParser {

namespace {

constexpr const char *COVER_ART_BASE = "https://coverartarchive.org/release/";

QString joinArtistCredit(const QJsonArray &credits) {
  QStringList parts;
  for (const auto &v : credits) {
    const QJsonObject obj = v.toObject();
    const QString name = obj.value("name").toString();
    if (!name.isEmpty()) {
      parts.append(name);
    }
    const QString joinPhrase = obj.value("joinphrase").toString();
    if (!joinPhrase.isEmpty()) {
      // joinphrase already includes the surrounding spacing per MB API
      // convention ("feat. ", " & "). Replace any trailing space-comma
      // with the literal as-emitted form.
      parts.last() += joinPhrase;
    }
  }
  return parts.join(QString());
}

QString joinLabelInfo(const QJsonArray &labelInfo) {
  QStringList names;
  for (const auto &v : labelInfo) {
    const QJsonObject info = v.toObject();
    const QJsonObject label = info.value("label").toObject();
    const QString name = label.value("name").toString();
    if (!name.isEmpty() && !names.contains(name)) {
      names.append(name);
    }
  }
  return names.join(QStringLiteral(", "));
}

QString collectGenreTags(const QJsonObject &release) {
  // MB returns both `tags` (community user tags) and `genres` (curated
  // genre tags) on releases when ?inc=tags+genres is requested. We
  // prefer `genres` when present; fall back to top-voted `tags`.
  const QJsonArray genres = release.value("genres").toArray();
  if (!genres.isEmpty()) {
    QStringList names;
    for (const auto &g : genres) {
      const QString n = g.toObject().value("name").toString();
      if (!n.isEmpty() && !names.contains(n)) {
        names.append(n);
      }
    }
    if (!names.isEmpty()) {
      return names.join(QStringLiteral(", "));
    }
  }
  const QJsonArray tags = release.value("tags").toArray();
  // Sort descending by count by walking once and inserting into a
  // small ordered list. MB doesn't sort the tags array itself.
  struct Tag {
    QString name;
    int count = 0;
  };
  QList<Tag> all;
  all.reserve(tags.size());
  for (const auto &t : tags) {
    const QJsonObject obj = t.toObject();
    Tag tag;
    tag.name = obj.value("name").toString();
    tag.count = obj.value("count").toInt(0);
    if (!tag.name.isEmpty()) {
      all.append(tag);
    }
  }
  std::sort(all.begin(), all.end(), [](const Tag &a, const Tag &b) { return a.count > b.count; });
  QStringList top;
  for (int i = 0; i < std::min<int>(5, all.size()); ++i) {
    top.append(all[i].name);
  }
  return top.join(QStringLiteral(", "));
}

int countTracks(const QJsonArray &media) {
  int total = 0;
  for (const auto &m : media) {
    total += m.toObject().value("track-count").toInt(0);
  }
  return total;
}

} // namespace

ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> parseSearchResponse(const QByteArray &json) {
  QJsonParseError err;
  const auto doc = QJsonDocument::fromJson(json, &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "MusicBrainz search response is not valid JSON",
                               "MusicBrainzParser::parseSearchResponse")
        .withDetails(err.errorString());
  }
  const QJsonObject root = doc.object();
  const QJsonArray releases = root.value("releases").toArray();

  QList<Scraper::ScrapeCandidate> out;
  out.reserve(releases.size());
  for (const auto &v : releases) {
    const QJsonObject r = v.toObject();
    Scraper::ScrapeCandidate c;
    c.providerSpecificId = r.value("id").toString();
    if (c.providerSpecificId.isEmpty()) {
      // No MBID = unusable candidate; skip.
      continue;
    }
    const QString title = r.value("title").toString();
    const QString artist = joinArtistCredit(r.value("artist-credit").toArray());
    if (artist.isEmpty()) {
      c.displayName = title;
    } else {
      c.displayName = QStringLiteral("%1 — %2").arg(artist, title);
    }
    QStringList subtitleParts;
    const QString date = r.value("date").toString();
    if (!date.isEmpty()) {
      subtitleParts.append(date);
    }
    const QString country = r.value("country").toString();
    if (!country.isEmpty()) {
      subtitleParts.append(country);
    }
    const QJsonArray media = r.value("media").toArray();
    if (!media.isEmpty()) {
      const QString format = media.first().toObject().value("format").toString();
      if (!format.isEmpty()) {
        subtitleParts.append(format);
      }
    }
    c.subtitle = subtitleParts.join(QStringLiteral(" · "));
    c.matchScore = r.value("score").toInt(-1);
    // Default thumbnail = front cover via Cover Art Archive — MB itself
    // doesn't return an image URL in the search response, but the
    // archive serves a 250px thumbnail at a predictable URL when
    // coverage exists. Consumer falls back gracefully when 404.
    c.thumbnailUrl =
        QUrl(QStringLiteral("%1%2/front-250.jpg").arg(COVER_ART_BASE, c.providerSpecificId));
    out.append(c);
  }
  std::sort(out.begin(), out.end(),
            [](const Scraper::ScrapeCandidate &a, const Scraper::ScrapeCandidate &b) {
              return a.matchScore > b.matchScore;
            });
  return out;
}

ErrorUtils::Result<Scraper::ScrapedItem> parseDetailResponse(const QByteArray &json) {
  QJsonParseError err;
  const auto doc = QJsonDocument::fromJson(json, &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "MusicBrainz detail response is not valid JSON",
                               "MusicBrainzParser::parseDetailResponse")
        .withDetails(err.errorString());
  }
  const QJsonObject release = doc.object();
  Scraper::ScrapedItem item;
  item.sourceProviderId = QStringLiteral("musicbrainz");

  const QString title = release.value("title").toString();
  const QString artist = joinArtistCredit(release.value("artist-credit").toArray());
  item.title = artist.isEmpty() ? title : QStringLiteral("%1 — %2").arg(artist, title);
  item.publisher = joinLabelInfo(release.value("label-info").toArray());
  item.releaseDate = release.value("date").toString();
  item.genre = collectGenreTags(release);
  // MusicBrainz "annotation" field is the closest equivalent to a
  // user-facing description; its inclusion in the response requires
  // ?inc=annotation. Empty when not present.
  item.description = release.value("annotation").toString();

  const int trackCount = countTracks(release.value("media").toArray());
  if (trackCount > 0) {
    item.customFields.insert(QStringLiteral("track_count"), QString::number(trackCount));
  }
  // MBID round-trip: the user (or a future re-scrape) can use this to
  // resync without re-searching. Stored as a typed custom field
  // because ItemMetadata has no first-class slot for it.
  item.customFields.insert(QStringLiteral("musicbrainz_id"), release.value("id").toString());

  // Country / barcode / packaging — useful overflow for the customFields
  // panel. Skipped when absent so we don't litter empty rows.
  const QString country = release.value("country").toString();
  if (!country.isEmpty()) {
    item.customFields.insert(QStringLiteral("country"), country);
  }
  const QString barcode = release.value("barcode").toString();
  if (!barcode.isEmpty()) {
    item.customFields.insert(QStringLiteral("barcode"), barcode);
  }

  appendCoverArtUrls(item, release.value("id").toString());
  return item;
}

void appendCoverArtUrls(Scraper::ScrapedItem &item, const QString &mbid) {
  if (mbid.isEmpty()) {
    return;
  }
  // Cover Art Archive serves canonical filenames at predictable URLs.
  // 500px is a good thumbnail-quality default that's still usable as
  // a grid cover; the user can swap in the full-size variant
  // (no -500 suffix) post-import if they want max resolution.
  Scraper::MediaAsset front;
  front.type = QStringLiteral("front");
  front.label = QStringLiteral("Front cover");
  front.url = QUrl(QStringLiteral("%1%2/front-500.jpg").arg(COVER_ART_BASE, mbid));
  item.media.append(front);

  Scraper::MediaAsset back;
  back.type = QStringLiteral("back");
  back.label = QStringLiteral("Back cover");
  back.url = QUrl(QStringLiteral("%1%2/back-500.jpg").arg(COVER_ART_BASE, mbid));
  item.media.append(back);
}

} // namespace MusicBrainzParser
