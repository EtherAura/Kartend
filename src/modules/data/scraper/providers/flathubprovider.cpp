// Flathub AppStream provider. Key-less HTTP JSON; the interesting part is
// identity resolution — Flatpak launcher-import stubs carry the exact app id
// as their .kartlink target, so no name search ever runs for them (and there
// is no key-less GET search endpoint to run for anything else — see lookup()).
#include "flathubprovider.h"

#include <utility>

#include <QRegularExpression>

#include "flathubparser.h"
#include "kartlink.h"

namespace {

constexpr const char *FLATHUB_HOST = "flathub.org";
constexpr const char *APPSTREAM_URL = "https://flathub.org/api/v2/appstream/";
// No published quota; pace politely well under anything a backend would
// notice. The batch runner's 429 breaker backstops it.
constexpr int FLATHUB_RATE_LIMIT_MS = 300;

/// A Flatpak app id is reverse-DNS with at least three segments
/// (com.play0ad.zeroad); flatpak-the-tool enforces this shape at install
/// time, so anything else is a display name, not an id.
bool looksLikeFlatpakAppId(const QString &value) {
  static const QRegularExpression pattern(
      QStringLiteral("^[A-Za-z0-9_-]+(\\.[A-Za-z0-9_-]+){2,}$"));
  return pattern.match(value).hasMatch();
}

Scraper::ScrapeCandidate exactCandidate(const QString &appId, const QString &displayName) {
  Scraper::ScrapeCandidate candidate;
  candidate.displayName = displayName.isEmpty() ? appId : displayName;
  candidate.subtitle = QStringLiteral("Flatpak app %1").arg(appId);
  candidate.providerSpecificId = appId;
  candidate.matchScore = 100;
  return candidate;
}

} // namespace

FlathubProvider::FlathubProvider() {
  registerThrottles({{FLATHUB_HOST, FLATHUB_RATE_LIMIT_MS}});
}

QUrl FlathubProvider::searchUrl(const QString &query) const {
  if (query.trimmed().isEmpty()) {
    return {};
  }
  const QString encoded = QString::fromUtf8(QUrl::toPercentEncoding(query.trimmed()));
  return QUrl(QStringLiteral("https://flathub.org/apps/search?q=%1").arg(encoded));
}

void FlathubProvider::lookup(const QString &query, LookupCallback callback) {
  if (!callback) {
    return;
  }
  // Flathub's API search is POST-only, so there is no request to make for a
  // free-form name. A query that is itself an app id (the only identity the
  // appstream endpoint accepts) still resolves exactly; anything else
  // returns no candidates and the dialog's WebSearch link covers the
  // by-name case.
  const QString trimmed = query.trimmed();
  if (looksLikeFlatpakAppId(trimmed)) {
    callback(QList<Scraper::ScrapeCandidate>{exactCandidate(trimmed, trimmed)});
    return;
  }
  callback(QList<Scraper::ScrapeCandidate>{});
}

void FlathubProvider::lookup(const LookupContext &ctx, LookupCallback callback) {
  if (!callback) {
    return;
  }
  // Launcher-import stub: the target IS the app id (argv-style
  // `flatpak run <app-id>` launcher, so no scheme prefix to strip) — emit
  // one exact candidate and skip any search. fetchDetail() then does the
  // single appstream request.
  if (KartLink::isKartLinkPath(ctx.filePath)) {
    const auto link = KartLink::read(ctx.filePath);
    if (!link.isError() && looksLikeFlatpakAppId(link.value().target.trimmed())) {
      const QString appId = link.value().target.trimmed();
      callback(QList<Scraper::ScrapeCandidate>{exactCandidate(appId, link.value().title)});
      return;
    }
    // A stub that isn't Flatpak-shaped (or is unreadable) falls through to
    // the query path — same behaviour as a plain file.
  }
  lookup(ctx.query, std::move(callback));
}

void FlathubProvider::fetchDetail(const Scraper::ScrapeCandidate &candidate,
                                  DetailCallback callback) {
  if (!callback) {
    return;
  }
  if (candidate.providerSpecificId.isEmpty()) {
    callback(ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                             "Flathub candidate missing app id",
                                             "FlathubProvider::fetchDetail"));
    return;
  }
  const QString appId = candidate.providerSpecificId;
  const QUrl url(QString::fromLatin1(APPSTREAM_URL) + appId);
  getJson<Scraper::ScrapedItem>(
      userAgentHeader(), url,
      [appId](const QByteArray &body) { return FlathubParser::parseAppstream(body, appId); },
      std::move(callback));
}

void FlathubProvider::fetchMediaBytes(const QUrl &url, MediaCallback callback) {
  // Metadata-only provider (no MediaFetch capability, parser emits no media
  // assets): the launcher import already copies each app's exported icon as
  // its cover, so there is nothing legitimate to fetch here.
  Q_UNUSED(url);
  if (callback) {
    callback(ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                             "Flathub provider does not fetch media",
                                             "FlathubProvider::fetchMediaBytes"));
  }
}
