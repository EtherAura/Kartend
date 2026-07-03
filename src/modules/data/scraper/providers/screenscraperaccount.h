#ifndef SCREENSCRAPERACCOUNT_H
#define SCREENSCRAPERACCOUNT_H

#include "errorutils.h"
#include "metadatalookupprovider.h"
#include "screenscraperparser.h"

#include <functional>

#include <QString>

struct GeneralSettings;

/// Account-scoped ScreenScraper helpers: credential resolution (shared
/// with ScreenScraperProvider::currentCredentials) plus the
/// ssuserInfos.php / ssinfraInfos.php probes. Split out of
/// screenscraperprovider.{h,cpp} so the settings UI — the only external
/// consumer — can query account state without pulling in the full
/// provider, and so the provider TU stays focused on the fetch/callback
/// pipeline.
namespace ScreenScraperProviderHelpers {

/// Resolve ScreenScraper credentials from settings, falling back to the bundled
/// dev key when the user hasn't set their own (dev fields only; user fields are
/// strictly opt-in). Single source of truth keyed off the SS_* constants so
/// currentCredentials() and the user/infra/health helpers can't drift on field
/// names or the literal "screenscraper" key (Kartend audit D-03).
struct SsCredentials {
  QString devId;
  QString devPassword;
  QString userId;
  QString userPassword;
};

[[nodiscard]] SsCredentials resolveSsCredentials(const GeneralSettings *settings);

/// Hit SS's `ssuserInfos.php` with whatever credentials are currently
/// in `settings.scraper.credentials["screenscraper"]` (falling back to
/// the bundled dev_id for the dev fields when absent). The callback
/// fires on the main thread with the parsed user-info struct or an
/// error context. Used by the Scraper settings panel to surface "this
/// account gets N threads" without forcing the user to scrape first.
using UserInfoCallback =
    std::function<void(ErrorUtils::Result<ScreenScraperParser::ScreenScraperUserInfo>)>;
void fetchUserInfo(const GeneralSettings *settings, UserInfoCallback callback);

/// Hit SS's `ssinfraInfos.php` to get realtime cluster status (CPU
/// load, active scrapers today, anonymous-tier shutdown flags). One
/// cheap probe used by the scrape-result dialog before firing the
/// real lookup so users see "SS is busy right now" instead of a
/// timeout. Needs only dev creds — user creds optional.
using InfraInfoCallback =
    std::function<void(ErrorUtils::Result<ScreenScraperParser::ScreenScraperInfraInfo>)>;
void fetchInfraInfo(const GeneralSettings *settings, InfraInfoCallback callback);

/// Probe ssinfraInfos.php and project the response into a HealthStatus
/// (refuseScrape + humanStatus). Honors `closeforleecher` /
/// `closeforexternalscrapers` for anonymous-only callers. Extracted
/// from ScreenScraperProvider::fetchHealthStatus so the pure
/// transformation (InfraInfo → HealthStatus) can be tested without a
/// network mock. Returns an empty HealthStatus on probe failure —
/// transient blips shouldn't surface in the dialog.
void fetchHealthStatus(const GeneralSettings *settings,
                       MetadataLookupProvider::HealthCallback callback);

} // namespace ScreenScraperProviderHelpers

#endif // SCREENSCRAPERACCOUNT_H
