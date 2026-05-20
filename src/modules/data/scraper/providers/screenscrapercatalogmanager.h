#ifndef SCREENSCRAPERCATALOGMANAGER_H
#define SCREENSCRAPERCATALOGMANAGER_H

#include "errorutils.h"
#include "screenscrapersystems.h"

#include <functional>

#include <QHash>
#include <QList>
#include <QString>

namespace Scraper {
class HttpClient;
}

/// Owns the on-disk + in-memory catalog state ScreenScraperProvider
/// needs to translate raw SS responses into friendly labels.
///
/// Two catalogs:
///   - `systemesListe.php` — systemeid → display name + aliases +
///     extensions. Drives autodetect when a collection has no explicit
///     screenscraperSystemId. Not cached in memory here — every
///     `ensureSystemsCatalog` either hits the fresh disk cache or
///     fetches; the parsed list is handed to the callback for the
///     provider's `resolveSystemId` step.
///   - `mediasJeuListe.php` — canonical media-tag → friendly label
///     (`box-2d` → "Box (2D)"). Held in-memory as a QHash and copied
///     into the parser's ParseOptions for every lookup so newly added
///     SS media types render without a Kartend release.
///
/// Extracted from ScreenScraperProvider so the network + cache
/// orchestration is testable without a QApplication or live SS API.
/// All collaborators (HttpClient, credentials, error mapping) are
/// injected through the constructor so tests can supply mocks.
class ScreenScraperCatalogManager {
public:
  /// SS dev + optional user credentials. The resolver pattern lets the
  /// manager pick up fresh values on every catalog dance without
  /// holding a reference to GeneralSettings — the provider remains the
  /// single owner of the settings-read path.
  struct Credentials {
    QString devId;
    QString devPassword;
    QString userId;
    QString userPassword;
  };

  /// Provider-supplied accessor that snapshots the active SS creds at
  /// call time. Invoked once at the top of each ensure*Catalog call.
  using CredentialsResolver = std::function<Credentials()>;

  /// Provider-supplied projection from raw HTTP errors to SS-specific
  /// user-facing messages (HTTP 423 → "infrastructure down", 429 →
  /// "rate-limited", etc.). Kept injectable so the SS-specific status
  /// vocabulary stays in screenscraperprovider.cpp instead of being
  /// duplicated here. Falling back to identity is fine for tests.
  using ErrorMapper =
      std::function<ErrorUtils::ErrorContext(const ErrorUtils::ErrorContext &)>;

  using SystemsReadyCallback = std::function<void(QList<ScreenScraperSystems::System>)>;

  ScreenScraperCatalogManager(Scraper::HttpClient *httpClient, QString userAgent,
                              CredentialsResolver credentialsResolver,
                              ErrorMapper errorMapper);

  /// Ensure the systems catalog is loaded — from the disk cache when
  /// fresh, otherwise via a network fetch of systemesListe.php. The
  /// callback fires (on the main thread) once the catalog is ready;
  /// returns an empty list on hard failure so the scrape can still
  /// proceed with systemeid=0.
  void ensureSystemsCatalog(SystemsReadyCallback callback) const;

  /// Trigger a background refresh of the media-type catalog when the
  /// on-disk cache is missing or stale. Cheap (single GET, parsed once
  /// per 30 days). Scrapes never block on it — the next scrape sees
  /// the refreshed labels once the fetch lands.
  void ensureMediaTypeCatalog() const;

  /// Cached SS media-type catalog as { canonical-tag → friendly label }.
  /// Empty until the first successful ensureMediaTypeCatalog round
  /// trip; the parser falls back to the raw canonical tag when an
  /// entry is missing.
  [[nodiscard]] const QHash<QString, QString> &mediaTypeLabels() const {
    return m_mediaTypeLabels;
  }

private:
  Scraper::HttpClient *m_httpClient;
  QString m_userAgent;
  CredentialsResolver m_credentialsResolver;
  ErrorMapper m_errorMapper;
  mutable QHash<QString, QString> m_mediaTypeLabels;
};

#endif // SCREENSCRAPERCATALOGMANAGER_H
