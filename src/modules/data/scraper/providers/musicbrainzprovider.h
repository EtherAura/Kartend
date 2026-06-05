#ifndef MUSICBRAINZPROVIDER_H
#define MUSICBRAINZPROVIDER_H

#include "providerbase.h"

#include <QString>
#include <QStringList>
#include <QUrl>

/// MusicBrainz API-backed provider. No credentials required — MB only
/// gates anonymous access via User-Agent + a 1 req/sec rate limit, both
/// honoured via HttpClient. Cover Art Archive URLs are constructed
/// per-MBID without a separate API roundtrip.
///
/// Supports WebSearch (so the URL-only fallback in
/// MetadataProviderRegistry is replaced by this when registered) and
/// the full async API surface (MetadataLookup + MediaFetch).
class MusicBrainzProvider : public ProviderBase {
public:
  MusicBrainzProvider();

  [[nodiscard]] QString id() const override { return QStringLiteral("musicbrainz"); }
  [[nodiscard]] QString displayName() const override { return QStringLiteral("MusicBrainz"); }
  [[nodiscard]] QStringList categories() const override { return {QStringLiteral("audio")}; }
  [[nodiscard]] Capabilities capabilities() const override {
    return Capability::WebSearch | Capability::MetadataLookup | Capability::MediaFetch;
  }
  [[nodiscard]] QUrl searchUrl(const QString &query) const override;

  void lookup(const QString &query, LookupCallback callback) override;
  void fetchDetail(const Scraper::ScrapeCandidate &candidate, DetailCallback callback) override;
  void fetchMediaBytes(const QUrl &url, MediaCallback callback) override;
};

#endif // MUSICBRAINZPROVIDER_H
