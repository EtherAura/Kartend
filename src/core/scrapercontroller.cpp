// Implementation extracted from mainwindow_scraper.cpp (Kartend-hzef step 3).
// openScraperDialog + promptResumePendingScrapeIfAny moved verbatim with
// references rerouted from MainWindow members to m_ctx.<getter>() calls.
// The old file-local pickLookupProvider helper (build the FULL registry,
// then release one provider out of it) has been replaced by
// MetadataProviderRegistry::claimLookupProvider, which selects first and
// constructs only the chosen provider; makeProviderBuilder below is the
// shared closure factory for the dialog + service contexts.
#include "scrapercontroller.h"

#include <functional>
#include <memory>
#include <utility>

#include <QAbstractButton>
#include <QApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <Qt>
#include <QTimer>

#include "applicationcontext.h"
#include "artworkutils.h"
#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "collection/typehelpers.h"
#include "collection/validationhelpers.h"
#include "detailspanemanager.h"
#include "entityjobbuilder.h"
#include "idatabasemanager.h"
#include "interactionmanager.h"
#include "itemwidget.h"
#include "metadatalookupprovider.h"
#include "metadataproviderregistry.h"
#include "navigationmanager.h"
#include "pathutils.h"
#include "scrapepersistence.h"
#include "scraperesultdialog.h"
#include "scraperservice.h"
#include "scrollmanager.h"

namespace ScraperControllerInternal {

/// Shared per-collection provider builder for the dialog and service
/// contexts (openScraperDialog wires the same closure into both;
/// promptResumePendingScrapeIfAny needs an identical one before a resume).
/// Each call resolves the collection's scraper via
/// MetadataProviderRegistry::claimLookupProvider — override id first, else
/// first category match — which constructs ONLY the selected provider
/// instead of building the full registry and discarding the unused siblings
/// on every collection switch. The provider is still fresh per call: its
/// collection accessor closes over @p idx (per-collection systemeid / DAT
/// overrides are read through it), so it must not be reused for another
/// collection. Declared in scrapercontroller.h as a test seam.
std::function<std::shared_ptr<MetadataLookupProvider>(int)>
makeProviderBuilder(QList<CollectionConfig> *collections, GeneralSettings *generalSettings) {
  return [collections, generalSettings](int idx) -> std::shared_ptr<MetadataLookupProvider> {
    if (!CollectionUtils::isValidIndex(idx, collections)) return nullptr;
    return MetadataProviderRegistry::claimLookupProvider(
        (*collections)[idx],
        [generalSettings]() -> const GeneralSettings * { return generalSettings; },
        [collections, idx]() -> const CollectionConfig * {
          if (!CollectionUtils::isValidIndex(idx, collections)) return nullptr;
          return &(*collections)[idx];
        });
  };
}

} // namespace ScraperControllerInternal

ScraperController::ScraperController(QObject *parent)
    : QObject(parent), m_scraperService(std::make_unique<Scraper::ScraperService>(nullptr)) {
  connect(m_scraperService.get(), &Scraper::ScraperService::scrapeFinished, this,
          [this](const Scraper::ScraperService::Summary &) {
            emit scrapeRunFinished();
            // Kartend-ud6q2: a creation-time fetch requested while this run was
            // live has been waiting for the service to go idle. Drain on the
            // next event-loop turn rather than here — startScrape re-entered
            // from inside its own scrapeFinished emission would restart the
            // state machine while the finishing run is still unwinding it.
            QTimer::singleShot(0, this, [this]() { drainPendingBackgroundEntityScrape(); });
          });
}

ScraperController::~ScraperController() = default;

void ScraperController::setContext(const ScraperControllerContext &context) {
  m_ctx = context;
}

