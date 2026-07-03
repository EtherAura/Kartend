// Account-scoped ScreenScraper helpers: credential resolution shared with
// the provider, plus the ssuserInfos.php / ssinfraInfos.php probes the
// settings panel and the pre-scrape health gate consume. Moved verbatim
// from screenscraperprovider.cpp.
#include "screenscraperaccount.h"

#include <algorithm>
#include <utility>

#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

#include "bundledcredentials.h"
#include "collection/generalsettings.h"
#include "httpclient.h"
#include "providerbase.h"
#include "screenscraperparser.h"
#include "screenscraperurls.h"

namespace {

constexpr const char *SS_PROVIDER_ID = "screenscraper";
constexpr const char *SS_FIELD_DEV_ID = "dev_id";
constexpr const char *SS_FIELD_DEV_PASSWORD = "dev_password";
constexpr const char *SS_FIELD_USER_ID = "user_id";
constexpr const char *SS_FIELD_USER_PASSWORD = "user_password";

} // namespace

namespace ScreenScraperProviderHelpers {

SsCredentials resolveSsCredentials(const GeneralSettings *settings) {
  SsCredentials c;
  if (settings) {
    const auto blob = settings->scraper.credentials.value(QString::fromLatin1(SS_PROVIDER_ID));
    c.devId = blob.value(QString::fromLatin1(SS_FIELD_DEV_ID));
    c.devPassword = blob.value(QString::fromLatin1(SS_FIELD_DEV_PASSWORD));
    c.userId = blob.value(QString::fromLatin1(SS_FIELD_USER_ID));
    c.userPassword = blob.value(QString::fromLatin1(SS_FIELD_USER_PASSWORD));
  }
  // Bundled dev fallback (empty until the SS forum application is approved,
  // which the caller treats as "credentials not configured").
  if (c.devId.isEmpty() || c.devPassword.isEmpty()) {
    const auto bundled = BundledCredentials::screenscraper();
    if (c.devId.isEmpty()) c.devId = bundled.devId;
    if (c.devPassword.isEmpty()) c.devPassword = bundled.devPassword;
  }
  return c;
}

void fetchUserInfo(const GeneralSettings *settings, UserInfoCallback callback) {
  if (!callback) return;
  // Build credentials from the same path as the main provider so the detected
  // user-info exactly matches what scrapes will actually send (Kartend audit
  // D-03). user_id / user_password are strictly opt-in (no fallback).
  const SsCredentials creds = resolveSsCredentials(settings);
  const QString &devId = creds.devId;
  const QString &devPassword = creds.devPassword;
  const QString &userId = creds.userId;
  const QString &userPassword = creds.userPassword;
  if (devId.isEmpty() || devPassword.isEmpty()) {
    callback(
        ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                        QStringLiteral("Developer credentials are not available"),
                                        "ScreenScraperProviderHelpers::fetchUserInfo"));
    return;
  }
  if (userId.isEmpty() || userPassword.isEmpty()) {
    callback(ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                             QStringLiteral("Member credentials not set"),
                                             "ScreenScraperProviderHelpers::fetchUserInfo"));
    return;
  }
  QUrl url(QStringLiteral("https://api.screenscraper.fr/api2/ssuserInfos.php"));
  QUrlQuery q;
  ScreenScraperUrls::addCommonQueryParams(q, devId, devPassword);
  q.addQueryItem(QStringLiteral("ssid"), userId);
  q.addQueryItem(QStringLiteral("sspassword"), userPassword);
  url.setQuery(q);
  Scraper::HttpClient::instance()->get(
      url, ProviderBase::userAgentHeader(),
      [callback = std::move(callback)](ErrorUtils::Result<QByteArray> response) {
        if (response.isError()) {
          callback(ScreenScraperUrls::mapScreenScraperHttpError(response.error()));
          return;
        }
        callback(ScreenScraperParser::parseUserInfoResponse(response.value()));
      },
      Scraper::HttpClient::kDefaultMaxResponseBytes, QString(),
      // Kartend-8xs72: ssuserInfos.php carries devpassword/sspassword in the
      // query string — pin it (and its redirects) to ScreenScraper's domain so
      // a cross-host redirect can't forward the credential-bearing URL. Same
      // allowlist mechanism fetchMediaBytes uses for media URLs.
      {QString::fromLatin1(ScreenScraperUrls::SS_MEDIA_HOST_SUFFIX)});
}

