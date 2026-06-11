#include "providerbase.h"

QString ProviderBase::userAgent() {
  // APP_VERSION is injected at compile time via target_compile_definitions
  // in the top-level CMakeLists.txt.
  return QStringLiteral("Kartend/%1 (https://github.com/EtherAura/Kartend)")
      .arg(QString::fromLatin1(APP_VERSION));
}

Scraper::HttpClient::RawHeaders ProviderBase::userAgentHeader() {
  return {{QByteArrayLiteral("User-Agent"), userAgent().toUtf8()}};
}

void ProviderBase::registerThrottles(
    std::initializer_list<std::pair<const char *, int>> hostLimits) {
  Scraper::HttpClient *client = Scraper::HttpClient::instance();
  for (const auto &[host, intervalMs] : hostLimits) {
    client->setRateLimit(QString::fromLatin1(host), intervalMs);
  }
}

ProviderBase::TestFetchFunction ProviderBase::s_testFetch;

void ProviderBase::setFetchFunctionForTesting(TestFetchFunction fetch) {
  s_testFetch = std::move(fetch);
}
