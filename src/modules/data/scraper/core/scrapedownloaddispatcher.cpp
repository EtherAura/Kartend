#include "scrapedownloaddispatcher.h"

#include <QByteArray>
#include <QFile>
#include <QPointer>
#include <QUrl>

#include "errorutils.h"
#include "metadatalookupprovider.h"
#include "scrapeassetdedup.h"
#include "scrapelogging.h"

namespace Scraper {

ScrapeDownloadDispatcher::ScrapeDownloadDispatcher(QObject *parent) : QObject(parent) {}

void ScrapeDownloadDispatcher::dispatch(const QList<MediaAsset> &selected,
                                        const std::shared_ptr<QElapsedTimer> &applyTimer) {
  m_downloads.clear();
  m_downloads.reserve(selected.size());
  m_failedTypes.clear();
  m_bytes = 0;
  m_total = selected.size();
  m_pending = selected.size();
  m_loopDone = false;
  m_finishedEmitted = false;

  if (!m_config.provider) {
    // No provider wired — nothing can be fetched, so drain the pending count
    // ourselves and finish empty rather than hanging on a count nothing will
    // decrement.
    m_pending = 0;
    m_loopDone = true;
    emitFinishedIfDone();
    return;
  }

  // Fire every selected asset at once; Scraper::HttpClient enforces the
  // per-host concurrency cap + inter-start throttle, so parallel dispatch just
  // fills the available slots instead of serializing behind one reply.
  //
  // QPointer guard (Kartend-5g0g2): a fetch completion can fire long after the
  // dispatcher (and the dialog that owns it) is destroyed — cancel
  // mid-download. The QPointer goes null on destruction; the callback checks it
  // before touching `this`.
  QPointer<ScrapeDownloadDispatcher> guard(this);

  // CRC short-circuit context. Active only for FillMissing / UpdateChanged
  // re-scrapes; Overwrite always wants the bytes and Skip never reaches here.
  const bool crcEligible = !m_config.rescrapeArtworkDir.isEmpty() &&
                           !m_config.rescrapeBaseName.isEmpty() &&
                           (m_config.rescrapeMode == RescrapeMode::FillMissing ||
                            m_config.rescrapeMode == RescrapeMode::UpdateChanged);

  for (const auto &asset : selected) {
    // (1) Cross-collection dedup: if the file already lives in a sibling
    // collection's `_shared/`, read its bytes from disk and route them through
    // the same pipeline so a new collection is self-contained on first scrape
    // without re-paying bandwidth.
    const QString existing =
        ScrapeAssetDedup::findExistingSharedAsset(asset, m_config.sharedSearchPaths);
    if (!existing.isEmpty()) {
      qCInfo(lcScrapeTimings) << "DISPATCH dedup hit" << asset.type << asset.label << "<-"
                              << existing;
      QFile f(existing);
      QByteArray bytes;
      if (f.open(QIODevice::ReadOnly)) {
        bytes = f.readAll();
      }
      if (!bytes.isEmpty()) {
        m_bytes += bytes.size();
        m_downloads.append(PendingMediaWrite{asset, bytes});
        // Don't finish from inside the loop: finished()'s slot can delete the
        // owning dialog (and this dispatcher), and the next iteration would
        // deref freed state (Kartend-5g0g2). Just tally; the post-loop check
        // finishes exactly once after every asset is dispatched.
        --m_pending;
        continue;
      }
      // The dedup copy exists but couldn't be read (permissions, truncated to
      // zero bytes, vanishing mount). Resolving here would make the asset
      // vanish silently — neither downloaded nor in failedTypes — so fall
      // through to the network fetch instead; its error path records the
      // type if that fails too (Kartend-jjyst.17).
      qCWarning(lcScrapeTimings) << "DISPATCH dedup copy unreadable" << existing
                                 << "— falling back to network fetch for" << asset.type;
    }

    // (2) Per-game CRC short-circuit: append md5/sha1 hints to the URL when the
    // destination already exists. The server replies with a tiny "*OK" body
    // when the local file matches; isHashShortCircuit() then drops it (no
    // bytes added, existing file untouched).
    QUrl fetchUrl = asset.url;
    if (crcEligible) {
      const QString existingPerGame = ScrapeAssetDedup::findExistingPerGameAsset(
          asset, m_config.rescrapeArtworkDir, m_config.rescrapeBaseName);
      if (!existingPerGame.isEmpty()) {
        const ScrapeAssetDedup::LocalHashes hashes =
            ScrapeAssetDedup::hashLocalFile(existingPerGame);
        fetchUrl = ScrapeAssetDedup::withHashHints(asset.url, hashes);
        qCInfo(lcScrapeTimings) << "DISPATCH hash-hint" << asset.type << "from" << existingPerGame;
      }
    }

    // (3) Network fetch. Kind-aware entry point: the asset type selects the
    // provider's per-kind Content-Type prefix + response cap (Kartend-jjyst.1).
    m_config.provider->fetchMediaBytesForType(
        fetchUrl, asset.type, [guard, asset, applyTimer](ErrorUtils::Result<QByteArray> response) {
          if (guard.isNull()) return; // dispatcher gone — drop the reply
          if (applyTimer) {
            qCInfo(lcScrapeTimings)
                << "DISPATCH complete" << asset.type << asset.label << "ok=" << response.isOk()
                << "elapsed_total=" << applyTimer->elapsed() << "ms";
          }
          if (response.isOk()) {
            const QByteArray bytes = response.value();
            // Hash short-circuit hit — server confirmed the local file matches,
            // so don't persist it. The existing file stays put.
            if (ScrapeAssetDedup::isHashShortCircuit(bytes)) {
              qCInfo(lcScrapeTimings)
                  << "DISPATCH hash-hit (skip)" << asset.type << "body=" << bytes.trimmed();
            } else if (!bytes.isEmpty()) {
              guard->m_bytes += bytes.size();
              guard->m_downloads.append(PendingMediaWrite{asset, bytes});
            } else {
              // Ok-but-empty is a failed fetch, same as the batch aggregator's
              // "isOk && !isEmpty" success test (Kartend-jjyst.14).
              guard->m_failedTypes.append(asset.type.toLower());
            }
          } else {
            // The asset itself is skipped — partial success beats failing the
            // whole scrape because one asset 404'd — but the type is recorded
            // so the mediaAbsent merge keeps its marker instead of re-chasing
            // (and re-failing) the download every FillMissing run
            // (Kartend-jjyst.14).
            guard->m_failedTypes.append(asset.type.toLower());
          }
          --guard->m_pending;
          const int completed = guard->m_total - guard->m_pending;
          emit guard->progressed(completed, guard->m_total, guard->m_bytes);
          if (guard->m_pending <= 0 && applyTimer) {
            qCInfo(lcScrapeTimings) << "DISPATCH all done in" << applyTimer->elapsed() << "ms";
          }
          // LAST touch — the slot may delete this dispatcher (Kartend-5g0g2).
          guard->emitFinishedIfDone();
        });
  }

  // Loop done. For the all-dedup-hit case (every asset resolved synchronously,
  // no completion callback pending) this is where finished() fires. For the
  // async case m_pending is still > 0 here and the last callback finishes it.
  m_loopDone = true;
  emitFinishedIfDone();
}

void ScrapeDownloadDispatcher::emitFinishedIfDone() {
  if (!m_loopDone || m_pending > 0 || m_finishedEmitted) {
    return;
  }
  m_finishedEmitted = true;
  // LAST touch — the slot may delete this dispatcher (Kartend-5g0g2); no member
  // access after the emit.
  emit finished(m_downloads, m_bytes, m_failedTypes);
}

} // namespace Scraper
