// Modal review dialog for scrape results. Provider-agnostic — drives
// the picked MetadataLookupProvider for detail + media downloads, so
// adding new providers (ScreenScraper, TMDB, Open Library) doesn't
// touch this file.
#include "scraperesultdialog.h"

#include "applicationcontext.h"
#include "flowlayout.h"
#include "mediatypecheckboxbuilder.h"
#include "result/batchprogressview.h"
#include "result/singleitemview.h"
#include "scraperesultdialogunified.h"

#include <limits>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHideEvent>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QPair>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSet>
#include <QShowEvent>
#include <QSplitter>
#include <QtConcurrent/QtConcurrentRun>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrlQuery>

// Shares the name "kartend.scrape.timings" with httpclient.cpp's
// category so QT_LOGGING_RULES toggles both at once. Each TU keeps
// its own static instance (Qt's logging registry dedupes by name).
// QtInfoMsg default so qCInfo lines emit by default.
Q_LOGGING_CATEGORY(lcDialogTimings, "kartend.scrape.timings", QtWarningMsg)
#include <QStackedWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>

#include "extensionutils.h"
#include "idatabasemanager.h"
#include "imagedecodeutils.h"
#include "metadatalookupprovider.h"
#include "pathutils.h"
#include "scrapejobgrouping.h"

namespace {

// Default size — modest baseline; dialog reflows responsively when
// the user resizes (narrower → fewer chips per row, wider → more).
constexpr int DIALOG_WIDTH = 900;
constexpr int DIALOG_HEIGHT = 780;

// FlowLayout extracted to flowlayout.{h,cpp} (Kartend-3fkz step 1).

QString renderDetailHtml(const Scraper::ScrapedItem &item) {
  // Keep the markup minimal — no inline styles, palette-aware via Qt's
  // text rendering. Field rows skipped when empty so the panel stays
  // honest about what the provider returned.
  QString html = QStringLiteral("<h3>%1</h3>").arg(item.title.toHtmlEscaped());
  auto row = [&](const QString &label, const QString &value) {
    if (!value.trimmed().isEmpty()) {
      html +=
          QStringLiteral("<p><b>%1:</b> %2</p>").arg(label.toHtmlEscaped(), value.toHtmlEscaped());
    }
  };
  row(QObject::tr("Publisher"), item.publisher);
  row(QObject::tr("Released"), item.releaseDate);
  row(QObject::tr("Genre"), item.genre);
  row(QObject::tr("Developer"), item.developer);
  if (item.runtimeSeconds > 0) {
    row(QObject::tr("Runtime"), QString::number(item.runtimeSeconds) + QStringLiteral("s"));
  }
  if (!item.description.trimmed().isEmpty()) {
    html += QStringLiteral("<p>%1</p>").arg(item.description.toHtmlEscaped());
  }
  if (!item.customFields.isEmpty()) {
    html += QStringLiteral("<p style='color:gray'><b>%1:</b></p><ul>").arg(QObject::tr("Other"));
    for (auto it = item.customFields.constBegin(); it != item.customFields.constEnd(); ++it) {
      html += QStringLiteral("<li><b>%1:</b> %2</li>")
                  .arg(it.key().toHtmlEscaped(), it.value().toHtmlEscaped());
    }
    html += QStringLiteral("</ul>");
  }
  return html;
}

} // namespace

