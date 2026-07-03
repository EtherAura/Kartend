// Pure ScreenScraper api2 URL/query builders + HTTP error mapping. Moved
// verbatim from screenscraperprovider.cpp so the SS wire shape can be
// unit-tested without the provider or the network.
#include "screenscraperurls.h"

#include <QUrlQuery>

namespace {

constexpr const char *SS_JEUINFOS = "https://api.screenscraper.fr/api2/jeuInfos.php";
// Platform/system media endpoint. Like mediaJeu.php it returns the media file
// bytes directly (NOT a JSON metadata document) keyed by systemeid + media
// type. Verified against the live SS API: the endpoint and parameter names
// are correct; the media parameter needs the region-qualified token form
// ("wheel(wor)" — buildSystemeMediaUrl appends the suffix); a missing
// type/region answers 200 "NOMEDIA" as text/html (caught by the image/
// content-type gate); quota exhaustion answers HTTP 430 like every other
// api2 endpoint.
constexpr const char *SS_MEDIASYSTEME = "https://api.screenscraper.fr/api2/mediaSysteme.php";

} // namespace

namespace ScreenScraperUrls {

void addCommonQueryParams(QUrlQuery &q, const QString &devId, const QString &devPassword) {
  q.addQueryItem(QStringLiteral("devid"), devId);
  q.addQueryItem(QStringLiteral("devpassword"), devPassword);
  q.addQueryItem(QStringLiteral("softname"), QStringLiteral("kartend"));
  q.addQueryItem(QStringLiteral("output"), QStringLiteral("json"));
}

QUrl buildJeuInfosUrl(const Credentials &creds, const QString &romnom, int systemeid,
                      const RomHasher::Result &hashes, bool hasUser) {
  QUrl url(QString::fromLatin1(SS_JEUINFOS));
  QUrlQuery q;
  // SECURITY (Kartend-0gp7): devpassword / sspassword unavoidably travel
  // in the query string. ScreenScraper's api2 endpoints authenticate only
  // via query parameters — there is no header-based auth — so unlike TMDB
  // (moved to an Authorization: Bearer header) these credentials cannot be
  // relocated off the URL. The residual exposure is bounded: every request
  // is HTTPS, so the query is encrypted on the wire and never appears in a
  // cleartext Referer to a third party; and local scrape.log lines pass
  // through redactedUrlForLog() (httpclient.cpp), which masks
  // devpassword/sspassword/ssid/devid before anything is written. Upstream
  // proxy access logs remain the only unmitigated sink and are outside our
  // control. Same constraint applies to every other api2 builder below.
  addCommonQueryParams(q, creds.devId, creds.devPassword);
  q.addQueryItem(QStringLiteral("romnom"), romnom);
  q.addQueryItem(QStringLiteral("systemeid"), QString::number(systemeid));
  if (!hashes.md5.isEmpty()) {
    q.addQueryItem(QStringLiteral("md5"), hashes.md5);
  }
  if (!hashes.sha1.isEmpty()) {
    q.addQueryItem(QStringLiteral("sha1"), hashes.sha1);
  }
  if (!hashes.crc.isEmpty()) {
    q.addQueryItem(QStringLiteral("crc"), hashes.crc);
  }
  if (hashes.size > 0) {
    q.addQueryItem(QStringLiteral("romtaille"), QString::number(hashes.size));
  }
  if (hasUser) {
    q.addQueryItem(QStringLiteral("ssid"), creds.userId);
    q.addQueryItem(QStringLiteral("sspassword"), creds.userPassword);
  }
  url.setQuery(q);
  return url;
}

QUrl buildSystemeMediaUrl(const Credentials &creds, int systemeid, const QString &apiToken,
                          bool hasUser) {
  QUrl url(QString::fromLatin1(SS_MEDIASYSTEME));
  QUrlQuery q;
  // devid/devpassword/softname like the api2 builders, but NO output=json — this
  // is a media (bytes) endpoint, not a JSON metadata one (Kartend-ckepd.4).
  q.addQueryItem(QStringLiteral("devid"), creds.devId);
  q.addQueryItem(QStringLiteral("devpassword"), creds.devPassword);
  q.addQueryItem(QStringLiteral("softname"), QStringLiteral("kartend"));
  if (hasUser) {
    q.addQueryItem(QStringLiteral("ssid"), creds.userId);
    q.addQueryItem(QStringLiteral("sspassword"), creds.userPassword);
  }
  q.addQueryItem(QStringLiteral("systemeid"), QString::number(systemeid));
  // Live-API verified: a bare token ("wheel") answers 200 "NOMEDIA"
  // (text/html) even for systems that DO have the art — the media parameter
  // wants the region-qualified form ("wheel(wor)"). Platform art on SS is
  // almost exclusively world-tagged, so request (wor) rather than plumbing
  // the per-game region preference through. A genuinely missing type still
  // answers NOMEDIA as text/html, which fetchMediaBytes' image/ content-type
  // gate converts to a structured, non-fatal per-asset miss.
  q.addQueryItem(QStringLiteral("media"), apiToken + QStringLiteral("(wor)"));
  url.setQuery(q);
  return url;
}

ErrorUtils::ErrorContext mapScreenScraperHttpError(const ErrorUtils::ErrorContext &original) {
  if (original.httpStatus <= 0) return original;
  QString message;
  // Default code preserves the upstream classification; the 404 case below
  // overrides it to RemoteResourceNotFound so the runner buckets a genuine
  // "no entry" as not-found rather than an error (Kartend-e8aag).
  ErrorUtils::ErrorCode mappedCode = original.code != ErrorUtils::ErrorCode::Success
                                         ? original.code
                                         : ErrorUtils::ErrorCode::DatabaseQueryFailed;
  switch (original.httpStatus) {
  case 400:
    message = QStringLiteral("ScreenScraper rejected the request as malformed (HTTP 400). "
                             "Likely causes: an invalid system id, a missing required field, "
                             "or a path component the server refused.");
    break;
  case 401:
    message = QStringLiteral(
        "ScreenScraper closed its API to non-members because the server is "
        "overloaded (HTTP 401). Try again later, or sign in with member "
        "credentials under Settings → Scrapers → ScreenScraper for priority access.");
    break;
  case 403:
    message = QStringLiteral(
        "ScreenScraper rejected the developer credentials (HTTP 403). "
        "Verify the dev_id and dev_password under Settings → Scrapers → ScreenScraper.");
    break;
  case 404:
    message = QStringLiteral("ScreenScraper has no entry for this game (HTTP 404).");
    mappedCode = ErrorUtils::ErrorCode::RemoteResourceNotFound;
    break;
  case 423:
    message = QStringLiteral("ScreenScraper infrastructure is currently down (HTTP 423). "
                             "Their service is unavailable — try again later.");
    break;
  case 426:
    message = QStringLiteral("This Kartend build was blocked by ScreenScraper (HTTP 426). "
                             "Update Kartend to the latest version — older releases are "
                             "occasionally blacklisted when their API requests fall behind a "
                             "breaking change.");
    break;
  case 429: {
    QString tail = QStringLiteral(" Reduce concurrent scrapes or wait before retrying.");
    if (original.retryAfterSeconds > 0) {
      tail = QStringLiteral(" Server asked us to wait %1 second(s) before retrying.")
                 .arg(original.retryAfterSeconds);
    }
    message = QStringLiteral("ScreenScraper rate-limited the request (HTTP 429).") + tail;
    break;
  }
  case 430:
    message = QStringLiteral("ScreenScraper's daily request quota for this account is "
                             "exhausted (HTTP 430). The quota resets at midnight UTC.");
    break;
  case 431:
    message = QStringLiteral("ScreenScraper hit the daily failed-lookup quota for this "
                             "account (HTTP 431). Too many ROMs in this collection don't "
                             "match anything in the SS database — fix the collection's "
                             "system id or the file naming, then try again tomorrow.");
    break;
  default:
    return original;
  }
  auto remapped = ErrorUtils::ErrorContext::error(mappedCode, message,
                                                  original.source.isEmpty()
                                                      ? QStringLiteral("ScreenScraperProvider")
                                                      : original.source)
                      .withHttpStatus(original.httpStatus);
  if (original.retryAfterSeconds > 0) {
    remapped.withRetryAfter(original.retryAfterSeconds);
  }
  if (!original.details.isEmpty()) {
    remapped.withDetails(original.details);
  }
  return remapped;
}

} // namespace ScreenScraperUrls
