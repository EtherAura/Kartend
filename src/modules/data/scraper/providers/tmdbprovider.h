#ifndef TMDBPROVIDER_H
#define TMDBPROVIDER_H

#include "metadatalookupprovider.h"

#include <QString>
#include <QStringList>
#include <QUrl>

struct GeneralSettings;

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
class TmdbProvider : public MetadataLookupProvider {
public:
  using GeneralSettingsAccessor = std::function<const GeneralSettings *()>;

  explicit TmdbProvider(GeneralSettingsAccessor settingsAccessor);

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

private:
  /// Returns the current token from credentials, or empty when unset.
  /// Single accessor so the not-configured error is consistent across
  /// lookup / fetchDetail / fetchMediaBytes.
  [[nodiscard]] QString currentToken() const;

  GeneralSettingsAccessor m_settingsAccessor;
};

#endif // TMDBPROVIDER_H