ScrapeResultDialog::ScrapeResultDialog(MetadataLookupProvider *provider,
                                       QList<Scraper::ScrapeCandidate> candidates, QWidget *parent)
    : QDialog(parent), m_provider(provider), m_candidates(std::move(candidates)),
      m_unified(std::make_unique<ScrapeResultDialogUnified>(this)) {
  setWindowTitle(tr("Scraper"));
  setModal(true);
  resize(DIALOG_WIDTH, DIALOG_HEIGHT);
  // Hard floor so the dialog stays usable on low-res screens, but
  // well below the preferred size so the user can shrink the window
  // and the FlowLayout-based metadata chips wrap accordingly. The
  // user can still drag-resize larger; we just clamp the lower edge.
  setMinimumSize(640, 520);
  buildUi();

  for (const auto &c : m_candidates) {
    auto *item = new QListWidgetItem(m_singleItemView->candidateList());
    QString label = c.displayName;
    if (!c.subtitle.isEmpty()) {
      label += QStringLiteral("\n  ") + c.subtitle;
    }
    if (c.matchScore >= 0) {
      label += QStringLiteral("  (%1)").arg(c.matchScore);
    }
    item->setText(label);
  }
  if (!m_candidates.isEmpty()) {
    m_singleItemView->candidateList()->setCurrentRow(0);
  }
  // ScreenScraper's jeuInfos.php returns exactly one matched game per
  // request — the candidate list ends up with one entry and the user
  // has nothing to choose between. Hide the panel entirely in that
  // case to claim the horizontal room for the description / media
  // checkboxes. Multi-candidate providers (MusicBrainz / OpenLibrary
  // / TMDB) keep the list since picking between candidates is the
  // whole point of their search response.
  if (m_singleItemView->candidateList() && m_candidates.size() <= 1) {
    m_singleItemView->candidateList()->hide();
  }

  // Provider health probe — fired once on dialog open. Default
  // implementation is a noop, so non-SS providers stay quiet. The
  // QPointer guard handles the case where the user dismisses the
  // dialog before the async probe lands.
  if (m_provider) {
    QPointer<ScrapeResultDialog> guard(this);
    m_provider->fetchHealthStatus([guard](MetadataLookupProvider::HealthStatus status) {
      if (guard.isNull()) return;
      if (status.humanStatus.isEmpty() && !status.refuseScrape) return;
      guard->m_singleItemView->healthLabel()->setText(status.humanStatus);
      guard->m_singleItemView->healthLabel()->show();
      if (status.refuseScrape) {
        guard->m_healthBlocksApply = true;
        // Even when a candidate gets selected later,
        // onCandidateSelected re-checks m_healthBlocksApply before
        // re-enabling Apply. The toolTip points the user at what
        // the Apply gate is waiting on.
        guard->m_applyButton->setEnabled(false);
        guard->m_applyButton->setToolTip(status.humanStatus);
      }
    });
  }
}

ScrapeResultDialog::~ScrapeResultDialog() = default;

namespace {
// Refcounted across every live instance — the main window queries
// this via isAnyInstanceVisible() to gate item-selection input while
// the user is mid-scrape.
int g_visibleInstanceCount = 0;
} // namespace

bool ScrapeResultDialog::isAnyInstanceVisible() {
  return g_visibleInstanceCount > 0;
}

void ScrapeResultDialog::closeEvent(QCloseEvent *event) {
  if (m_mode == Mode::Unified) {
    // Unified flow: closing must not cancel the underlying scrape.
    // Pause if interactive mid-pick so the service doesn't fire the
    // next item's picker into a dead UI; auto mode just keeps going.
    if (m_service && m_service->state() == Scraper::ScraperService::State::RunningInteractive) {
      m_service->pauseInteractive();
    }
    hide();
    event->ignore();
    return;
  }
  QDialog::closeEvent(event);
}

void ScrapeResultDialog::hideEvent(QHideEvent *event) {
  // The background scrape keeps producing itemCompleted signals while
  // the dialog is hidden. Stop the periodic timers here so the
  // *invisible* UI doesn't keep running — service still ticks, but we
  // don't burn CPU updating widgets nobody can see. The itemCompleted
  // slot also short-circuits its pixmap-scale work via isVisible().
  if (m_liveTickTimer) m_liveTickTimer->stop();
  if (m_marqueeTimer) m_marqueeTimer->stop();
  if (g_visibleInstanceCount > 0) {
    --g_visibleInstanceCount;
  }
  QDialog::hideEvent(event);
}

void ScrapeResultDialog::showEvent(QShowEvent *event) {
  QDialog::showEvent(event);
  ++g_visibleInstanceCount;
  // Resume periodic UI updates when the dialog becomes visible again.
  // Only restart while the service is still actively scraping —
  // ticks at idle are pure waste. Note startUnifiedScrape also creates
  // these timers on the running-service path; this branch just covers
  // a plain show() after a hide().
  if (m_service && m_service->isActive()) {
    if (m_liveTickTimer && !m_liveTickTimer->isActive()) m_liveTickTimer->start();
    if (m_marqueeTimer && !m_marqueeTimer->isActive()) m_marqueeTimer->start();
  }
}

void ScrapeResultDialog::setSharedAssetSearchPaths(const QStringList &paths) {
  m_sharedSearchPaths = paths;
}

void ScrapeResultDialog::setRescrapeContext(const QString &artworkDir, const QString &baseName,
                                            Scraper::RescrapeMode rescrapeMode) {
  m_rescrapeArtworkDir = artworkDir;
  m_rescrapeBaseName = baseName;
  m_rescrapeMode = rescrapeMode;
}

