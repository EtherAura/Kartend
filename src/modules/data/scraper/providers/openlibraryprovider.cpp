// Open Library API provider. No credentials required; we throttle the
// search/works endpoints gently and the cover host more aggressively so
// a batch scrape doesn't hammer covers.openlibrary.org.
#include "openlibraryprovider.h"

#include <utility>

#include <QUrlQuery>

#include "openlibraryparser.h"

namespace {

constexpr const char *OL_HOST = "openlibrary.org";
constexpr const char *OL_COVERS_HOST = "covers.openlibrary.org";
constexpr const char *OL_SEARCH = "https://openlibrary.org/search.json";
constexpr const char *OL_WORKS_BASE = "https://openlibrary.org/works/";
constexpr int OL_RATE_LIMIT_MS = 200; // No documented hard cap; be polite.
constexpr int OL_COVERS_RATE_LIMIT_MS = 100;
constexpr int SEARCH_LIMIT = 10;

} // namespace

OpenLibraryProvider::OpenLibraryProvider() {
  registerThrottles({{OL_HOST, OL_RATE_LIMIT_MS}, {OL_COVERS_HOST, OL_COVERS_RATE_LIMIT_MS}});
}

QUrl OpenLibraryProvider::searchUrl(const QString &query) const {
  if (query.trimmed().isEmpty()) {
    return {};
  }
  const QString encoded = QString::fromUtf8(QUrl::toPercentEncoding(query.trimmed()));
  return QUrl(QStringLiteral("https://openlibrary.org/search?q=%1").arg(encoded));
}

void OpenLibraryProvider::lookup(const QString &query, LookupCallback callback) {
  if (!callback) {
    return;
  }
  const QString trimmed = query.trimmed();
  if (trimmed.isEmpty()) {
    callback(QList<Scraper::ScrapeCandidate>{});
    return;
  }

  // ISBN-keyed lookup is much more reliable than title search when the
  // filename actually contains one. Fall back to title search when no
  // ISBN match — still useful for {Author - Title.epub}-style filenames.
  const QString isbn = OpenLibraryParser::extractIsbnFromText(trimmed);

  QUrl url(QString::fromLatin1(OL_SEARCH));
  QUrlQuery q;
  if (!isbn.isEmpty()) {
    q.addQueryItem(QStringLiteral("isbn"), isbn);
  } else {
    q.addQueryItem(QStringLiteral("q"), trimmed);
  }
  q.addQueryItem(QStringLiteral("limit"), QString::number(SEARCH_LIMIT));
  url.setQuery(q);

  getJson<QList<Scraper::ScrapeCandidate>>(
      userAgentHeader(), url,
      [](const QByteArray &body) { return OpenLibraryParser::parseSearchResponse(body); },
      std::move(callback));
}

void OpenLibraryProvider::fetchDetail(const Scraper::ScrapeCandidate &candidate,
                                      DetailCallback callback) {
  if (!callback) {
    return;
  }
  if (candidate.providerSpecificId.isEmpty()) {
    callback(ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                             "Open Library candidate missing work key",
                                             "OpenLibraryProvider::fetchDetail"));
    return;
  }

  const QString workKey = candidate.providerSpecificId;
  const QUrl url(QString::fromLatin1(OL_WORKS_BASE) + workKey + QStringLiteral(".json"));

  getJson<Scraper::ScrapedItem>(
      userAgentHeader(), url,
      [workKey](const QByteArray &body) {
        return OpenLibraryParser::parseDetailResponse(body, workKey);
      },
      std::move(callback));
}

void OpenLibraryProvider::fetchMediaBytes(const QUrl &url, MediaCallback callback) {
  if (!callback) {
    return;
  }
  // getImageBytes pins the response to image/* — a misconfigured cover URL
  // that 200s with text/html or application/json must not be fed straight
  // into the image decoder — and pins the fetch + any redirect to
  // covers.openlibrary.org so a hostile/MITM'd upstream can't 3xx-redirect it
  // to an internal host (SSRF, Kartend audit faz4r).
  getImageBytes(userAgentHeader(), url, std::move(callback), {QString::fromLatin1(OL_COVERS_HOST)});
}
