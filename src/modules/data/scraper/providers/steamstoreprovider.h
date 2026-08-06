#ifndef STEAMSTOREPROVIDER_H
#define STEAMSTOREPROVIDER_H

#include "providerbase.h"

#include <QString>
#include <QStringList>
#include <QUrl>

/// Steam storefront web API provider (Kartend-ksjx0). No credentials or API
/// key. Its purpose is the data the local appinfo.vdf pass (Kartend-11elw)
/// cannot supply: store descriptions, screenshots, backgrounds, and
/// trailers.
///
/// Identity: for launcher-import stubs (.kartlink with a
/// steam://rungameid/<appid> target) the appid is read straight from the
/// file, so lookup() emits a single exact candidate with no search request
/// at all — no fuzzy name matching for imported items. Anything else falls
/// back to the storesearch endpoint by name.
///
/// Rate limits: the storefront API tolerates roughly 200 requests per
/// 5 minutes per IP; the host throttle paces just under that and the batch
/// runner's 429 breaker backstops it.
class SteamStoreProvider : public ProviderBase {
public:
  SteamStoreProvider();

  [[nodiscard]] QString id() const override { return QStringLiteral("steam"); }
  [[nodiscard]] QString displayName() const override { return QStringLiteral("Steam Store"); }
  [[nodiscard]] QStringList categories() const override { return {QStringLiteral("games")}; }
  [[nodiscard]] Capabilities capabilities() const override {
    return Capability::WebSearch | Capability::MetadataLookup | Capability::MediaFetch;
  }
  [[nodiscard]] QUrl searchUrl(const QString &query) const override;

  void lookup(const QString &query, LookupCallback callback) override;
  void lookup(const LookupContext &ctx, LookupCallback callback) override;
  void fetchDetail(const Scraper::ScrapeCandidate &candidate, DetailCallback callback) override;
  void fetchMediaBytes(const QUrl &url, MediaCallback callback) override;
  /// Kind-aware guard: trailers are video/* payloads far over the image
  /// cap, so the image pin must not apply to them.
  void fetchMediaBytesForType(const QUrl &url, const QString &mediaType,
                              MediaCallback callback) override;
};

#endif // STEAMSTOREPROVIDER_H