namespace {
// Compute the on-disk filename a group/company-scoped asset would
// occupy under an artwork directory. Mirrors the layout used by
// scrapepersistence.cpp (`_shared/<type>/<scope>_<id>.<ext>`). We
// can't easily share the extension-inference helper because it lives
// in an anonymous namespace there; for the dedup probe we accept any
// of the common image extensions, since the actual stored ext
// depends on which URL the *first* scrape used.
QStringList sharedAssetProbePaths(const Scraper::MediaAsset &asset, const QString &artworkDir) {
  if (asset.scope == Scraper::MediaScope::Game || asset.scopeKey.isEmpty() ||
      artworkDir.isEmpty()) {
    return {};
  }
  const QString scopePrefix = asset.scope == Scraper::MediaScope::Group
                                  ? QStringLiteral("group_")
                                  : QStringLiteral("company_");
  const QString dir = QDir(artworkDir).filePath(QStringLiteral("_shared/") + asset.type);
  // Probe png first (default), then common fallbacks. extensionForAsset
  // in scrapepersistence defaults to png for images; if a previous
  // scrape used outputformat=jpg or SS served webp, we'd still find
  // it here.
  QStringList out;
  for (const char *ext : {"png", "jpg", "jpeg", "webp"}) {
    out.append(
        QDir(dir).filePath(scopePrefix + asset.scopeKey + QLatin1Char('.') + QLatin1String(ext)));
  }
  return out;
}

QString findExistingSharedAsset(const Scraper::MediaAsset &asset, const QStringList &searchPaths) {
  for (const QString &artDir : searchPaths) {
    for (const QString &candidate : sharedAssetProbePaths(asset, artDir)) {
      if (QFileInfo::exists(candidate)) {
        return candidate;
      }
    }
  }
  return QString();
}

/// Compute the on-disk path scrapepersistence.cpp would use for a
/// per-game (Game-scoped) image asset, given the active collection's
/// artwork directory + basename. Mirrors the layout
/// `{artworkDir}/<type>/{baseName}.<ext>`. Probes a small extension
/// whitelist because the actual stored extension depends on whichever
/// URL the *prior* scrape used (default png, possibly jpg/webp).
/// Returns the first existing candidate, or empty when none match.
QString findExistingPerGameAsset(const Scraper::MediaAsset &asset, const QString &artworkDir,
                                 const QString &baseName) {
  if (asset.scope != Scraper::MediaScope::Game || artworkDir.isEmpty() || baseName.isEmpty() ||
      asset.type.isEmpty()) {
    return {};
  }
  // Skip videos / manuals / non-image kinds — the CRC short-circuit
  // is documented for SS's mediaJeu.php image endpoints. Videos
  // (`mediaVideoJeu.php`) and manuals (`mediaManuelJeu.php`) don't
  // accept the hash params per SS docs.
  static const QStringList kSkipTypes = {QStringLiteral("video"), QStringLiteral("manual")};
  if (kSkipTypes.contains(asset.type.toLower())) return {};
  const QString dir = QDir(artworkDir).filePath(asset.type);
  for (const char *ext : {"png", "jpg", "jpeg", "webp"}) {
    const QString candidate = QDir(dir).filePath(baseName + QLatin1Char('.') + QLatin1String(ext));
    if (QFileInfo::exists(candidate)) {
      return candidate;
    }
  }
  return {};
}

struct LocalHashes {
  QString crc32Hex; // SS expects the CRC32 as a hex string
  QString md5Hex;
  QString sha1Hex;
};

/// Hash an existing on-disk asset for the SS short-circuit. Streams
/// the file once through both MD5 and SHA1 (cheap relative to the
/// download we're trying to skip); CRC32 left empty because Qt
/// doesn't ship one and the MD5 path is sufficient. Empty result
/// when the file can't be read — caller falls back to unconditional
/// fetch.
LocalHashes hashLocalFile(const QString &path) {
  LocalHashes out;
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return out;
  QCryptographicHash md5(QCryptographicHash::Md5);
  QCryptographicHash sha1(QCryptographicHash::Sha1);
  while (!f.atEnd()) {
    const QByteArray chunk = f.read(64 * 1024);
    md5.addData(chunk);
    sha1.addData(chunk);
  }
  out.md5Hex = QString::fromLatin1(md5.result().toHex());
  out.sha1Hex = QString::fromLatin1(sha1.result().toHex());
  return out;
}

/// Append SS's hash query params to a media URL so its server can
/// short-circuit the response when our local file matches. SS docs:
/// when the supplied hash matches, the server replies with a tiny
/// "MD5OK" / "SHA1OK" / "CRCOK" body instead of the full bytes.
QUrl withHashHints(const QUrl &original, const LocalHashes &hashes) {
  if (hashes.md5Hex.isEmpty() && hashes.sha1Hex.isEmpty() && hashes.crc32Hex.isEmpty()) {
    return original;
  }
  QUrl out(original);
  QUrlQuery q(out);
  if (!hashes.md5Hex.isEmpty() && !q.hasQueryItem(QStringLiteral("md5"))) {
    q.addQueryItem(QStringLiteral("md5"), hashes.md5Hex);
  }
  if (!hashes.sha1Hex.isEmpty() && !q.hasQueryItem(QStringLiteral("sha1"))) {
    q.addQueryItem(QStringLiteral("sha1"), hashes.sha1Hex);
  }
  if (!hashes.crc32Hex.isEmpty() && !q.hasQueryItem(QStringLiteral("crc"))) {
    q.addQueryItem(QStringLiteral("crc"), hashes.crc32Hex);
  }
  out.setQuery(q);
  return out;
}

/// True when SS's short-circuit reply landed (small body that starts
/// with one of the documented sentinels). The trailing newline /
/// whitespace is sometimes present; trim before comparing.
bool isHashShortCircuit(const QByteArray &body) {
  if (body.size() > 32) return false;
  const QByteArray trimmed = body.trimmed();
  return trimmed == "MD5OK" || trimmed == "SHA1OK" || trimmed == "CRCOK";
}
} // namespace

