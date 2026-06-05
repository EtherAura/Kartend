#ifndef OPENLIBRARYPROVIDER_H
#define OPENLIBRARYPROVIDER_H

#include "providerbase.h"

#include <QString>
#include <QStringList>
#include <QUrl>

/// Open Library API-backed provider for the "reference" category. No
/// credentials or API key required. The provider tries an ISBN-keyed
/// lookup when the query string contains a recognisable ISBN-10 or
/// ISBN-13 (typical for filenames like "9780451524935 - 1984.epub")
/// and falls back to a free-text title search otherwise.
class OpenLibraryProvider : public ProviderBase {
public:
  OpenLibraryProvider();

  [[nodiscard]] QString id() const override { return QStringLiteral("openlibrary"); }
  [[nodiscard]] QString displayName() const override { return QStringLiteral("Open Library"); }
  [[nodiscard]] QStringList categories() const override { return {QStringLiteral("reference")}; }
  [[nodiscard]] Capabilities capabilities() const override {
    return Capability::WebSearch | Capability::MetadataLookup | Capability::MediaFetch;
  }
  [[nodiscard]] QUrl searchUrl(const QString &query) const override;

  void lookup(const QString &query, LookupCallback callback) override;
  void fetchDetail(const Scraper::ScrapeCandidate &candidate, DetailCallback callback) override;
  void fetchMediaBytes(const QUrl &url, MediaCallback callback) override;
};

#endif // OPENLIBRARYPROVIDER_H
