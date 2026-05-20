#ifndef SCREENSCRAPERQUOTAMANAGER_H
#define SCREENSCRAPERQUOTAMANAGER_H

#include "scrapertypes.h"

#include <QByteArray>

/// Per-account quota tracker for ScreenScraper.fr.
///
/// SS embeds the account's `ssuser` block (request counters + per-day
/// limits) in every jeuInfos.php reply. Pulling it once per item is free
/// — the bytes are already in hand — so the dialog's live readout tracks
/// without a dedicated ssuserInfos.php round-trip. Extracted from
/// ScreenScraperProvider so the state + parse step are testable in
/// isolation without any HTTP machinery.
///
/// Anonymous-tier responses (no `ssuser` block) leave the previous
/// snapshot intact instead of clobbering it with a half-valid record.
class ScreenScraperQuotaManager {
public:
  ScreenScraperQuotaManager() = default;

  /// Refresh the cached quota from a raw jeuInfos.php response body.
  /// A response without a parseable `ssuser` block is a no-op (returns
  /// false) — the previous snapshot stays put.
  bool updateFromResponse(const QByteArray &json);

  /// Most recent per-account quota snapshot. Stays invalid (`valid ==
  /// false`) until the first lookup response with an `ssuser` block
  /// lands.
  [[nodiscard]] Scraper::QuotaStatus status() const { return m_quota; }

private:
  Scraper::QuotaStatus m_quota;
};

#endif // SCREENSCRAPERQUOTAMANAGER_H