void ScrapeResultDialog::buildUi() {
  auto *root = new QVBoxLayout(this);

  // Outer two-page stack: the single-item splitter and the batch
  // progress panel live as sibling pages so setBatchRunner can flip
  // the dialog into a progress-only view without recreating any of
  // the single-item state.
  m_modeStack = new QStackedWidget(this);

  // ── Single-item page ────────────────────────────────────────────
  m_singleItemView = new SingleItemScrapeView(m_modeStack);
  m_modeStack->addWidget(m_singleItemView);
  connect(m_singleItemView, &SingleItemScrapeView::candidateRowChanged, this,
          &ScrapeResultDialog::onCandidateSelected);

  // ── Batch progress page ─────────────────────────────────────────
  m_batchView = new BatchScrapeProgressView(m_modeStack);
  m_modeStack->addWidget(m_batchView);
  connect(m_batchView, &BatchScrapeProgressView::finished, this,
          [this](Scraper::BatchScrapeRunner::Summary) { accept(); });

  // ── Unified setup page ──────────────────────────────────────────
  m_unified->buildUnifiedPanel();
  m_modeStack->addWidget(m_unifiedPage);

  root->addWidget(m_modeStack, 1);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
  m_applyButton = buttons->addButton(tr("Apply"), QDialogButtonBox::AcceptRole);
  m_applyButton->setEnabled(false);
  // Cancel semantics:
  //  • Single-item, pre-Apply  → reject() (no work done yet).
  //  • Single-item, mid-Apply  → accept() and commit whatever finished.
  //    The user has already paid the bandwidth cost for those assets;
  //    abandoning them just means they re-download next time.
  //  • Batch                   → forward to runner->cancel(); the
  //    runner drains its in-flight callbacks and emits `finished`,
  //    which we treat as a normal completion (the dialog accept()s
  //    via the connected slot in setBatchRunner).
  connect(buttons, &QDialogButtonBox::rejected, this, [this]() {
    if (m_mode == Mode::Batch) {
      if (m_batchRunner) m_batchRunner->cancel();
      return;
    }
    if (m_mode == Mode::Unified) {
      // Setup phase: just reject the dialog (no scrape has started).
      // Running phase, service-owned: ask the service to cancel;
      //   the service drains in-flight items and emits
      //   scrapeFinished, the dialog's signal handler flips back
      //   to the Setup view. The dialog itself doesn't close.
      // Running phase, legacy in-dialog orchestration: flip the
      //   cancel flag so the next chain hop stops.
      if (m_service && m_service->isActive()) {
        m_service->cancel();
        return;
      }
      if (m_unifiedPhase == UnifiedPhase::Setup || m_unifiedPhase == UnifiedPhase::Done) {
        reject();
        return;
      }
      m_unifiedCancelled = true;
      if (m_batchRunner) m_batchRunner->cancel();
      return;
    }
    if (m_downloadsTotal > 0) {
      qCInfo(lcDialogTimings) << "DIALOG cancel mid-download — committing"
                              << m_result.downloads.size() << "of" << m_downloadsTotal;
      accept();
    } else {
      reject();
    }
  });
  connect(m_applyButton, &QPushButton::clicked, this, &ScrapeResultDialog::onApply);
  root->addWidget(buttons);
}

