#ifndef TMDBPROVIDER_H
#define TMDBPROVIDER_H

#include "providerbase.h"

#include <QString>
#include <QStringList>
#include <QUrl>

struct GeneralSettings;
struct CollectionConfig;

/// TMDB API-backed provider. Requires a v4 read access token (a free
/// TMDB account → Settings → API → "API Read Access Token (v4 auth)").
/// The token is read from `GeneralSettings::scraperCredentials["tmdb"]
/// ["api_token"]` at every API call, so editing the credential in the
/// settings panel takes effect without an app restart. Empty token =
/// lookup() returns an InvalidArgument error with a friendly "set
/// your token in Settings → Scrapers" message rather than firing a
/// guaranteed-401 request.
///
/// Implementation note: this provider needs a pointer to the live
/// GeneralSettings struct (the registry creates the provider; the
/// credential blob lives on MainWindow::m_generalSettings). Owners
/// pass a getter callback so the provider stays decoupled from the
/// storage location. When the getter is null or returns nullptr, the
/// provider behaves as if the token were unset.
class TmdbProvider : public ProviderBase {
public:
  using GeneralSettingsAccessor = std::function<const GeneralSettings *()>;
  /// Getter for the collection being scraped — supplies the collection name a
  /// Collection entity scrape searches TMDB for (Kartend-ckepd.5). Same shape as
  /// ScreenScraperProvider's accessor; the registry wires it per collection.
  using CollectionAccessor = std::function<const CollectionConfig *()>;

  TmdbProvider(GeneralSettingsAccessor settingsAccessor, CollectionAccessor collectionAccessor);

  [[nodiscard]] QString id() const override { return QStringLiteral("tmdb"); }
  [[nodiscard]] QString displayName() const override {
    return QStringLiteral("The Movie Database (TMDB)");
  }
  [[nodiscard]] QStringList categories() const override { return {QStringLiteral("video")}; }
  [[nodiscard]] Capabilities capabilities() const override {
    return Capability::WebSearch | Capability::MetadataLookup | Capability::MediaFetch;
  }
  [[nodiscard]] QUrl searchUrl(const QString &query) const override;

  void lookup(const QString &query, LookupCallback callback) override;
  void fetchDetail(const Scraper::ScrapeCandidate &candidate, DetailCallback callback) override;
  void fetchMediaBytes(const QUrl &url, MediaCallback callback) override;

  /// Kartend-ckepd.5: TMDB scrapes per-game items (Game) and whole-collection
  /// art (Collection). Category has no TMDB source and is deliberately omitted.
  [[nodiscard]] QList<Scraper::ScrapeEntityType> supportedEntities() const override {
    return {Scraper::ScrapeEntityType::Game, Scraper::ScrapeEntityType::Collection};
  }
  /// Scrape a Collection entity: search TMDB collections by the collection's
  /// name and emit the top match's logo/background art. A miss is a routine
  /// not-found (niche collections legitimately have none — Kartend-e8aag).
  void fetchEntity(const Scraper::EntityScrapeTarget &target, DetailCallback callback) override;

private:
  /// Returns the current token from credentials, or empty when unset.
  /// Single accessor so the not-configured error is consistent across
  /// lookup / fetchDetail / fetchMediaBytes.
  [[nodiscard]] QString currentToken() const;

  GeneralSettingsAccessor m_settingsAccessor;
  CollectionAccessor m_collectionAccessor;
};

#endif // TMDBPROVIDER_H
