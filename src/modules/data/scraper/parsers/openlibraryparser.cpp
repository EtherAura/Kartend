// Open Library JSON parsers + ISBN-from-filename helper. Pure data
// shaping; the provider layer wires these to the HttpClient.
#include "openlibraryparser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStringList>

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace OpenLibraryParser {

namespace {

constexpr const char *COVER_BASE = "https://covers.openlibrary.org/b/id/";

// Open Library work keys are `OL<digits>W`. A `key` that fails this shape
// (after the /works/ strip) is untrusted junk that must never be interpolated
// into the detail-fetch URL path — a `../`, `?`, or `#` there would redirect
// the follow-up request to an attacker-influenced path (Kartend-tjyh).
bool isValidWorkKey(const QString &key) {
  static const QRegularExpression re(
      QRegularExpression::anchoredPattern(QStringLiteral("OL[0-9]+W")));
  return re.match(key).hasMatch();
}

QString joinStringArray(const QJsonArray &arr, const QString &sep = QStringLiteral(", ")) {
  QStringList parts;
  for (const auto &v : arr) {
    const QString s = v.toString().trimmed();
    if (!s.isEmpty() && !parts.contains(s)) {
      parts.append(s);
    }
  }
  return parts.join(sep);
}

/// Open Library's `description` field shape varies — sometimes a flat
/// string, sometimes `{"type":"...","value":"..."}`. Normalise to the
/// inner string.
QString readDescription(const QJsonValue &v) {
  if (v.isString()) {
    return v.toString();
  }
  if (v.isObject()) {
    return v.toObject().value("value").toString();
  }
  return {};
}

} // namespace

ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> parseSearchResponse(const QByteArray &json) {
  QJsonParseError err;
  const auto doc = QJsonDocument::fromJson(json, &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "Open Library search response is not valid JSON",
                               "OpenLibraryParser::parseSearchResponse")
        .withDetails(err.errorString());
  }
  const QJsonArray docs = doc.object().value("docs").toArray();

  QList<Scraper::ScrapeCandidate> out;
  out.reserve(docs.size());
  for (const auto &v : docs) {
    const QJsonObject d = v.toObject();
    Scraper::ScrapeCandidate c;
    // Open Library returns work keys like "/works/OL12345W". Strip
    // the prefix for our id so the detail-fetch URL builder can
    // re-prefix uniformly.
    QString key = d.value("key").toString();
    if (key.startsWith(QStringLiteral("/works/"))) {
      key = key.mid(7);
    }
    c.providerSpecificId = key;
    if (!isValidWorkKey(c.providerSpecificId)) {
      // Empty or malformed work key = unusable candidate (and an unsafe URL
      // path component); skip it (Kartend-tjyh).
      continue;
    }
    const QString title = d.value("title").toString();
    const QString author = joinStringArray(d.value("author_name").toArray());
    if (author.isEmpty()) {
      c.displayName = title;
    } else {
      c.displayName = QStringLiteral("%1 — %2").arg(author, title);
    }
    QStringList sub;
    const int year = d.value("first_publish_year").toInt(0);
    if (year > 0) {
      sub.append(QString::number(year));
    }
    const QString publisher = joinStringArray(d.value("publisher").toArray());
    if (!publisher.isEmpty()) {
      // Cap publisher at first one for the subtitle so the row stays
      // scannable; the full list is preserved in the detail response.
      const QString first = publisher.split(',').first().trimmed();
      if (!first.isEmpty()) {
        sub.append(first);
      }
    }
    c.subtitle = sub.join(QStringLiteral(" · "));
    // Open Library doesn't expose a per-row match score; leave at -1
    // (unknown) so the dialog renders nothing for it. Their docs
    // array is already sorted by relevance.
    c.matchScore = -1;
    const qint64 coverId = static_cast<qint64>(d.value("cover_i").toDouble(-1));
    if (coverId > 0) {
      c.thumbnailUrl =
          QUrl(QStringLiteral("%1%2-S.jpg").arg(QString::fromLatin1(COVER_BASE)).arg(coverId));
    }
    out.append(c);
  }
  return out;
}