void ScrapeResultDialog::setScraperContext(const ScraperContext &ctx) {
  m_scraperCtx = ctx;
}

void ScrapeResultDialog::startUnifiedScrape(int preCollectionIndex, const QString &preItemPath) {
  m_unified->startUnifiedScrape(preCollectionIndex, preItemPath);
}

void ScrapeResultDialog::onCollectionTreeCurrentChanged(QTreeWidgetItem *current,
                                                        QTreeWidgetItem *previous) {
  m_unified->onCollectionTreeCurrentChanged(current, previous);
}

void ScrapeResultDialog::onCollectionCheckChanged(QTreeWidgetItem *item, int column) {
  m_unified->onCollectionCheckChanged(item, column);
}

void ScrapeResultDialog::onItemCheckChanged(QListWidgetItem *item) {
  m_unified->onItemCheckChanged(item);
}

void ScrapeResultDialog::setScraperService(Scraper::ScraperService *service) {
  qCInfo(lcDialogTimings) << "DIALOG setScraperService called service=" << service
                          << "current=" << m_service << "this=" << this;
  if (m_service == service) {
    qCInfo(lcDialogTimings) << "DIALOG setScraperService: same service, skipping connect";
    return;
  }
  m_service = service;
  if (!m_service) return;
  qCInfo(lcDialogTimings) << "DIALOG setScraperService: establishing connections";
  // Connect to every signal the dialog needs to keep its Live view in
  // sync. Each handler was an inline lambda before Kartend-3fkz step 2
  // pulled the bodies out into named private slots — setScraperService
  // is now a connect table, and each handler is reviewable on its own.
  connect(m_service, &Scraper::ScraperService::scrapeStarted, this,
          &ScrapeResultDialog::onServiceScrapeStarted);
  connect(m_service, &Scraper::ScraperService::itemBegan, this,
          &ScrapeResultDialog::onServiceItemBegan);
  connect(m_service, &Scraper::ScraperService::itemCompleted, this,
          &ScrapeResultDialog::onServiceItemCompleted);
  connect(m_service, &Scraper::ScraperService::pickerNeeded, this,
          &ScrapeResultDialog::onServicePickerNeeded);
  connect(m_service, &Scraper::ScraperService::scrapeFinished, this,
          &ScrapeResultDialog::onServiceScrapeFinished);
  connect(m_service, &Scraper::ScraperService::scrapePaused, this,
          &ScrapeResultDialog::onServiceScrapePaused);
  connect(m_service, &Scraper::ScraperService::quotaUpdated, this,
          &ScrapeResultDialog::onServiceQuotaUpdated);
}

void ScrapeResultDialog::onServiceScrapeStarted(int total) {
  m_unified->onServiceScrapeStarted(total);
}

void ScrapeResultDialog::onServiceItemBegan(int done, int total, const QString &collectionName,
                                            const QString &name) {
  m_unified->onServiceItemBegan(done, total, collectionName, name);
}

void ScrapeResultDialog::onServiceItemCompleted(int done, int total,
                                                const Scraper::ScrapedItem &scraped,
                                                const QStringList &mediaPaths) {
  m_unified->onServiceItemCompleted(done, total, scraped, mediaPaths);
}

void ScrapeResultDialog::onServicePickerNeeded(const QString &itemPath, const QString &itemName,
                                               const QList<Scraper::ScrapeCandidate> &candidates,
                                               std::shared_ptr<MetadataLookupProvider> provider,
                                               const QString &artworkDir) {
  m_unified->onServicePickerNeeded(itemPath, itemName, candidates, provider, artworkDir);
}

void ScrapeResultDialog::onServiceScrapeFinished(const Scraper::ScraperService::Summary &s) {
  m_unified->onServiceScrapeFinished(s);
}

void ScrapeResultDialog::onServiceScrapePaused() {
  m_unified->onServiceScrapePaused();
}

void ScrapeResultDialog::onServiceQuotaUpdated(const Scraper::QuotaStatus &quota) {
  m_unified->onServiceQuotaUpdated(quota);
}

void ScrapeResultDialog::showScrapeErrorDetails() {
  m_unified->showScrapeErrorDetails();
}

void ScrapeResultDialog::onScrapeClicked() {
  m_unified->onScrapeClicked();
}