// Shared open-time setup for both the per-item scrape (openScraperDialog) and
// the entity scrape (openEntityScraperDialog): construct/reuse the single dialog
// instance, wire its completion handler once, and bind the live context +
// service. Returns the ready dialog, or nullptr if the DB isn't up yet — the
// caller then picks the start method (startUnifiedScrape vs startEntityScrape)
// and shows it.
ScrapeResultDialog *ScraperController::prepareScraperDialog() {
  QWidget *parent = m_ctx.getParentWindow ? m_ctx.getParentWindow() : nullptr;
  IDatabaseManager *db = m_ctx.getDatabaseManager ? m_ctx.getDatabaseManager() : nullptr;
  if (!db) {
    QMessageBox::warning(parent, tr("Scraper"), tr("Database is not ready."));
    return nullptr;
  }

  // Single reused dialog instance. The dialog's closeEvent override
  // hides instead of destroying when Unified mode is active, so we
  // keep the widget tree across opens. First call constructs;
  // subsequent calls just rebind context + raise.
  if (!m_scraperDialog) {
    m_scraperDialog = new ScrapeResultDialog(/*provider=*/nullptr, /*candidates=*/{}, parent);
    m_scraperDialog->setWindowFlag(Qt::Window, true);
    m_scraperDialog->setModal(false);
    // Post-completion housekeeping: refresh the active grid + sidebar +
    // artwork cache once a scrape ends, then surface a summary box. The
    // connect lives inside this construction block so it is made exactly
    // once per dialog instance — structurally impossible to stack a second
    // handler on a re-open (this replaces the old ad-hoc connected-yet bool
    // guard). Each controller wires the dialog it constructed, so a 2nd
    // controller still gets its own summary / grid refresh.
    QObject::connect(
        m_scraperDialog, &ScrapeResultDialog::unifiedScrapeFinished, this,
        [this](int scraped, int skipped, int errors, int notFound,
               const QStringList &firstFailures) {
          DetailsPaneManager *dpm =
              m_ctx.getDetailsPaneManager ? m_ctx.getDetailsPaneManager() : nullptr;
          ScrollManager *scroll = m_ctx.getScrollManager ? m_ctx.getScrollManager() : nullptr;
          InteractionManager *interaction =
              m_ctx.getInteractionManager ? m_ctx.getInteractionManager() : nullptr;
          if (dpm && scroll && interaction) {
            const int sel = interaction->currentSelectedIndex();
            if (sel >= 0) {
              ItemWidget *widgetPtr = scroll->getActiveWidgets().value(sel, nullptr);
              dpm->updateSidebarMetadata(widgetPtr);
            }
          }
          ArtworkUtils::clearDirectoryCache();
          NavigationManager *nav =
              m_ctx.getNavigationManager ? m_ctx.getNavigationManager() : nullptr;
          QList<CollectionConfig> *cols = m_ctx.getCollections ? m_ctx.getCollections() : nullptr;
          const int curIdx =
              m_ctx.getCurrentCollectionIndex ? m_ctx.getCurrentCollectionIndex() : -1;
          if (nav && CollectionUtils::isValidIndex(curIdx, cols)) {
            nav->safeReloadCollection(curIdx);
          }
          // Kartend-ud6q2: a creation-time entity fetch is silent by design.
          // The dialog is bound to the shared service for as long as it
          // exists, so it reports the end of a background run the user never
          // started — everything above (fresh artwork cache, reloaded grid and
          // sidebar) is still wanted, only the modal box is not. Cleared here
          // rather than on scrapeFinished because that handler runs BEFORE the
          // dialog re-emits, and would clear the flag before this reads it.
          if (std::exchange(m_backgroundEntityRunActive, false)) return;
          QString text =
              tr("Scrape complete.\n\nScraped: %1\nSkipped: %2\nNot found: %3\nErrors: %4")
                  .arg(scraped)
                  .arg(skipped)
                  .arg(notFound)
                  .arg(errors);
          if (!firstFailures.isEmpty()) {
            text += QStringLiteral("\n\n") +
                    tr("First failures:\n%1").arg(firstFailures.join(QChar('\n')));
          }
          QWidget *parentWindow = m_ctx.getParentWindow ? m_ctx.getParentWindow() : nullptr;
          // The unified scrape keeps running while its dialog is hidden, so
          // completion can land while the user is inside an unrelated modal
          // exec() loop (settings, bulk edit, kart preflight). An
          // application-modal summary would then spin its own nested exec() and
          // stack over that dialog in a surprising order (Kartend-ykidl). When a
          // modal is already up, surface the summary through a non-modal box
          // (WA_DeleteOnClose, non-blocking show()) so it waits for the user
          // instead of hijacking the active dialog; the input grab of the live
          // modal keeps the box inert until that dialog closes. With nothing
          // modal up, keep the familiar blocking summary.
          if (QApplication::activeModalWidget()) {
            auto *box = new QMessageBox(QMessageBox::Information, tr("Scraper"), text,
                                        QMessageBox::Ok, parentWindow);
            box->setAttribute(Qt::WA_DeleteOnClose);
            box->setModal(false);
            box->show();
            box->raise();
          } else {
            QMessageBox::information(parentWindow, tr("Scraper"), text);
          }
        });
  }
  ScrapeResultDialog *dialog = m_scraperDialog;

  QList<CollectionConfig> *collections = m_ctx.getCollections ? m_ctx.getCollections() : nullptr;
  GeneralSettings *generalSettings =
      m_ctx.getGeneralSettings ? m_ctx.getGeneralSettings() : nullptr;
  const ApplicationContext *appCtx =
      m_ctx.getApplicationContext ? m_ctx.getApplicationContext() : nullptr;

  ScrapeResultDialog::ScraperContext sctx;
  sctx.collections = collections;
  sctx.ctx = appCtx;
  sctx.generalSettings = generalSettings;
  // One builder for both contexts: bindServiceContext pushes it into the
  // long-lived service (so a resume on next launch can still build providers)
  // and hands it back for the dialog's copy.
  sctx.providerBuilder = bindServiceContext();
  sctx.applyResult = [this, collections,
                      generalSettings](int collectionIndex, const QString &filePath,
                                       const ScrapeResultDialog::Result &result) {
    if (!CollectionUtils::isValidIndex(collectionIndex, collections)) return;
    IDatabaseManager *innerDb = m_ctx.getDatabaseManager ? m_ctx.getDatabaseManager() : nullptr;
    if (!innerDb || !generalSettings) return;
    const CollectionConfig &cfg = (*collections)[collectionIndex];
    const QString expandedMediaDir = PathUtils::validateAndExpandPath(cfg.mediaDirectory, cfg.name);
    const QString uuid = CollectionUtils::computeCollectionUuid(cfg.name, expandedMediaDir);
    const QString artworkDir = PathUtils::validateAndExpandPath(cfg.artworkDirectory, cfg.name);
    const QString baseName = QFileInfo(filePath).completeBaseName();
    QList<Scraper::PendingMediaWrite> writes;
    writes.reserve(result.downloads.size());
    for (const auto &d : result.downloads) {
      Scraper::PendingMediaWrite w;
      w.asset = d.asset;
      w.bytes = d.bytes;
      writes.append(w);
    }
    const Scraper::RescrapeMode rescrapeMode =
        static_cast<Scraper::RescrapeMode>(generalSettings->scraper.options.rescrapeMode);
    (void)Scraper::applyScrapedItem(innerDb, uuid, filePath, artworkDir, baseName, result.item,
                                    writes, rescrapeMode);
  };
  // Rebind dialog context + service through the dialog's single open-time
  // entry point. bindForOpen is idempotent — no connection it (transitively)
  // makes can stack across re-opens.
  dialog->bindForOpen(sctx, m_scraperService.get());
  return dialog;
}

