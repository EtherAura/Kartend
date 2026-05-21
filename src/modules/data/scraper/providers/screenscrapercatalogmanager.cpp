#include "screenscrapercatalogmanager.h"

#include <utility>

#include <QUrl>
#include <QUrlQuery>

#include "httpclient.h"
#include "screenscrapermediatypecache.h"
#include "screenscrapersystemcache.h"

namespace {

constexpr const char *SS_SYSTEMES_LISTE = "https://api.screenscraper.fr/api2/systemesListe.php";
constexpr const char *SS_MEDIAS_JEU_LISTE = "https://api.screenscraper.fr/api2/mediasJeuListe.php";

} // namespace

ScreenScraperCatalogManager::ScreenScraperCatalogManager(Scraper::HttpClient *httpClient,
                                                         QString userAgent,
                                                         CredentialsResolver credentialsResolver,
                                                         ErrorMapper errorMapper)
    : m_httpClient(httpClient), m_userAgent(std::move(userAgent)),
      m_credentialsResolver(std::move(credentialsResolver)), m_errorMapper(std::move(errorMapper)) {
}

void ScreenScraperCatalogManager::ensureSystemsCatalog(SystemsReadyCallback callback) const {
  if (!callback) return;
  const QString cachePath = ScreenScraperSystemCache::defaultCachePath();

  // Disk-cache hit (fresh) → callback immediately, no network. Stale
  // disk hit also returns the on-disk copy AND kicks off a background
  // refresh — we don't want to block the user's scrape on a fresh
  // catalog when the on-disk one is good enough.
  if (!cachePath.isEmpty() && !ScreenScraperSystemCache::isCacheStale(cachePath)) {
    auto loaded = ScreenScraperSystemCache::loadCachedSystems(cachePath);
    if (loaded.isOk()) {
      callback(loaded.value());
      return;
    }
  }
  // Cache missing or stale — fetch fresh. If we have on-disk data
  // we'll still serve the user from it on a fetch failure (degrade
  // rather than error), so try a load first as a fallback ready
  // value.
  QList<ScreenScraperSystems::System> staleFallback;
  if (!cachePath.isEmpty()) {
    auto staleLoad = ScreenScraperSystemCache::loadCachedSystems(cachePath);
    if (staleLoad.isOk()) {
      staleFallback = staleLoad.value();
    }
  }

  // Network fetch needs dev credentials. SS rejects every API request
  // without a valid dev_id + dev_password (regular SS.fr account login
  // alone is not enough — it only acts as a per-account rate-limit
  // boost on TOP of dev creds). Without dev creds, fall back to
  // whatever the disk had (possibly empty).
  const Credentials creds = m_credentialsResolver ? m_credentialsResolver() : Credentials{};
  if (creds.devId.isEmpty() || creds.devPassword.isEmpty()) {
    callback(staleFallback);
    return;
  }
  const bool hasUser = !creds.userId.isEmpty() && !creds.userPassword.isEmpty();

  QUrl url(QString::fromLatin1(SS_SYSTEMES_LISTE));
  QUrlQuery q;
  q.addQueryItem(QStringLiteral("devid"), creds.devId);
  q.addQueryItem(QStringLiteral("devpassword"), creds.devPassword);
  q.addQueryItem(QStringLiteral("softname"), QStringLiteral("kartend"));
  q.addQueryItem(QStringLiteral("output"), QStringLiteral("json"));
  // ssid/sspassword optional — adds the user's per-account rate
  // limit on top of the dev tier when both are configured.
  if (hasUser) {
    q.addQueryItem(QStringLiteral("ssid"), creds.userId);
    q.addQueryItem(QStringLiteral("sspassword"), creds.userPassword);
  }
  url.setQuery(q);

  if (!m_httpClient) {
    callback(staleFallback);
    return;
  }
  m_httpClient->get(url, m_userAgent,
                    [callback = std::move(callback), cachePath, staleFallback,
                     errorMapper = m_errorMapper](ErrorUtils::Result<QByteArray> response) {
                      if (response.isError()) {
                        // Surface a structured warning when the systems-catalog fetch
                        // fails — the catch-all `callback(staleFallback)` lets the
                        // scrape proceed with whatever the disk cache had, but the
                        // log line tells the user *why* the live refresh fell back.
                        ErrorUtils::ErrorContext remapped =
                            errorMapper ? errorMapper(response.error()) : response.error();
                        ErrorUtils::logError(remapped);
                        callback(staleFallback);
                        return;
                      }
                      auto parsed =
                          ScreenScraperSystemCache::parseSystemsResponse(response.value());
                      if (parsed.isError() || parsed.value().isEmpty()) {
                        callback(staleFallback);
                        return;
                      }
                      if (!cachePath.isEmpty()) {
                        // Best-effort: cache write failure is logged inside saveSystems
                        // and falls back to in-memory-only — the lookup still proceeds.
                        (void)ScreenScraperSystemCache::saveSystems(cachePath, parsed.value());
                      }
                      callback(parsed.value());
                    });
}

