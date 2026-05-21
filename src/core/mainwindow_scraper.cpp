// MainWindow's scraper-service + marquee lifecycle entry points.
//
// Extracted from mainwindow_wiring.cpp during the responsibility-based TU
// split. Owns:
//
//   * openScraperDialog() — long-lived ScrapeResultDialog construction +
//     ScraperService binding + post-completion housekeeping handler. Routed
//     to from the File → Scraper menu action and the right-click context
//     menu via IMainWindow::openScraperDialog.
//   * promptResumePendingScrapeIfAny() — startup resume flow that surfaces
//     a non-blocking Resume / Discard / Keep prompt for a crash-recovered
//     pending-scrape state file.
//   * pickLookupProvider() — anonymous-namespace helper that resolves a
//     CollectionConfig's explicit `scraperProviderId` override OR the first
//     category match for its `type`, releases the chosen provider out of a
//     MetadataProviderRegistry vector, and hands back a shared_ptr.
//   * applyMarqueeSettings() / updateMarqueeArtwork() — thin shims that
//     forward to the owned MarqueeController so the IMainWindow override
//     points stay on MainWindow without dragging the secondary-monitor
//     window logic into the same TU as the scan-event handlers.

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

#include "artworkutils.h"
#include "collectionutils.h"
#include "databasemanager.h"
#include "detailspanemanager.h"
#include "interactionmanager.h"
#include "itemwidget.h"
#include "mainwindow.h"
#include "marqueecontroller.h"
#include "metadatalookupprovider.h"
#include "metadataproviderregistry.h"
#include "navigationmanager.h"
#include "pathutils.h"
#include "scraperesultdialog.h"
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
  // Claim a provider out of the registry: verify it does metadata
  // lookups, release its unique_ptr, hand back a shared_ptr. Returns
  // null (registry untouched) when the provider can't do lookups.
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

  // Explicit per-collection override takes precedence.
  const QString overrideId = cfg.scraperOverrides.scraperProviderId.trimmed();
  if (!overrideId.isEmpty()) {
    for (auto &up : registry) {
      if (up && up->id() == overrideId) {
        if (auto provider = claim(up.get())) return provider;
        break; // ids are unique — a non-lookup match falls through to type
      }
    }
  }

  // Fall back to the first category match for the collection type.
  const auto applicable = MetadataProviderRegistry::forCategory(registry, cfg.type);
  for (auto &up : registry) {
    if (up && applicable.contains(up.get())) {
      if (auto provider = claim(up.get())) return provider;
    }
  }
  return nullptr;
}

} // namespace

void MainWindow::applyMarqueeSettings() {
  if (m_marqueeController) {
    m_marqueeController->applyMarqueeSettings();
  }
}

void MainWindow::updateMarqueeArtwork() {
  if (m_marqueeController) {
    m_marqueeController->updateMarqueeArtwork();
  }
}

void MainWindow::openScraperDialog(int preCollectionIndex, const QString &preItemPath) {
  if (!getDatabaseManager()) {
    QMessageBox::warning(this, tr("Scraper"), tr("Database is not ready."));
    return;
  }

  // Single reused dialog instance. The dialog's closeEvent override
  // hides instead of destroying when Unified mode is active, so we
  // keep the widget tree across opens. First call constructs;
  // subsequent calls just rebind context + raise.
  if (!m_scraperDialog) {
    m_scraperDialog = new ScrapeResultDialog(/*provider=*/nullptr, /*candidates=*/{}, this);
    m_scraperDialog->setWindowFlag(Qt::Window, true);
    // Unified flow is non-modal — the user must be able to keep
    // using the main window while a scrape runs in the background.
    m_scraperDialog->setModal(false);
  }
  auto *dialog = m_scraperDialog;

  ScrapeResultDialog::ScraperContext sctx;
  sctx.collections = &m_collections;
  sctx.databaseManager = getDatabaseManager();
  sctx.generalSettings = &m_generalSettings;
  // Provider builder: per-collection-index lookup that hands the
  // dialog a fresh shared_ptr<MetadataLookupProvider>. Each call
  // rebuilds the registry with the live accessor closures so SS
  // sees current credentials + the right systemeid override.
  sctx.providerBuilder = [this](int idx) -> std::shared_ptr<MetadataLookupProvider> {
    if (!CollectionUtils::isValidIndex(idx, &m_collections)) return nullptr;
    auto registry = MetadataProviderRegistry::builtIn(
        [this]() -> const GeneralSettings * { return &m_generalSettings; },
        [this, idx]() -> const CollectionConfig * {
          if (!CollectionUtils::isValidIndex(idx, &m_collections)) return nullptr;
          return &m_collections[idx];
        });
    return pickLookupProvider(registry, m_collections[idx]);
  };
  // applyResult: post-Apply persistence hook for the unified
  // interactive flow. Mirrors the existing single-item right-click
  // path's Scraper::applyScrapedItem call so metadata + media writes
  // hit disk identically.
  sctx.applyResult = [this](int collectionIndex, const QString &filePath,
                            const ScrapeResultDialog::Result &result) {
    if (!CollectionUtils::isValidIndex(collectionIndex, &m_collections)) return;
    if (!getDatabaseManager()) return;
    const CollectionConfig &cfg = m_collections[collectionIndex];
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
        static_cast<Scraper::RescrapeMode>(m_generalSettings.scraperOptions.rescrapeMode);
    (void)Scraper::applyScrapedItem(getDatabaseManager(), uuid, filePath, artworkDir, baseName,
                                    result.item, writes, rescrapeMode);
  };
  dialog->setScraperContext(sctx);
  // Hand the long-lived service to the dialog. The service was
  // constructed in MainWindow's ctor; we configure it here with the
  // same builder so a resume on next launch can still build
  // providers, then bind to the dialog. The dialog reads
  // m_service->isActive() in startUnifiedScrape and lands directly
  // in the Live view when there's already a run in progress.
  Scraper::ScraperService::Context srvCtx;
  srvCtx.databaseManager = getDatabaseManager();
  srvCtx.generalSettings = &m_generalSettings;
  srvCtx.collections = &m_collections;
  srvCtx.providerBuilder = sctx.providerBuilder;
  m_scraperService->setContext(srvCtx);
  dialog->setScraperService(m_scraperService.get());

  // Post-completion housekeeping: refresh the active grid + sidebar +
  // artwork cache once the scrape ends, then surface a summary box.
  // UniqueConnection prevents the lambda from being attached multiple
  // times if the user opens the dialog repeatedly — without it, one
  // scrape-finish would pop N message boxes.
  static bool s_unifiedFinishedConnected = false;
  if (!s_unifiedFinishedConnected) {
    s_unifiedFinishedConnected = true;
    QObject::connect(
        dialog, &ScrapeResultDialog::unifiedScrapeFinished, this,
        [this](int scraped, int skipped, int errors, const QStringList &firstFailures) {
          if (getDetailsPaneManager() && getScrollManager() && getInteractionManager()) {
            const int sel = getInteractionManager()->currentSelectedIndex();
            if (sel >= 0) {
              ItemWidget *widgetPtr = getScrollManager()->getActiveWidgets().value(sel, nullptr);
              getDetailsPaneManager()->updateSidebarMetadata(widgetPtr);
            }
          }
          ArtworkUtils::clearDirectoryCache();
          if (getNavigationManager() &&
              CollectionUtils::isValidIndex(currentCollectionIndex, &m_collections)) {
            getNavigationManager()->safeReloadCollection(currentCollectionIndex);
          }
          QString text = tr("Scrape complete.\n\nScraped: %1\nSkipped: %2\nErrors: %3")
                             .arg(scraped)
                             .arg(skipped)
                             .arg(errors);
          if (!firstFailures.isEmpty()) {
            text += QStringLiteral("\n\n") +
                    tr("First failures:\n%1").arg(firstFailures.join(QChar('\n')));
          }
          QMessageBox::information(this, tr("Scraper"), text);
        });
  }

  dialog->startUnifiedScrape(preCollectionIndex, preItemPath);
  dialog->show();
  dialog->raise();
  dialog->activateWindow();
}