ErrorUtils::Result<Scraper::ScrapedItem> parseDetailResponse(const QByteArray &json,
                                                             const QString &workKey) {
  QJsonParseError err;
  const auto doc = QJsonDocument::fromJson(json, &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "Open Library detail response is not valid JSON",
                               "OpenLibraryParser::parseDetailResponse")
        .withDetails(err.errorString());
  }
  const QJsonObject work = doc.object();
  Scraper::ScrapedItem item;
  item.sourceProviderId = QStringLiteral("openlibrary");

  item.title = work.value("title").toString();
  item.description = readDescription(work.value("description"));
  // Subjects on the work response are roughly the genre tags;
  // Open Library has no separate "genre" field on works.
  item.genre = joinStringArray(work.value("subjects").toArray());

  // The work-level response doesn't carry author names directly — it
  // points at /authors/OL.../json. We persist the author key as a
  // custom field instead of doing a second roundtrip; the search-
  // response's author_name list is what the candidate display name
  // already showed the user.
  if (!workKey.isEmpty()) {
    item.customFields.insert(QStringLiteral("openlibrary_key"), workKey);
  }

  // first_publish_date isn't standardised on the work record, but the
  // first-published-year + month combos are common enough to forward
  // when present.
  const QString firstPublishDate = work.value("first_publish_date").toString();
  if (!firstPublishDate.isEmpty()) {
    item.releaseDate = firstPublishDate;
  }

  // Subjects can be empty; covers list is more reliable on the work
  // response. Use the first cover_i when present.
  const QJsonArray covers = work.value("covers").toArray();
  if (!covers.isEmpty()) {
    appendCoverUrls(item, static_cast<qint64>(covers.first().toDouble(-1)));
  }

  return item;
}

void appendCoverUrls(Scraper::ScrapedItem &item, qint64 coverId) {
  if (coverId <= 0) {
    return;
  }
  // Medium becomes the front cover (good balance of quality vs
  // bytes for a typical grid tile); Large is bundled as a
  // secondary asset for users who want a higher-res variant.
  Scraper::MediaAsset front;
  front.type = QStringLiteral("front");
  front.label = QStringLiteral("Cover (medium)");
  front.url = QUrl(QStringLiteral("%1%2-M.jpg").arg(QString::fromLatin1(COVER_BASE)).arg(coverId));
  item.media.append(front);

  Scraper::MediaAsset large;
  large.type = QStringLiteral("fanart");
  large.label = QStringLiteral("Cover (large)");
  large.url = QUrl(QStringLiteral("%1%2-L.jpg").arg(QString::fromLatin1(COVER_BASE)).arg(coverId));
  item.media.append(large);
}

namespace {

// ISBN-10: 9 digits followed by a check char (0-9 or X). Valid iff
// sum(value[i] * (10 - i)) % 11 == 0, with a trailing 'X' counting as 10.
bool isValidIsbn10(const QString &isbn) {
  if (isbn.size() != 10) {
    return false;
  }
  int sum = 0;
  for (int i = 0; i < 10; ++i) {
    const QChar c = isbn.at(i);
    int value = 0;
    if (i == 9 && (c == QLatin1Char('X') || c == QLatin1Char('x'))) {
      value = 10;
    } else if (c.isDigit()) {
      value = c.digitValue();
    } else {
      return false;
    }
    sum += value * (10 - i);
  }
  return sum % 11 == 0;
}

// ISBN-13: 13 digits. Valid iff sum(digit[i] * w) % 10 == 0 with the
// alternating 1,3,1,3,... weight schedule.
bool isValidIsbn13(const QString &isbn) {
  if (isbn.size() != 13) {
    return false;
  }
  int sum = 0;
  for (int i = 0; i < 13; ++i) {
    const QChar c = isbn.at(i);
    if (!c.isDigit()) {
      return false;
    }
    sum += c.digitValue() * ((i % 2 == 0) ? 1 : 3);
  }
  return sum % 10 == 0;
}

} // namespace

QString extractIsbnFromText(const QString &text) {
  // Try ISBN-13 first (more specific), then ISBN-10. The leading "ISBN" and
  // internal hyphens/spaces are optional; we strip the separators to the
  // canonical form. Only a capture whose check digit validates is returned —
  // an unvalidated 10-digit run (a year, an internal id) otherwise fed a bogus
  // isbn= query and auto-applied wrong matches in batch (Kartend-tipud). All
  // matches are scanned so a real ISBN later in the string isn't shadowed by an
  // earlier invalid run.
  static const QRegularExpression isbn13(QStringLiteral("(?:ISBN[- :]*)?(97[89](?:[- ]?\\d){10})"),
                                         QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression isbn10(
      QStringLiteral("(?:ISBN[- :]*)?(\\d(?:[- ]?\\d){8}[- ]?[\\dXx])"),
      QRegularExpression::CaseInsensitiveOption);
  for (const auto *re : {&isbn13, &isbn10}) {
    auto it = re->globalMatch(text);
    while (it.hasNext()) {
      QString digits = it.next().captured(1);
      digits.remove(QChar('-'));
      digits.remove(QChar(' '));
      const bool valid = digits.size() == 13 ? isValidIsbn13(digits) : isValidIsbn10(digits);
      if (valid) {
        return digits;
      }
    }
  }
  return {};
}

} // namespace OpenLibraryParser