void ScrapeResultDialog::onCandidateSelected(int row) {
  m_currentRow = row;
  if (row < 0 || row >= m_candidates.size()) {
    m_singleItemView->setDetailPage(SingleItemScrapeView::DetailStackPage::Empty);
    m_applyButton->setEnabled(false);
    return;
  }

  // Cache hit — re-render without an HTTP roundtrip.
  if (m_detailCache.contains(row)) {
    m_currentDetail = m_detailCache.value(row);
    m_singleItemView->detailText()->setHtml(renderDetailHtml(m_currentDetail));
    m_singleItemView->populateMediaCheckboxes(m_currentDetail);
    m_singleItemView->setDetailPage(SingleItemScrapeView::DetailStackPage::Detail);
    m_applyButton->setEnabled(!m_healthBlocksApply);
    return;
  }

  m_singleItemView->setDetailPage(SingleItemScrapeView::DetailStackPage::Loading);
  m_applyButton->setEnabled(false);

  if (!m_provider) {
    return;
  }
  const Scraper::ScrapeCandidate cand = m_candidates[row];
  m_provider->fetchDetail(cand, [this, row, cand](ErrorUtils::Result<Scraper::ScrapedItem> result) {
    // Bail if the user moved on to a different candidate while we
    // were waiting — only update the UI when we're still showing
    // the row this callback is for.
    if (m_currentRow != row) {
      return;
    }
    if (result.isError()) {
      m_singleItemView->detailText()->setHtml(QStringLiteral("<p style='color:red'>%1</p>")
                                                  .arg(result.error().message.toHtmlEscaped()));
      m_singleItemView->mediaList()->clear();
      m_singleItemView->clearMediaRows();
      m_singleItemView->setDetailPage(SingleItemScrapeView::DetailStackPage::Detail);
      m_applyButton->setEnabled(false);
      return;
    }
    m_currentDetail = result.value();
    m_detailCache.insert(row, m_currentDetail);
    m_singleItemView->detailText()->setHtml(renderDetailHtml(m_currentDetail));
    m_singleItemView->populateMediaCheckboxes(m_currentDetail);
    m_singleItemView->setDetailPage(SingleItemScrapeView::DetailStackPage::Detail);
    m_applyButton->setEnabled(!m_healthBlocksApply);
  });
}

void ScrapeResultDialog::onApply() {
  if (m_currentDetail.title.isEmpty()) {
    return;
  }
  m_result.item = m_currentDetail;
  m_result.downloads.clear();

  QList<Scraper::MediaAsset> selected;
  if (m_mode == Mode::Unified && m_unifiedPhase == UnifiedPhase::InteractivePicking) {
    // Unified interactive Apply: there's no per-item media checkbox
    // panel in this view, so derive the asset list from the setup-
    // view media-type checkboxes (same filter auto-mode uses). Each
    // matching MediaAsset from the current detail is queued for the
    // download pass below.
    QSet<QString> filter;
    for (auto it = m_mediaTypeChecks.constBegin(); it != m_mediaTypeChecks.constEnd(); ++it) {
      if (it.key() == QLatin1String("_metadata")) continue;
      if (it.value()->isChecked()) filter.insert(it.key().toLower());
    }
    for (const auto &asset : m_currentDetail.media) {
      if (!asset.url.isValid()) continue;
      if (filter.isEmpty()) {
        // Empty filter falls back to "front cover only" — same
        // legacy behaviour BatchScrapeRunner uses.
        if (asset.type.compare(QStringLiteral("front"), Qt::CaseInsensitive) == 0) {
          selected.append(asset);
          break;
        }
      } else if (filter.contains(asset.type.toLower())) {
        selected.append(asset);
      }
    }
  } else {
    // Legacy single-item / batch path — checkbox-driven media list.
    for (const auto &row : m_singleItemView->mediaRows()) {
      if (row.first->isChecked()) {
        selected.append(row.second);
      }
    }
  }

  if (selected.isEmpty() || !m_provider) {
    m_unified->finishCurrentApply();
    return;
  }

  m_applyButton->setEnabled(false);
  m_singleItemView->candidateList()->setEnabled(false);
  m_downloadsTotal = selected.size();
  m_downloadsPending = selected.size();
  m_downloadedBytes = 0;
  m_downloadStartMs = QDateTime::currentMSecsSinceEpoch();
  m_singleItemView->statusLabel()->setText(tr("Downloading %1 media items…").arg(m_downloadsTotal));

  // Trace the dispatch + each completion so we can tell where time
  // goes during interactive scrape: dialog-side dispatch loop, HttpClient
  // throttle, or actual network duration.
  auto applyTimer = std::make_shared<QElapsedTimer>();
  applyTimer->start();
  // First selected asset's URL gets logged so we can confirm whether
  // the active preset injected `maxwidth/maxheight` query params.
  // (Otherwise the throttling/concurrency lines tell us nothing
  // about whether the resize policy actually took effect.)
  const QString sample =
      selected.isEmpty() ? QStringLiteral("(none)") : selected.first().url.toString().left(200);
  qCInfo(lcDialogTimings) << "DIALOG dispatch begin total=" << m_downloadsTotal
                          << "first_url=" << sample;

  dispatchSelectedDownloads(selected, applyTimer);
}