void fetchInfraInfo(const GeneralSettings *settings, InfraInfoCallback callback) {
  if (!callback) return;
  // Mirror fetchUserInfo's credential resolution so the probe runs with exactly
  // what subsequent scrapes will use (Kartend audit D-03). Dev creds are
  // required (SS rejects unauthenticated infra polls); user creds are optional
  // and only add the tier boost.
  const SsCredentials creds = resolveSsCredentials(settings);
  const QString &devId = creds.devId;
  const QString &devPassword = creds.devPassword;
  const QString &userId = creds.userId;
  const QString &userPassword = creds.userPassword;
  if (devId.isEmpty() || devPassword.isEmpty()) {
    callback(
        ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                        QStringLiteral("Developer credentials are not available"),
                                        "ScreenScraperProviderHelpers::fetchInfraInfo"));
    return;
  }
  QUrl url(QStringLiteral("https://api.screenscraper.fr/api2/ssinfraInfos.php"));
  QUrlQuery q;
  ScreenScraperUrls::addCommonQueryParams(q, devId, devPassword);
  if (!userId.isEmpty() && !userPassword.isEmpty()) {
    q.addQueryItem(QStringLiteral("ssid"), userId);
    q.addQueryItem(QStringLiteral("sspassword"), userPassword);
  }
  url.setQuery(q);
  Scraper::HttpClient::instance()->get(
      url, ProviderBase::userAgentHeader(),
      [callback = std::move(callback)](ErrorUtils::Result<QByteArray> response) {
        if (response.isError()) {
          callback(ScreenScraperUrls::mapScreenScraperHttpError(response.error()));
          return;
        }
        callback(ScreenScraperParser::parseInfraInfoResponse(response.value()));
      },
      Scraper::HttpClient::kDefaultMaxResponseBytes, QString(),
      // Kartend-8xs72: ssinfraInfos.php carries devpassword/sspassword in the
      // query string — pin it (and its redirects) to ScreenScraper's domain so
      // a cross-host redirect can't forward the credential-bearing URL. Same
      // allowlist mechanism fetchMediaBytes uses for media URLs.
      {QString::fromLatin1(ScreenScraperUrls::SS_MEDIA_HOST_SUFFIX)});
}

void fetchHealthStatus(const GeneralSettings *settings,
                       MetadataLookupProvider::HealthCallback callback) {
  if (!callback) return;
  // Whether the caller has user creds wired up — drives whether the
  // `closeforleecher` flag should refuse the scrape (anonymous tier is
  // the leecher tier in SS parlance) vs just warn.
  const SsCredentials creds = resolveSsCredentials(settings);
  const bool hasUserCreds = !creds.userId.isEmpty() && !creds.userPassword.isEmpty();
  fetchInfraInfo(settings, [callback = std::move(callback), hasUserCreds](
                               ErrorUtils::Result<ScreenScraperParser::ScreenScraperInfraInfo> r) {
    using HealthStatus = MetadataLookupProvider::HealthStatus;
    if (r.isError()) {
      // Probe failure is non-fatal — the actual scrape will hit the
      // same error path and report it via mapScreenScraperHttpError.
      // Stay silent in the dialog rather than fearmongering on a
      // transient blip.
      callback(HealthStatus{});
      return;
    }
    const auto &info = r.value();
    HealthStatus out;
    // Refuse anonymous scrapes when SS has shut its API to the
    // leecher tier. Member scrapes still go through (SS allows them
    // on a separate path).
    if (info.closedForLeechers && !hasUserCreds) {
      out.refuseScrape = true;
      out.humanStatus =
          QObject::tr("ScreenScraper has closed its API to anonymous traffic right now. "
                      "Sign in with member credentials under Settings → Scrapers → ScreenScraper, "
                      "or try again later.");
      callback(out);
      return;
    }
    if (info.closedForNonMembers && !hasUserCreds) {
      out.refuseScrape = true;
      out.humanStatus = QObject::tr("ScreenScraper has closed its API to non-members right now "
                                    "(server overloaded). Sign in with member credentials, or try "
                                    "again later.");
      callback(out);
      return;
    }
    // Surface load info when any of the CPU figures are alarming or
    // scraper count is high. Threshold is intentionally loose — we
    // want to nudge the user about slow scrapes, not pepper them
    // with infra trivia on a quiet day.
    const int peakCpu = std::max({info.cpu1Percent, info.cpu2Percent, info.cpu3Percent});
    if (peakCpu >= 70 || info.activeScrapers >= 200) {
      QStringList parts;
      if (peakCpu > 0) {
        parts << QObject::tr("CPU %1%").arg(peakCpu);
      }
      if (info.activeScrapers > 0) {
        parts << QObject::tr("%1 active scrapers").arg(info.activeScrapers);
      }
      out.humanStatus = QObject::tr("ScreenScraper is busy right now (%1) — "
                                    "expect slower downloads.")
                            .arg(parts.join(QStringLiteral(", ")));
    }
    callback(out);
  });
}

} // namespace ScreenScraperProviderHelpers
