// Steam storefront provider. Key-less HTTP JSON; the interesting part is
// identity resolution — launcher-import stubs carry the exact appid, so no
// name search ever runs for them (see lookup(LookupContext&)).
#include "steamstoreprovider.h"

#include <utility>

#include <QUrlQuery>

#include "httpclient.h"
#include "kartlink.h"
#include "steamstoreparser.h"

namespace {

constexpr const char *STORE_HOST = "store.steampowered.com";
constexpr const char *STORE_SEARCH = "https://store.steampowered.com/api/storesearch/";
constexpr const char *STORE_APPDETAILS = "https://store.steampowered.com/api/appdetails";
// ~200 requests / 5 min per IP → one per 1.5s; pace a shade under.
constexpr int STORE_RATE_LIMIT_MS = 1600;
constexpr int CDN_RATE_LIMIT_MS = 50;

const QLatin1String kRunGameIdPrefix("steam://rungameid/");

/// Media URLs point at Steam's CDN fleet; the storefront occasionally
/// serves from the store host itself. Suffix-pinned for every media
/// fetch AND any redirect (SSRF defence — the list is required by
/// getImageBytes and passed to the video path's raw get too).
QStringList mediaHostSuffixes() {
  return {QStringLiteral("steamstatic.com"), QStringLiteral("steamcdn-a.akamaihd.net"),
          QStringLiteral("store.steampowered.com")};
}

} // namespace

SteamStoreProvider::SteamStoreProvider() {
  registerThrottles({{STORE_HOST, STORE_RATE_LIMIT_MS},
                     {"shared.akamai.steamstatic.com", CDN_RATE_LIMIT_MS},
                     {"shared.cloudflare.steamstatic.com", CDN_RATE_LIMIT_MS},
                     {"cdn.akamai.steamstatic.com", CDN_RATE_LIMIT_MS},
                     {"cdn.cloudflare.steamstatic.com", CDN_RATE_LIMIT_MS},
                     {"video.akamai.steamstatic.com", CDN_RATE_LIMIT_MS},
                     {"steamcdn-a.akamaihd.net", CDN_RATE_LIMIT_MS}});
}

QUrl SteamStoreProvider::searchUrl(const QString &query) const {
  if (query.trimmed().isEmpty()) {
    return {};
  }
  const QString encoded = QString::fromUtf8(QUrl::toPercentEncoding(query.trimmed()));
  return QUrl(QStringLiteral("https://store.steampowered.com/search/?term=%1").arg(encoded));
}

void SteamStoreProvider::lookup(const QString &query, LookupCallback callback) {
  if (!callback) {
    return;
  }
  const QString trimmed = query.trimmed();
  if (trimmed.isEmpty()) {
    callback(QList<Scraper::ScrapeCandidate>{});
    return;
  }
  QUrl url(QString::fromLatin1(STORE_SEARCH));
  QUrlQuery q;
  q.addQueryItem(QStringLiteral("term"), trimmed);
  // cc/l pin the storefront region/language so results (and later
  // appdetails fields) are stable regardless of the host's locale.
  q.addQueryItem(QStringLiteral("cc"), QStringLiteral("us"));
  q.addQueryItem(QStringLiteral("l"), QStringLiteral("en"));
  url.setQuery(q);

  getJson<QList<Scraper::ScrapeCandidate>>(
      userAgentHeader(), url,
      [](const QByteArray &body) { return SteamStoreParser::parseStoreSearch(body); },
      std::move(callback));
}

void SteamStoreProvider::lookup(const LookupContext &ctx, LookupCallback callback) {
  if (!callback) {
    return;
  }
  // Launcher-import stub: the appid is in the file — emit one exact
  // candidate and skip the search endpoint entirely. fetchDetail() does
  // the single appdetails request, which also trivially satisfies the
  // runner's detail-caching contract (nothing to cache).
  if (KartLink::isKartLinkPath(ctx.filePath)) {
    const auto link = KartLink::read(ctx.filePath);
    if (!link.isError() && link.value().target.startsWith(kRunGameIdPrefix)) {
      const QString appId = link.value().target.mid(kRunGameIdPrefix.size());
      if (!appId.isEmpty()) {
        Scraper::ScrapeCandidate candidate;
        candidate.displayName = link.value().title.isEmpty() ? ctx.query : link.value().title;
        candidate.subtitle = QStringLiteral("Steam app %1").arg(appId);
        candidate.providerSpecificId = appId;
        candidate.matchScore = 100;
        callback(QList<Scraper::ScrapeCandidate>{candidate});
        return;
      }
    }
    // A stub that isn't Steam-targeted (or is unreadable) still gets the
    // name search below — same behaviour as a plain file.
  }
  lookup(ctx.query, std::move(callback));
}

void SteamStoreProvider::fetchDetail(const Scraper::ScrapeCandidate &candidate,
                                     DetailCallback callback) {
  if (!callback) {
    return;
  }
  if (candidate.providerSpecificId.isEmpty()) {
    callback(ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                             "Steam candidate missing appid",
                                             "SteamStoreProvider::fetchDetail"));
    return;
  }
  const QString appId = candidate.providerSpecificId;
  QUrl url(QString::fromLatin1(STORE_APPDETAILS));
  QUrlQuery q;
  q.addQueryItem(QStringLiteral("appids"), appId);
  q.addQueryItem(QStringLiteral("cc"), QStringLiteral("us"));
  q.addQueryItem(QStringLiteral("l"), QStringLiteral("en"));
  url.setQuery(q);

  getJson<Scraper::ScrapedItem>(
      userAgentHeader(), url,
      [appId](const QByteArray &body) { return SteamStoreParser::parseAppDetails(body, appId); },
      std::move(callback));
}

void SteamStoreProvider::fetchMediaBytes(const QUrl &url, MediaCallback callback) {
  if (!callback) {
    return;
  }
  getImageBytes(userAgentHeader(), url, std::move(callback), mediaHostSuffixes());
}

void SteamStoreProvider::fetchMediaBytesForType(const QUrl &url, const QString &mediaType,
                                                MediaCallback callback) {
  if (!callback) {
    return;
  }
  if (Scraper::kindForType(mediaType) != Scraper::MediaKind::Video) {
    // Images keep the image/ pin + tight cap; the store never serves
    // manuals, so everything non-video routes through the image path.
    fetchMediaBytes(url, std::move(callback));
    return;
  }
  // Trailers: video/* payloads run tens of MB — the image cap and pin
  // would reject them. Host allowlist still applies to the fetch and any
  // redirect.
  Scraper::HttpClient::instance()->get(url, userAgentHeader(), std::move(callback),
                                       Scraper::HttpClient::kDefaultMaxResponseBytes,
                                       QStringLiteral("video/"), mediaHostSuffixes());
}
