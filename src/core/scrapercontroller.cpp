// Implementation extracted from mainwindow_scraper.cpp (Kartend-hzef step 3).
// openScraperDialog + promptResumePendingScrapeIfAny moved verbatim with
// references rerouted from MainWindow members to m_ctx.<getter>() calls.
// pickLookupProvider (file-local helper) moved into the anonymous namespace
// below.
#include "scrapercontroller.h"

#include <memory>
#include <vector>

#include <QAbstractButton>
#include <QDateTime>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <Qt>

#include "applicationcontext.h"
#include "artworkutils.h"
#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "collection/typehelpers.h"
#include "collection/validationhelpers.h"
#include "detailspanemanager.h"
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

namespace {

/// Resolve the metadata-lookup provider for a collection scrape. An
/// explicit `CollectionConfig::scraperProviderId` override wins; with no
/// override (or one that doesn't resolve to a lookup-capable provider)
/// the first provider whose category matches the collection `type` is
/// used. Ownership of the chosen provider is released out of @p registry
/// into the returned shared_ptr; returns null when nothing usable matches.
std::shared_ptr<MetadataLookupProvider>
pickLookupProvider(std::vector<std::unique_ptr<MetadataProvider>> &registry,
                   const CollectionConfig &cfg) {
  auto claim = [&registry](MetadataProvider *p) -> std::shared_ptr<MetadataLookupProvider> {
    if (!p || !p->capabilities().testFlag(MetadataProvider::Capability::MetadataLookup)) {
      return nullptr;
    }
    auto *typed = dynamic_cast<MetadataLookupProvider *>(p);
    if (!typed) return nullptr;
    for (auto &up : registry) {
      if (up.get() == p) {
        up.release();
        break;
      }
    }
    return std::shared_ptr<MetadataLookupProvider>(typed);
  };

  const QString overrideId = cfg.scraperOverrides.scraperProviderId.trimmed();
  if (!overrideId.isEmpty()) {
    for (auto &up : registry) {
      if (up && up->id() == overrideId) {
        if (auto provider = claim(up.get())) return provider;
        break;
      }
    }
  }

  const auto applicable = MetadataProviderRegistry::forCategory(registry, cfg.type);
  for (auto &up : registry) {
    if (up && applicable.contains(up.get())) {
      if (auto provider = claim(up.get())) return provider;
    }
  }
  return nullptr;
}

} // namespace

ScraperController::ScraperController(QObject *parent)
    : QObject(parent), m_scraperService(std::make_unique<Scraper::ScraperService>(nullptr)) {}

ScraperController::~ScraperController() = default;

void ScraperController::setContext(const ScraperControllerContext &context) {
  m_ctx = context;
}

void ScraperController::openScraperDialog(int preCollectionIndex, const QString &preItemPath) {
  QWidget *parent = m_ctx.getParentWindow ? m_ctx.getParentWindow() : nullptr;
  IDatabaseManager *db = m_ctx.getDatabaseManager ? m_ctx.getDatabaseManager() : nullptr;
  if (!db) {
    QMessageBox::warning(parent, tr("Scraper"), tr("Database is not ready."));
    return;
  }

  // Single reused dialog instance. The dialog's closeEvent override
  // hides instead of destroying when Unified mode is active, so we
  // keep the widget tree across opens. First call constructs;
  // subsequent calls just rebind context + raise.
  if (!m_scraperDialog) {
    m_scraperDialog = new ScrapeResultDialog(/*provider=*/nullptr, /*candidates=*/{}, parent);
    m_scraperDialog->setWindowFlag(Qt::Window, true);
    m_scraperDialog->setModal(false);
  }
  auto *dialog = m_scraperDialog;

  QList<CollectionConfig> *collections = m_ctx.getCollections ? m_ctx.getCollections() : nullptr;
  GeneralSettings *generalSettings =
      m_ctx.getGeneralSettings ? m_ctx.getGeneralSettings() : nullptr;
  const ApplicationContext *appCtx =
      m_ctx.getApplicationContext ? m_ctx.getApplicationContext() : nullptr;

  ScrapeResultDialog::ScraperContext sctx;
  sctx.collections = collections;
  sctx.ctx = appCtx;
  sctx.generalSettings = generalSettings;
  sctx.providerBuilder = [collections,
                          generalSettings](int idx) -> std::shared_ptr<MetadataLookupProvider> {
    if (!CollectionUtils::isValidIndex(idx, collections)) return nullptr;
    auto registry = MetadataProviderRegistry::builtIn(
        [generalSettings]() -> const GeneralSettings * { return generalSettings; },
        [collections, idx]() -> const CollectionConfig * {
          if (!CollectionUtils::isValidIndex(idx, collections)) return nullptr;
          return &(*collections)[idx];
        });
    return pickLookupProvider(registry, (*collections)[idx]);
  };
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
  dialog->setScraperContext(sctx);

  // Bind the long-lived service to the dialog. The service is owned by
  // this controller (constructed in the ctor); we configure it here with
  // the same builder so a resume on next launch can still build
  // providers, then bind to the dialog.
  Scraper::ScraperService::Context srvCtx;
  srvCtx.ctx = appCtx;
  srvCtx.generalSettings = generalSettings;
  srvCtx.collections = collections;
  srvCtx.providerBuilder = sctx.providerBuilder;
  m_scraperService->setContext(srvCtx);
  dialog->setScraperService(m_scraperService.get());

  // Post-completion housekeeping: refresh the active grid + sidebar +
  // artwork cache once the scrape ends, then surface a summary box. The dialog
  // is reused across opens, so guard the connect to attach the lambda exactly
  // once per controller — otherwise one scrape-finish would pop N message
  // boxes. The guard is per-instance (not a process-wide static) so a 2nd
  // controller still connects its own dialog (Kartend-r2722).
  if (!m_unifiedFinishedConnected) {
    m_unifiedFinishedConnected = true;
    QObject::connect(
        dialog, &ScrapeResultDialog::unifiedScrapeFinished, this,
        [this](int scraped, int skipped, int errors, const QStringList &firstFailures) {
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
          QString text = tr("Scrape complete.\n\nScraped: %1\nSkipped: %2\nErrors: %3")
                             .arg(scraped)
                             .arg(skipped)
                             .arg(errors);
          if (!firstFailures.isEmpty()) {
            text += QStringLiteral("\n\n") +
                    tr("First failures:\n%1").arg(firstFailures.join(QChar('\n')));
          }
          QWidget *parentWindow = m_ctx.getParentWindow ? m_ctx.getParentWindow() : nullptr;
          QMessageBox::information(parentWindow, tr("Scraper"), text);
        });
  }

  dialog->startUnifiedScrape(preCollectionIndex, preItemPath);
  dialog->show();
  dialog->raise();
  dialog->activateWindow();
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
  srvCtx.providerBuilder = [collections,
                            generalSettings](int idx) -> std::shared_ptr<MetadataLookupProvider> {
    if (!CollectionUtils::isValidIndex(idx, collections)) return nullptr;
    auto registry = MetadataProviderRegistry::builtIn(
        [generalSettings]() -> const GeneralSettings * { return generalSettings; },
        [collections, idx]() -> const CollectionConfig * {
          if (!CollectionUtils::isValidIndex(idx, collections)) return nullptr;
          return &(*collections)[idx];
        });
    return pickLookupProvider(registry, (*collections)[idx]);
  };
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
  box->setInformativeText(tr("Scraped so far: %1\nSkipped: %2\nErrors: %3\n"
                             "Remaining items: %4")
                              .arg(pending.summarySoFar.scraped)
                              .arg(pending.summarySoFar.skipped)
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
