#ifndef KARTEND_TESTS_STUBMETADATAPROVIDER_H
#define KARTEND_TESTS_STUBMETADATAPROVIDER_H

// Shared in-memory MetadataLookupProvider for scraper-related tests.
// Originally lived inside test_batchscraperunner.cpp; lifted to its own
// header so test_batchscraperunner_integration (and any future scraper
// integration tests) can consume the same canned-response shape without
// duplicating the boilerplate.

#include <utility>

#include <QHash>
#include <QList>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include "errorutils.h"
#include "metadatalookupprovider.h"
#include "scrapertypes.h"

namespace KartendTest {

/// Tiny in-memory MetadataLookupProvider. Returns whatever the test
/// preloaded for each query. Both lookup() and fetchDetail() fire the
/// callback on the same event-loop tick via QTimer::singleShot so the
/// chain behaves like the real (async) providers — without that, all
/// the callbacks run inline and the cancellation / progress-emission
/// ordering doesn't get exercised.
class StubMetadataProvider : public MetadataLookupProvider {
public:
  /// Per-query canned responses. The runner queries by the file's
  /// `completeBaseName` (extension-stripped name), so the key is the
  /// expected base name.
  struct Canned {
    QList<Scraper::ScrapeCandidate> candidates;
    Scraper::ScrapedItem detail;
    /// Force lookup() to return this error instead of the candidates.
    /// Empty message = no error.
    QString lookupError;
    /// Force fetchDetail() to return this error.
    QString detailError;
  };
  QHash<QString, Canned> byQuery;

  /// Per-URL canned cover bytes. fetchMediaBytes() looks up the
  /// requested URL here; missing URL → empty bytes (the runner treats
  /// that as a failed cover fetch and skips the write).
  QHash<QUrl, QByteArray> mediaByUrl;
  /// URLs in this set fail the fetchMediaBytes call. The bytes from
  /// `mediaByUrl` (if any) are ignored when the URL is also here.
  QSet<QUrl> mediaErrorUrls;
  /// Track which URLs the runner asked us to fetch — tests assert
  /// against the call shape to verify the fetchPrimaryCover toggle.
  mutable QList<QUrl> mediaRequestLog;

  QString id() const override { return QStringLiteral("stub"); }
  QString displayName() const override { return QStringLiteral("Stub"); }
  QStringList categories() const override { return {QStringLiteral("games")}; }
  Capabilities capabilities() const override { return Capability::MetadataLookup; }
  QUrl searchUrl(const QString &) const override { return {}; }

  void lookup(const QString &query, LookupCallback cb) override {
    QTimer::singleShot(0, [this, query, cb = std::move(cb)]() {
      const Canned &c = byQuery.value(query);
      if (!c.lookupError.isEmpty()) {
        cb(ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument, c.lookupError,
                                           "stub"));
        return;
      }
      cb(c.candidates);
    });
  }

  void fetchDetail(const Scraper::ScrapeCandidate &candidate, DetailCallback cb) override {
    QTimer::singleShot(0, [this, candidate, cb = std::move(cb)]() {
      // Walk byQuery looking for the candidate's id — small test tables
      // make the linear scan fine.
      for (auto it = byQuery.constBegin(); it != byQuery.constEnd(); ++it) {
        if (!it.value().candidates.isEmpty() &&
            it.value().candidates.first().providerSpecificId == candidate.providerSpecificId) {
          if (!it.value().detailError.isEmpty()) {
            cb(ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                               it.value().detailError, "stub"));
            return;
          }
          cb(it.value().detail);
          return;
        }
      }
      cb(Scraper::ScrapedItem{});
    });
  }

  void fetchMediaBytes(const QUrl &url, MediaCallback cb) override {
    mediaRequestLog.append(url);
    QTimer::singleShot(0, [this, url, cb = std::move(cb)]() {
      if (mediaErrorUrls.contains(url)) {
        cb(ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                           QStringLiteral("media fetch failed"), "stub"));
        return;
      }
      cb(mediaByUrl.value(url));
    });
  }
};

/// Helper to build a deterministic candidate + detail pair for a given
/// query. Keeps test bodies focused on controller behaviour rather than
/// structural data.
inline StubMetadataProvider::Canned makeStubMatch(const QString &id, const QString &title) {
  StubMetadataProvider::Canned c;
  Scraper::ScrapeCandidate cand;
  cand.displayName = title;
  cand.providerSpecificId = id;
  cand.matchScore = 100;
  c.candidates.append(cand);
  c.detail.title = title;
  c.detail.sourceProviderId = QStringLiteral("stub");
  return c;
}

} // namespace KartendTest

#endif // KARTEND_TESTS_STUBMETADATAPROVIDER_H
