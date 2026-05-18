#ifndef BUNDLED_CREDENTIALS_H
#define BUNDLED_CREDENTIALS_H

#include <QString>

/// Compile-time-bundled API credentials that ship with Kartend so the
/// median user doesn't need to register their own dev account before
/// scraping. Power users / batch scrapers can still override these via
/// Settings → Scrapers (per-provider tab) and their own credentials win.
///
/// **Why obfuscated, not encrypted**: the binary necessarily contains
/// the deobfuscation routine, so a determined attacker can extract the
/// values trivially. The point is purely to prevent casual harvesting
/// — `strings(1)` over the binary doesn't reveal the bundled password,
/// which is the only realistic threat model. Real protection of the
/// bundled credentials would require a server-side proxy, which would
/// add a single point of failure that the SS-Skyscraper-other-Open-
/// Source-scraper precedent shows is unnecessary in practice.
///
/// **Fallback when un-populated**: the bundled values default to empty
/// strings until the project's SS dev-credentials application is
/// approved. Empty-bundled means the provider falls back to the
/// "credentials not configured" error — i.e., behaviourally identical
/// to a build with no bundled creds.
namespace BundledCredentials {

/// One provider's bundled credential pair. Either field may be empty
/// when the project hasn't (yet) acquired credentials for that
/// provider; the consumer treats that case as "no bundled creds —
/// require user to supply their own".
struct ScreenScraperPair {
  QString devId;
  QString devPassword;
};

/// Returns the bundled SS dev credentials, deobfuscated on demand.
/// Empty pair when un-populated.
[[nodiscard]] ScreenScraperPair screenscraper();

} // namespace BundledCredentials

#endif // BUNDLED_CREDENTIALS_H