void ScrapeResultDialog::dispatchSelectedDownloads(
    const QList<Scraper::MediaAsset> &selected, const std::shared_ptr<QElapsedTimer> &applyTimer) {
  // Fire every selected asset at once. Scraper::HttpClient enforces
  // the per-host concurrency cap + inter-start throttle, so dispatching
  // in parallel just fills the available slots instead of waiting for
  // each download to finish before queueing the next. Big interactive
  // scrapes (20+ media types) used to spend the whole queue serialized
  // behind one in-flight reply at a time; with the throttle policy
  // already gating downstream this serialization was pure dead time.
  //
  // QPointer guard: each fetch's completion callback can fire long
  // after the dialog has been accept()'d and destroyed by the caller
  // (cancel mid-download → caller exits exec() → caller's scope ends
  // → dialog dtor). The QPointer goes null when the dialog is
  // destroyed; the callback checks it before touching `this`.
  QPointer<ScrapeResultDialog> guard(this);
  // CRC short-circuit context lookup. Active only when the user has
  // configured FillMissing or UpdateChanged for the active scrape.
  // Overwrite mode wants the bytes regardless; Skip never reaches
  // this dialog (batch-level signal, gate'd in batchscraperunner).
  const bool crcEligible = !m_rescrapeArtworkDir.isEmpty() && !m_rescrapeBaseName.isEmpty() &&
                           (m_rescrapeMode == Scraper::RescrapeMode::FillMissing ||
                            m_rescrapeMode == Scraper::RescrapeMode::UpdateChanged);
  for (const auto &asset : selected) {
    // Cross-collection dedup for group/company-scoped assets. If the
    // file already lives in the current collection's `_shared/` (or
    // any sibling collection's), read its bytes from disk and route
    // them through the same MediaDownload pipeline used by the
    // network path — applyScrapedItem then writes them into the
    // *active* collection's `_shared/` directory, so a new collection
    // is self-contained on first scrape without re-paying bandwidth.
    const QString existing = findExistingSharedAsset(asset, m_sharedSearchPaths);
    if (!existing.isEmpty()) {
      qCInfo(lcDialogTimings) << "DIALOG dedup hit" << asset.type << asset.label << "<-"
                              << existing;
      QFile f(existing);
      QByteArray bytes;
      if (f.open(QIODevice::ReadOnly)) {
        bytes = f.readAll();
      }
      if (!bytes.isEmpty()) {
        MediaDownload dl;
        dl.asset = asset;
        dl.bytes = bytes;
        m_downloadedBytes += bytes.size();
        m_result.downloads.append(dl);
      }
      --m_downloadsPending;
      if (m_downloadsPending <= 0) {
        m_unified->finishCurrentApply();
      }
      continue;
    }
    // Per-game CRC short-circuit: append md5/sha1 hash hints to the
    // URL when the destination file already exists. SS replies with
    // a tiny "*OK" body when the local file matches, and we treat
    // that as a benign skip — no bytes added to the download list,
    // existing file stays untouched. The persistence layer's
    // re-scrape gate then never sees this asset and does the right
    // thing by default (FillMissing keeps existing; UpdateChanged
    // can't compare bytes because we never downloaded them, but
    // since SS confirmed equality the existing file is the right
    // copy anyway).
    QUrl fetchUrl = asset.url;
    if (crcEligible) {
      const QString existingPerGame =
          findExistingPerGameAsset(asset, m_rescrapeArtworkDir, m_rescrapeBaseName);
      if (!existingPerGame.isEmpty()) {
        const LocalHashes hashes = hashLocalFile(existingPerGame);
        fetchUrl = withHashHints(asset.url, hashes);
        qCInfo(lcDialogTimings) << "DIALOG hash-hint" << asset.type << "from" << existingPerGame;
      }
    }
    m_provider->fetchMediaBytes(
        fetchUrl, [guard, asset, applyTimer](ErrorUtils::Result<QByteArray> response) {
          if (guard.isNull()) return; // dialog gone — drop the reply
          qCInfo(lcDialogTimings) << "DIALOG complete" << asset.type << asset.label
                                  << "ok=" << response.isOk()
                                  << "elapsed_total=" << applyTimer->elapsed() << "ms";
          if (response.isOk()) {
            const QByteArray bytes = response.value();
            // Hash short-circuit hit — SS confirmed the local file
            // matches, so don't add to the download list. Persistence
            // never sees the asset and the existing file stays put.
            if (isHashShortCircuit(bytes)) {
              qCInfo(lcDialogTimings)
                  << "DIALOG hash-hit (skip)" << asset.type << "body=" << bytes.trimmed();
            } else {
              MediaDownload dl;
              dl.asset = asset;
              dl.bytes = bytes;
              guard->m_downloadedBytes += dl.bytes.size();
              guard->m_result.downloads.append(dl);
            }
          }
          // On error we silently skip — partial success is better than
          // failing the whole scrape because one asset 404'd.
          --guard->m_downloadsPending;
          const int completed = guard->m_downloadsTotal - guard->m_downloadsPending;
          guard->updateSingleItemProgress(completed);
          if (guard->m_downloadsPending <= 0) {
            qCInfo(lcDialogTimings) << "DIALOG all done in" << applyTimer->elapsed() << "ms";
            guard->m_unified->finishCurrentApply();
          }
        });
  }
  qCInfo(lcDialogTimings) << "DIALOG dispatch loop returned in" << applyTimer->elapsed() << "ms"
                          << "(should be near zero — all calls are async)";
}

