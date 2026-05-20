#include "screenscraperquotamanager.h"

#include "screenscraperparser.h"

#include <QDateTime>
#include <QTime>

bool ScreenScraperQuotaManager::updateFromResponse(const QByteArray &json) {
  const auto info = ScreenScraperParser::extractUserInfo(json);
  if (!info) {
    // Anonymous tier / no block — keep the prior snapshot. Clobbering
    // with a half-valid record would make the dialog flicker between
    // "showing quota" and "no quota known".
    return false;
  }
  m_quota.valid = true;
  m_quota.dailyUsed = info->requestsToday;
  m_quota.dailyMax = info->maxRequestsPerDay;
  m_quota.koUsed = info->requestsKoToday;
  m_quota.koMax = info->maxRequestsKoPerDay;
  // SS rolls the per-day counters at 00:00 UTC. Compute the next such
  // boundary so the dialog can show the user when their quota frees up.
  // currentDateTimeUtc() + setTime + addDays uses only long-stable QDateTime
  // API — avoids the QTimeZone::UTC initialization enum, which is Qt 6.5+ and
  // breaks the build against the Qt 6.4 the CI runners ship.
  QDateTime resetUtc = QDateTime::currentDateTimeUtc();
  resetUtc.setTime(QTime(0, 0));
  m_quota.resetAtUtc = resetUtc.addDays(1);
  return true;
}
