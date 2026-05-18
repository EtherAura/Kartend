#ifndef WEBSEARCHPROVIDER_H
#define WEBSEARCHPROVIDER_H

#include "metadataprovider.h"

#include <QString>
#include <QStringList>
#include <QUrl>

/// URL-template implementation of MetadataProvider. Stage-1 baseline:
/// every built-in provider currently registered with MetadataProviderRegistry
/// is a WebSearchProvider — they don't talk to an API, they just build a
/// search URL the context menu opens in the user's browser. Stage-2 API
/// providers (ScreenScraper.fr, TMDB, MusicBrainz, ...) will subclass
/// MetadataProvider directly and override the API surfaces; this
/// concrete class never grows beyond the URL-search use case.
class WebSearchProvider : public MetadataProvider {
public:
  /// @param id Stable lowercase id (`"mobygames"`, `"tmdb"`, ...).
  /// @param displayName Human-readable label for the menu.
  /// @param categories Lowercase category tags the provider applies to.
  /// @param searchUrlTemplate A URL template containing `%1` where the
  ///        percent-encoded query string should be substituted.
  WebSearchProvider(QString id, QString displayName, QStringList categories,
                    QString searchUrlTemplate);

  [[nodiscard]] QString id() const override { return m_id; }
  [[nodiscard]] QString displayName() const override { return m_displayName; }
  [[nodiscard]] QStringList categories() const override { return m_categories; }
  [[nodiscard]] Capabilities capabilities() const override { return Capability::WebSearch; }
  [[nodiscard]] QUrl searchUrl(const QString &query) const override;

private:
  QString m_id;
  QString m_displayName;
  QStringList m_categories;
  QString m_searchUrlTemplate;
};

#endif // WEBSEARCHPROVIDER_H