std::function<std::shared_ptr<MetadataLookupProvider>(int)>
ScraperController::bindServiceContext() {
  QList<CollectionConfig> *collections = m_ctx.getCollections ? m_ctx.getCollections() : nullptr;
  if (!collections) return {};
  GeneralSettings *generalSettings =
      m_ctx.getGeneralSettings ? m_ctx.getGeneralSettings() : nullptr;
  const ApplicationContext *appCtx =
      m_ctx.getApplicationContext ? m_ctx.getApplicationContext() : nullptr;

  Scraper::ScraperService::Context srvCtx;
  srvCtx.ctx = appCtx;
  srvCtx.generalSettings = generalSettings;
  srvCtx.collections = collections;
  srvCtx.providerBuilder =
      ScraperControllerInternal::makeProviderBuilder(collections, generalSettings);
  m_scraperService->setContext(srvCtx);
  return srvCtx.providerBuilder;
}

void ScraperController::openScraperDialog(int preCollectionIndex, const QString &preItemPath) {
  ScrapeResultDialog *dialog = prepareScraperDialog();
  if (!dialog) return;
  // A run the user started themselves always reports its summary, even if a
  // silent background fetch left the flag set (no dialog existed to consume
  // it at the time).
  m_backgroundEntityRunActive = false;
  dialog->startUnifiedScrape(preCollectionIndex, preItemPath);
  dialog->show();
  dialog->raise();
  dialog->activateWindow();
}