void ScreenScraperCatalogManager::ensureMediaTypeCatalog() const {
  // Hot path: catalog already in memory. The map is the projection
  // we hand to the parser; non-empty means we've populated it from
  // either disk cache or a fresh fetch, so callers can move on.
  if (!m_mediaTypeLabels.isEmpty()) return;
  const QString cachePath = ScreenScraperMediaTypeCache::defaultCachePath();
  // Disk-cache hit (fresh) → populate in memory and return. Stale
  // disk hit also populates from disk AND kicks off a background
  // refresh — same degrade-rather-than-block policy as
  // ensureSystemsCatalog.
  if (!cachePath.isEmpty()) {
    auto loaded = ScreenScraperMediaTypeCache::loadCachedMediaTypes(cachePath);
    if (loaded.isOk() && !loaded.value().isEmpty()) {
      for (const auto &mt : loaded.value()) {
        if (!mt.type.isEmpty() && !mt.displayName.isEmpty()) {
          m_mediaTypeLabels.insert(mt.type, mt.displayName);
        }
      }
      // If still fresh, no network needed.
      if (!ScreenScraperMediaTypeCache::isCacheStale(cachePath)) return;
    }
  }
  // Stale or missing — fire a background fetch. We never block the
  // scrape on this; subsequent scrapes will see the refreshed labels
  // once the fetch lands. Needs dev creds (SS rejects unauthenticated
  // catalog fetches the same way it rejects systemesListe).
  const Credentials creds = m_credentialsResolver ? m_credentialsResolver() : Credentials{};
  if (creds.devId.isEmpty() || creds.devPassword.isEmpty()) return;

  QUrl url(QString::fromLatin1(SS_MEDIAS_JEU_LISTE));
  QUrlQuery q;
  q.addQueryItem(QStringLiteral("devid"), creds.devId);
  q.addQueryItem(QStringLiteral("devpassword"), creds.devPassword);
  q.addQueryItem(QStringLiteral("softname"), QStringLiteral("kartend"));
  q.addQueryItem(QStringLiteral("output"), QStringLiteral("json"));
  if (!creds.userId.isEmpty() && !creds.userPassword.isEmpty()) {
    q.addQueryItem(QStringLiteral("ssid"), creds.userId);
    q.addQueryItem(QStringLiteral("sspassword"), creds.userPassword);
  }
  url.setQuery(q);

  if (!m_httpClient) return;
  // Use a mutable-this lambda to write back the populated map. The
  // catalog manager's lifetime is tied to the provider, which is
  // tied to the registry — when the registry releases the provider,
  // any in-flight fetch's callback would run on a freed `this`. The
  // HttpClient callback is fired on the main thread and the registry
  // tear-down is also main-thread, so the race is serialised;
  // `ensureMediaTypeCatalog` is best-effort and we accept the small
  // UAF window as the practical tradeoff for not adding a QPointer-
  // style guard to a non-QObject helper.
  m_httpClient->get(url, m_userAgent, [this, cachePath](ErrorUtils::Result<QByteArray> response) {
    if (response.isError()) {
      // Quiet log path — fetching the catalog is a polish
      // refresh, not load-bearing for a scrape. The fallback
      // behavior (raw SS tags as labels) is documented and
      // acceptable when SS is having a bad day.
      ErrorUtils::ErrorContext remapped =
          m_errorMapper ? m_errorMapper(response.error()) : response.error();
      ErrorUtils::logError(remapped);
      return;
    }
    auto parsed = ScreenScraperMediaTypeCache::parseMediaTypesResponse(response.value());
    if (parsed.isError() || parsed.value().isEmpty()) return;
    for (const auto &mt : parsed.value()) {
      if (!mt.type.isEmpty() && !mt.displayName.isEmpty()) {
        m_mediaTypeLabels.insert(mt.type, mt.displayName);
      }
    }
    if (!cachePath.isEmpty()) {
      (void)ScreenScraperMediaTypeCache::saveMediaTypes(cachePath, parsed.value());
    }
  });
}