void MainWindow::promptResumePendingScrapeIfAny() {
  if (!m_scraperService) return;
  auto pending = m_scraperService->loadPendingState(/*consumeOnLoad=*/false);
  if (!pending.isValid()) return;
  // Wire the service context first so a resume (silent or accepted)
  // can build providers + persist progress. Same context as
  // openScraperDialog — kept in sync because both paths run on
  // MainWindow's live closures.
  Scraper::ScraperService::Context srvCtx;
  srvCtx.databaseManager = getDatabaseManager();
  srvCtx.generalSettings = &m_generalSettings;
  srvCtx.collections = &m_collections;
  srvCtx.providerBuilder = [this](int idx) -> std::shared_ptr<MetadataLookupProvider> {
    if (!CollectionUtils::isValidIndex(idx, &m_collections)) return nullptr;
    auto registry = MetadataProviderRegistry::builtIn(
        [this]() -> const GeneralSettings * { return &m_generalSettings; },
        [this, idx]() -> const CollectionConfig * {
          if (!CollectionUtils::isValidIndex(idx, &m_collections)) return nullptr;
          return &m_collections[idx];
        });
    return pickLookupProvider(registry, m_collections[idx]);
  };
  m_scraperService->setContext(srvCtx);

  // Auto-resume is gated by GeneralSettings::ScraperOptions::scrapeAutoResume.
  // Off by default — first-time users see the modal Resume / Discard prompt
  // below and learn the recovery path. Power users running unattended
  // overnight batches flip it on so a crash + relaunch self-heals without a
  // dialog blocking the resume.
  const bool autoResume = m_generalSettings.scraperOptions.scrapeAutoResume;
  if (autoResume) {
    m_scraperService->loadPendingState(/*consumeOnLoad=*/true); // delete file
    m_scraperService->resumeFromState(pending);
    // Also pop the Scraper window so the user has the Live view +
    // Close / Cancel buttons available without hunting for the menu.
    openScraperDialog();
    return;
  }
  const int remaining = pending.totalRemaining();
  const QString started =
      QDateTime::fromMSecsSinceEpoch(pending.startedAtUnixMs).toString(Qt::TextDate);
  // Non-blocking prompt — using open() + buttonClicked instead of
  // exec() keeps the event loop spinning so the collection grid can
  // finish painting / loading while the user reads the prompt.
  // exec() would freeze startup behind the modal, which on large
  // scrape jobs is exactly when the user wants the grid visible
  // first.
  auto *box = new QMessageBox(this);
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
  // Capture pending by value — the state struct is cheap enough and
  // we already paid for the parse. Avoids re-parsing the file in the
  // Resume branch.
  connect(box, &QMessageBox::buttonClicked, this,
          [this, box, resumeBtn, discardBtn, pending](QAbstractButton *clicked) {
            if (clicked == resumeBtn) {
              m_scraperService->discardPendingState();
              m_scraperService->resumeFromState(pending);
              openScraperDialog();
            } else if (clicked == discardBtn) {
              m_scraperService->discardPendingState();
            }
            // "Keep for later" leaves the file in place — next launch re-prompts.
            box->deleteLater();
          });
  box->open();
}