void ScraperController::openEntityScraperDialog(int collectionIndex) {
  ScrapeResultDialog *dialog = prepareScraperDialog();
  if (!dialog) return;
  m_backgroundEntityRunActive = false; // user-initiated: show its summary
  // Don't surface an empty dialog when the collection has no entity-capable
  // scraper — startEntityScrape shows the reason and returns false.
  if (!dialog->startEntityScrape(collectionIndex)) return;
  dialog->show();
  dialog->raise();
  dialog->activateWindow();
}

void ScraperController::startBackgroundEntityScrape(const QList<int> &collectionIndices) {
  if (collectionIndices.isEmpty()) return;
  for (int index : collectionIndices) {
    if (index >= 0 && !m_pendingBackgroundEntityIndices.contains(index)) {
      m_pendingBackgroundEntityIndices.append(index);
    }
  }
  drainPendingBackgroundEntityScrape();
}

void ScraperController::drainPendingBackgroundEntityScrape() {
  if (m_pendingBackgroundEntityIndices.isEmpty()) return;
  if (!m_scraperService || m_scraperService->isActive()) return;
  // Hold while a modal dialog is up. Two reasons, both load-bearing:
  //
  //  * The settings dialog emits collectionSaved for a new collection while
  //    it is still open, so this fires mid-edit — and the collection list can
  //    keep mutating under a live run.
  //  * Its own creation-time "fetch collection info" opt-in (Kartend-445su)
  //    launches a VISIBLE entity scrape when the dialog closes. Starting
  //    first would make that explicit request collide with this implicit one
  //    and greet the user with "a scrape is already running". Letting the
  //    user's own request go first costs nothing: by the time this drains,
  //    the art has landed and the re-check below drops the request.
  if (QApplication::activeModalWidget()) {
    if (!m_backgroundDrainRetryArmed) {
      m_backgroundDrainRetryArmed = true;
      // Polled rather than signal-driven: a modal can be closed from anywhere
      // (settings, import wizard, a message box), and there is no one signal
      // that means "the last modal went away" to hang this on.
      QTimer::singleShot(kBackgroundDrainRetryMs, this, [this]() {
        m_backgroundDrainRetryArmed = false;
        drainPendingBackgroundEntityScrape();
      });
    }
    return;
  }
  // No database, no persistence sink for the scraped metadata rows. Unlike
  // the dialog paths this one has no window to warn through — and warning
  // would break the silence anyway — so hold the request instead. The next
  // drain (a later creation, or the end of another run) retries once the DB
  // is up. Same "absent getter counts as no database" reading as
  // prepareScraperDialog.
  IDatabaseManager *db = m_ctx.getDatabaseManager ? m_ctx.getDatabaseManager() : nullptr;
  if (!db) return;

  QList<CollectionConfig> *collections = m_ctx.getCollections ? m_ctx.getCollections() : nullptr;
  if (!collections) return;
  const auto providerBuilder = bindServiceContext();
  if (!providerBuilder) return;

  // Take the whole pending set now: indices that build no jobs are dropped
  // rather than retried forever, and a collection deleted between request and
  // drain simply falls out of range.
  const QList<int> indices = std::exchange(m_pendingBackgroundEntityIndices, {});
  QList<Scraper::ScraperService::CollectionJob> queue;
  for (int index : indices) {
    if (!CollectionUtils::isValidIndex(index, collections)) continue;
    const CollectionConfig &cfg = (*collections)[index];
    // Re-checked here, not only when the request was made: anything that
    // landed art meanwhile — the settings dialog's own opt-in fetch, a
    // hand-picked icon, an earlier queued job for the same collection — makes
    // this request redundant, and dropping it is how the implicit fetch stays
    // out of the way of the explicit one.
    if (!cfg.collectionIcon.isEmpty() || !cfg.background.headerLogoImage.isEmpty()) continue;
    const QString expandedMediaDir = PathUtils::validateAndExpandPath(cfg.mediaDirectory, cfg.name);
    const QString uuid = CollectionUtils::computeCollectionUuid(cfg.name, expandedMediaDir);
    const QString artworkDir = PathUtils::validateAndExpandPath(cfg.artworkDirectory, cfg.name);
    queue.append(Scraper::buildEntityJobs(*collections, index, uuid, artworkDir, providerBuilder));
  }
  if (queue.isEmpty()) return;

  // Auto mode, all entity media, write metadata — the same shape the dialog's
  // one-shot entity scrape uses, so the two routes land identical art.
  m_backgroundEntityRunActive = true;
  if (!m_scraperService->startScrape(queue, Scraper::ScraperService::Mode::Auto,
                                     /*mediaFilter=*/{}, /*writeMetadata=*/true)) {
    // Refused — the service went active between the idle check and here.
    // Put the indices back, ahead of anything that arrived meanwhile, so the
    // end of that run picks them up in request order.
    m_backgroundEntityRunActive = false;
    QList<int> restored = indices;
    for (int index : std::as_const(m_pendingBackgroundEntityIndices)) {
      if (!restored.contains(index)) restored.append(index);
    }
    m_pendingBackgroundEntityIndices = restored;
  }
}