QString ScrapeResultDialog::formatDuration(qint64 ms) {
  if (ms <= 0) return QStringLiteral("—");
  const qint64 totalSec = ms / 1000;
  const qint64 h = totalSec / 3600;
  const qint64 m = (totalSec / 60) % 60;
  const qint64 s = totalSec % 60;
  if (h > 0) {
    return QStringLiteral("%1:%2:%3")
        .arg(h)
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'));
  }
  if (m > 0) {
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
  }
  return tr("%1s").arg(s);
}

void ScrapeResultDialog::updateSingleItemProgress(int completed) {
  // Wall-clock throughput + count + ETA. Bytes/sec drives the rate
  // readout (mirrors aggregate network behaviour rather than per-asset
  // spikes — more useful when mixed image / video downloads are in
  // flight). ETA is items-based (elapsed / completed × remaining)
  // because asset sizes vary by an order of magnitude inside one
  // scrape; per-bytes ETA would whiplash when a 30 MB video lands
  // next to a 50 KB cover.
  const qint64 elapsedMs =
      std::max<qint64>(1, QDateTime::currentMSecsSinceEpoch() - m_downloadStartMs);
  const double mibPerSec = (m_downloadedBytes / (1024.0 * 1024.0)) / (elapsedMs / 1000.0);
  QString rateStr;
  if (mibPerSec >= 1.0) {
    rateStr = tr("%1 MiB/s").arg(mibPerSec, 0, 'f', 1);
  } else {
    rateStr = tr("%1 KiB/s").arg(mibPerSec * 1024.0, 0, 'f', 0);
  }
  QString etaStr = QStringLiteral("—");
  if (completed > 0 && m_downloadsTotal > completed) {
    const qint64 etaMs = static_cast<qint64>((double(elapsedMs) / double(completed)) *
                                             double(m_downloadsTotal - completed));
    etaStr = formatDuration(etaMs);
  }
  m_singleItemView->statusLabel()->setText(tr("Downloaded %1 of %2 (%3) · ETA %4")
                                               .arg(completed)
                                               .arg(m_downloadsTotal)
                                               .arg(rateStr)
                                               .arg(etaStr));
}

void ScrapeResultDialog::setBatchRunner(Scraper::BatchScrapeRunner *runner,
                                        const QString &collectionName, int totalItems) {
  m_mode = Mode::Batch;
  m_batchRunner = runner;
  // Flip to the batch page; the single-item splitter stays alive but
  // hidden. Apply is meaningless in batch mode — the runner drives
  // every per-item decision — so we hide it. Cancel keeps its slot in
  // the button row and forwards to runner->cancel() via the lambda in
  // buildUi. The BatchScrapeProgressView observes the runner's signals
  // and emits its own `finished` signal, which the host accepts via
  // the connect set up in buildUi.
  if (m_modeStack) m_modeStack->setCurrentWidget(m_batchView);
  if (m_applyButton) m_applyButton->hide();
  m_batchView->setRunner(runner, collectionName, totalItems);
}

Scraper::BatchScrapeRunner::Summary ScrapeResultDialog::batchSummary() const {
  return m_batchView ? m_batchView->summary() : Scraper::BatchScrapeRunner::Summary{};
}
