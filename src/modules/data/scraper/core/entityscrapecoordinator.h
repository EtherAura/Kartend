#ifndef ENTITYSCRAPECOORDINATOR_H
#define ENTITYSCRAPECOORDINATOR_H

#include <memory>

#include <QList>
#include <QString>
#include <QStringList>

#include "errorutils.h"
#include "metadatalookupprovider.h"
#include "scrapepersistence.h"
#include "scrapertypes.h"

namespace Scraper {

class ScraperService;

/// Entity-scrape execution engine, extracted from ScraperService via the
/// friend + back-pointer pattern (precedent: VirtualScrollEngine): the
/// canonical run state — queue, cursor, summary, run generation, cancel
/// token, in-flight write list — stays on the service so the batch path,
/// persistence, and teardown guarantees are untouched; this class owns only
/// the entity flow's code. An entity job (platform/collection/category) is a
/// single provider->fetchEntity() dispatch whose media fan-out, config
/// wiring, and queue accounting live here.
///
/// Not a QObject: async callbacks guard on QPointer<ScraperService> (the
/// coordinator is a value member of the service, so its lifetime is the
/// service's) and re-enter through the service's member.
class EntityScrapeCoordinator {
public:
  explicit EntityScrapeCoordinator(ScraperService *service) : m_svc(service) {}

  /// Dispatch the current queue entry as a single entity scrape: resolve a
  /// provider, fire one fetchEntity(), and advance on its async result. Used
  /// for non-game jobs in both Auto and Interactive mode — an entity scrape
  /// has no per-item picker.
  void startEntityCollection();

  /// Manufacturer-logo matching pass (Kartend-cnti4, user-approved design):
  /// runs when a scrape queue COMPLETES (not on cancel/quota-stop). For each
  /// collection whose name case-insensitively matches a recorded ScreenScraper
  /// company name, wire the on-disk company art (harvested from this and
  /// earlier game scrapes) into its icon/logo slots and persist — so a
  /// hand-made "manufacturer" parent gets its logo from the same scrape run
  /// that arted its children, with no extra requests and no user step. A slot
  /// holding platform art or a user-chosen image is never displaced: only
  /// empty or company-art-owned slots are written.
  void applyManufacturerLogos();

private:
  /// @p wikiFallbackTried marks a result that already went through the
  /// Wikidata logo fallback (Kartend-czna3) so a not-found from the fallback
  /// itself books as not-found instead of recursing.
  void onEntityFetchComplete(const ErrorUtils::Result<Scraper::ScrapedItem> &result,
                             const std::shared_ptr<MetadataLookupProvider> &provider,
                             quint64 generation, bool wikiFallbackTried = false);
  /// Map scraped platform art (the _shared/ paths writeMediaFiles wrote this
  /// run PLUS the ones it kept because they already satisfied the rescrape
  /// policy, Kartend-jjyst.5) onto the collection's CollectionConfig art
  /// fields and persist via the settings manager. `collectionUuid` is
  /// verified against the collection at `collectionIndex` (re-resolving by
  /// UUID on mismatch) so a collections list edited mid-run can't receive
  /// another collection's art.
  void applyEntityArtToConfig(const QString &collectionUuid, int collectionIndex,
                              const QList<Scraper::MediaAsset> &assets,
                              const QStringList &landedPaths);
  /// File-I/O phase of an entity scrape: run writeMediaFiles on the global
  /// QThreadPool (mirroring BatchScrapeRunner's media-write phase) so a slow
  /// or wedged mount can't block the GUI thread, guarded by the per-run
  /// cancel token and a step watchdog that errors the item instead of
  /// wedging the queue forever.
  void dispatchEntityMediaWrite(const Scraper::ScrapedItem &item, const QString &collectionUuid,
                                int collectionIndex, const QString &artworkDir,
                                const QString &baseName,
                                const QList<Scraper::PendingMediaWrite> &writes,
                                Scraper::RescrapeMode rescrapeMode, quint64 generation);
  /// Main-thread continuation of dispatchEntityMediaWrite: book the write
  /// result, wire the config art, and advance the queue.
  void onEntityMediaWriteFinished(const Scraper::ScrapedItem &item, const QString &collectionUuid,
                                  int collectionIndex, const Scraper::MediaWriteResult &res);
  /// Count one entity item done, advance the cursor, persist, and pump.
  void finishEntityItem();

  ScraperService *m_svc = nullptr;
};

} // namespace Scraper

#endif // ENTITYSCRAPECOORDINATOR_H