void ScraperController::promptResumePendingScrapeIfAny() {
  if (!m_scraperService) return;
  auto pending = m_scraperService->loadPendingState(/*consumeOnLoad=*/false);
  if (!pending.isValid()) return;

  QList<CollectionConfig> *collections = m_ctx.getCollections ? m_ctx.getCollections() : nullptr;
  GeneralSettings *generalSettings =
      m_ctx.getGeneralSettings ? m_ctx.getGeneralSettings() : nullptr;
  const ApplicationContext *appCtx =
      m_ctx.getApplicationContext ? m_ctx.getApplicationContext() : nullptr;

  // Wire the service context first so a resume (silent or accepted)
  // can build providers + persist progress. Same context as
  // openScraperDialog — kept in sync because both paths run on
  // MainWindow's live closures.
  Scraper::ScraperService::Context srvCtx;
  srvCtx.ctx = appCtx;
  srvCtx.generalSettings = generalSettings;
  srvCtx.collections = collections;
  srvCtx.providerBuilder =
      ScraperControllerInternal::makeProviderBuilder(collections, generalSettings);
  m_scraperService->setContext(srvCtx);

  // Auto-resume is gated by ScraperOptions::scrapeAutoResume.
  // Off by default — first-time users see the modal Resume / Discard prompt
  // below and learn the recovery path. Power users running unattended
  // overnight batches flip it on so a crash + relaunch self-heals without a
  // dialog blocking the resume.
  const bool autoResume = generalSettings && generalSettings->scraper.options.scrapeAutoResume;
  if (autoResume) {
    // Discard the state we already loaded above rather than loading it a second
    // time with consumeOnLoad — the re-read's value was thrown away and, if the
    // file changed between the two reads, consume would delete a different state
    // than the one being resumed. Mirrors the manual Resume branch below
    // (Kartend-h2736).
    m_scraperService->discardPendingState();
    m_scraperService->resumeFromState(pending);
    openScraperDialog();
    return;
  }
  const int remaining = pending.totalRemaining();
  const QString started =
      QDateTime::fromMSecsSinceEpoch(pending.startedAtUnixMs).toString(Qt::TextDate);
  QWidget *parent = m_ctx.getParentWindow ? m_ctx.getParentWindow() : nullptr;
  auto *box = new QMessageBox(parent);
  box->setAttribute(Qt::WA_DeleteOnClose);
  box->setWindowTitle(tr("Resume scrape?"));
  box->setIcon(QMessageBox::Question);
  box->setText(tr("An interrupted scrape from %1 was found.").arg(started));
  box->setInformativeText(tr("Scraped so far: %1\nSkipped: %2\nNot found: %3\nErrors: %4\n"
                             "Remaining items: %5")
                              .arg(pending.summarySoFar.scraped)
                              .arg(pending.summarySoFar.skipped)
                              .arg(pending.summarySoFar.notFound)
                              .arg(pending.summarySoFar.errors)
                              .arg(remaining));
  auto *resumeBtn = box->addButton(tr("Resume"), QMessageBox::AcceptRole);
  auto *discardBtn = box->addButton(tr("Discard"), QMessageBox::DestructiveRole);
  box->addButton(tr("Keep for later"), QMessageBox::RejectRole);
  box->setDefaultButton(resumeBtn);
  connect(box, &QMessageBox::buttonClicked, this,
          [this, box, resumeBtn, discardBtn, pending](QAbstractButton *clicked) {
            if (clicked == resumeBtn) {
              m_scraperService->discardPendingState();
              m_scraperService->resumeFromState(pending);
              openScraperDialog();
            } else if (clicked == discardBtn) {
              m_scraperService->discardPendingState();
            }
            box->deleteLater();
          });
  box->open();
}
