// Modal review dialog for scrape results. Provider-agnostic — drives
// the picked MetadataLookupProvider for detail + media downloads, so
// adding new providers (ScreenScraper, TMDB, Open Library) doesn't
// touch this file.
#include "scraperesultdialog.h"

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
#include "metadatalookupprovider.h"
#include "pathutils.h"
#include "scrapejobgrouping.h"

namespace {

// Default size — modest baseline; dialog reflows responsively when
// the user resizes (narrower → fewer chips per row, wider → more).
constexpr int DIALOG_WIDTH = 900;
constexpr int DIALOG_HEIGHT = 780;

/// Reflowing wrap layout for the metadata chip widgets. Items lay
/// out left-to-right until the right edge of the container is
/// reached, then wrap to the next line. Width changes (user resize)
/// re-flow the items so ultrawide windows pack more chips per row
/// and narrow windows stack them. Adapted from the canonical Qt
/// FlowLayout example.
class FlowLayout : public QLayout {
public:
  explicit FlowLayout(QWidget *parent, int margin = 0, int hSpacing = 8, int vSpacing = 6)
      : QLayout(parent), m_hSpace(hSpacing), m_vSpace(vSpacing) {
    setContentsMargins(margin, margin, margin, margin);
  }
  ~FlowLayout() override {
    while (QLayoutItem *item = takeAt(0)) delete item;
  }
  void addItem(QLayoutItem *item) override { m_items.append(item); }
  int horizontalSpacing() const { return m_hSpace; }
  int verticalSpacing() const { return m_vSpace; }
  Qt::Orientations expandingDirections() const override { return {}; }
  bool hasHeightForWidth() const override { return true; }
  int heightForWidth(int width) const override { return doLayout(QRect(0, 0, width, 0), true); }
  int count() const override { return m_items.size(); }
  QLayoutItem *itemAt(int idx) const override { return m_items.value(idx); }
  QLayoutItem *takeAt(int idx) override {
    return (idx >= 0 && idx < m_items.size()) ? m_items.takeAt(idx) : nullptr;
  }
  QSize minimumSize() const override {
    QSize s;
    for (auto *it : m_items) s = s.expandedTo(it->minimumSize());
    int l, t, r, b;
    getContentsMargins(&l, &t, &r, &b);
    s += QSize(l + r, t + b);
    return s;
  }
  void setGeometry(const QRect &r) override {
    QLayout::setGeometry(r);
    doLayout(r, false);
  }
  QSize sizeHint() const override { return minimumSize(); }

private:
  int doLayout(const QRect &rect, bool testOnly) const {
    int l, t, r, b;
    getContentsMargins(&l, &t, &r, &b);
    QRect eff = rect.adjusted(l, t, -r, -b);
    int x = eff.x();
    int y = eff.y();
    int lineH = 0;
    for (auto *it : m_items) {
      const QSize sz = it->sizeHint();
      int nextX = x + sz.width() + m_hSpace;
      if (nextX - m_hSpace > eff.right() && lineH > 0) {
        x = eff.x();
        y += lineH + m_vSpace;
        nextX = x + sz.width() + m_hSpace;
        lineH = 0;
      }
      if (!testOnly) it->setGeometry(QRect(QPoint(x, y), sz));
      x = nextX;
      lineH = qMax(lineH, sz.height());
    }
    return y + lineH - rect.y() + b;
  }
  QList<QLayoutItem *> m_items;
  int m_hSpace;
  int m_vSpace;
};

constexpr int STACK_EMPTY = 0;
constexpr int STACK_LOADING = 1;
constexpr int STACK_DETAIL = 2;

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
    : QDialog(parent), m_provider(provider), m_candidates(std::move(candidates)) {
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
    auto *item = new QListWidgetItem(m_candidateList);
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
    m_candidateList->setCurrentRow(0);
  }
  // ScreenScraper's jeuInfos.php returns exactly one matched game per
  // request — the candidate list ends up with one entry and the user
  // has nothing to choose between. Hide the panel entirely in that
  // case to claim the horizontal room for the description / media
  // checkboxes. Multi-candidate providers (MusicBrainz / OpenLibrary
  // / TMDB) keep the list since picking between candidates is the
  // whole point of their search response.
  if (m_candidateList && m_candidates.size() <= 1) {
    m_candidateList->hide();
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
      guard->m_healthLabel->setText(status.humanStatus);
      guard->m_healthLabel->show();
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

  // ── Single-item page (existing layout) ──────────────────────────
  m_singleItemPage = new QWidget(m_modeStack);
  auto *singleLayout = new QVBoxLayout(m_singleItemPage);
  singleLayout->setContentsMargins(0, 0, 0, 0);

  auto *splitter = new QSplitter(Qt::Horizontal, m_singleItemPage);

  m_candidateList = new QListWidget(splitter);
  m_candidateList->setMinimumWidth(220);
  connect(m_candidateList, &QListWidget::currentRowChanged, this,
          &ScrapeResultDialog::onCandidateSelected);
  splitter->addWidget(m_candidateList);

  auto *rightContainer = new QWidget(splitter);
  auto *rightLayout = new QVBoxLayout(rightContainer);
  rightLayout->setContentsMargins(0, 0, 0, 0);

  m_detailStack = new QStackedWidget(rightContainer);
  m_emptyLabel = new QLabel(tr("Pick a candidate from the list."), rightContainer);
  m_emptyLabel->setAlignment(Qt::AlignCenter);
  m_emptyLabel->setStyleSheet("color: palette(mid);");
  m_loadingLabel = new QLabel(tr("Loading details…"), rightContainer);
  m_loadingLabel->setAlignment(Qt::AlignCenter);
  m_loadingLabel->setStyleSheet("color: palette(mid);");
  auto *detailContainer = new QWidget(rightContainer);
  auto *detailLayout = new QVBoxLayout(detailContainer);
  detailLayout->setContentsMargins(0, 0, 0, 0);
  m_detailText = new QTextBrowser(detailContainer);
  m_detailText->setOpenExternalLinks(true);
  detailLayout->addWidget(m_detailText, 1);
  auto *mediaHeader = new QLabel(tr("Media to download:"), detailContainer);
  mediaHeader->setStyleSheet("font-weight: bold;");
  detailLayout->addWidget(mediaHeader);
  m_mediaList = new QListWidget(detailContainer);
  m_mediaList->setSelectionMode(QAbstractItemView::NoSelection);
  m_mediaList->setMaximumHeight(140);
  detailLayout->addWidget(m_mediaList);

  m_detailStack->insertWidget(STACK_EMPTY, m_emptyLabel);
  m_detailStack->insertWidget(STACK_LOADING, m_loadingLabel);
  m_detailStack->insertWidget(STACK_DETAIL, detailContainer);
  rightLayout->addWidget(m_detailStack, 1);

  m_healthLabel = new QLabel(rightContainer);
  m_healthLabel->setWordWrap(true);
  // Hidden until the provider's health probe lands (or stays hidden
  // forever for providers that have no probe). Yellow-ish accent so
  // the line reads as a heads-up — not buried in the same italic
  // grey the download-progress line uses.
  m_healthLabel->setStyleSheet(
      "color: palette(highlight); padding: 4px; background: palette(alternate-base);");
  m_healthLabel->hide();
  rightLayout->addWidget(m_healthLabel);

  m_statusLabel = new QLabel(rightContainer);
  // palette(mid) ran too dim on dark themes — the live "Downloaded N
  // of M (X.X MiB/s)" readout fades into the dialog background while
  // the download is in flight, which is the moment the user actually
  // wants to read it. palette(windowtext) keeps it at the same
  // contrast as the rest of the dialog text; the italic still
  // differentiates it from the description body.
  m_statusLabel->setStyleSheet("color: palette(windowtext); font-style: italic;");
  rightLayout->addWidget(m_statusLabel);

  splitter->addWidget(rightContainer);
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  singleLayout->addWidget(splitter, 1);

  m_modeStack->addWidget(m_singleItemPage);

  // ── Batch progress page ─────────────────────────────────────────
  buildBatchPanel();
  m_modeStack->addWidget(m_batchPage);

  // ── Unified setup page ──────────────────────────────────────────
  buildUnifiedPanel();
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

void ScrapeResultDialog::buildBatchPanel() {
  m_batchPage = new QWidget(m_modeStack);
  auto *layout = new QVBoxLayout(m_batchPage);
  layout->setContentsMargins(20, 20, 20, 20);
  layout->setSpacing(12);

  m_batchHeaderLabel = new QLabel(m_batchPage);
  QFont headerFont = m_batchHeaderLabel->font();
  headerFont.setPointSizeF(headerFont.pointSizeF() * 1.2);
  headerFont.setBold(true);
  m_batchHeaderLabel->setFont(headerFont);
  m_batchHeaderLabel->setWordWrap(true);
  layout->addWidget(m_batchHeaderLabel);

  m_batchCurrentLabel = new QLabel(m_batchPage);
  m_batchCurrentLabel->setWordWrap(true);
  layout->addWidget(m_batchCurrentLabel);

  m_batchProgressBar = new QProgressBar(m_batchPage);
  m_batchProgressBar->setRange(0, 100);
  m_batchProgressBar->setValue(0);
  m_batchProgressBar->setTextVisible(true);
  layout->addWidget(m_batchProgressBar);

  m_batchTimingLabel = new QLabel(m_batchPage);
  m_batchTimingLabel->setWordWrap(true);
  layout->addWidget(m_batchTimingLabel);

  m_batchCountsLabel = new QLabel(m_batchPage);
  layout->addWidget(m_batchCountsLabel);

  layout->addStretch(1);
}

void ScrapeResultDialog::buildUnifiedPanel() {
  m_unifiedPage = new QWidget(m_modeStack);
  auto *root = new QVBoxLayout(m_unifiedPage);
  root->setContentsMargins(8, 8, 8, 8);
  root->setSpacing(8);

  // ── Top: collection tree (left) + items list (right) ────────────
  auto *splitter = new QSplitter(Qt::Horizontal, m_unifiedPage);
  m_unifiedSplitterContainer = splitter; // tracked so we can hide during a run

  m_collectionTree = new QTreeWidget(splitter);
  m_collectionTree->setHeaderLabel(tr("Collections"));
  m_collectionTree->setMinimumWidth(220);
  m_collectionTree->setRootIsDecorated(true);
  m_collectionTree->setAnimated(true);
  m_collectionTree->header()->setStretchLastSection(true);
  connect(m_collectionTree, &QTreeWidget::currentItemChanged, this,
          &ScrapeResultDialog::onCollectionTreeCurrentChanged);
  connect(m_collectionTree, &QTreeWidget::itemChanged, this,
          &ScrapeResultDialog::onCollectionCheckChanged);
  splitter->addWidget(m_collectionTree);

  auto *rightContainer = new QWidget(splitter);
  auto *rightLayout = new QVBoxLayout(rightContainer);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  auto *itemsHeaderRow = new QHBoxLayout;
  m_itemsHeaderLabel = new QLabel(tr("Select a collection to see its items."), rightContainer);
  itemsHeaderRow->addWidget(m_itemsHeaderLabel, 1);
  auto *itemsSelectAll = new QPushButton(tr("Select all"), rightContainer);
  auto *itemsSelectNone = new QPushButton(tr("Select none"), rightContainer);
  itemsHeaderRow->addWidget(itemsSelectAll);
  itemsHeaderRow->addWidget(itemsSelectNone);
  rightLayout->addLayout(itemsHeaderRow);
  // Bulk-toggle every visible row's checkbox + the underlying
  // inclusion map; mirrors what a user would do row-by-row. No-op
  // when no collection is currently displayed.
  auto setAllItemsChecked = [this](bool checked) {
    const auto *cur = m_collectionTree->currentItem();
    if (!cur) return;
    const int idx = m_treeItemToCollectionIndex.value(const_cast<QTreeWidgetItem *>(cur), -1);
    if (idx < 0) return;
    // The items list rows are only enabled when the collection itself
    // is checked; respect that gating here so disabled rows stay off.
    if (cur->checkState(0) != Qt::Checked && checked) return;
    QStringList included;
    QSignalBlocker b(m_unifiedItemsList);
    for (int i = 0; i < m_unifiedItemsList->count(); ++i) {
      auto *row = m_unifiedItemsList->item(i);
      if (!(row->flags() & Qt::ItemIsEnabled)) continue;
      row->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
      if (checked) {
        const QString path = row->data(Qt::UserRole).toString();
        if (!path.isEmpty()) included.append(path);
      }
    }
    m_itemSelectionByCollection[idx] = included;
  };
  connect(itemsSelectAll, &QPushButton::clicked, this,
          [setAllItemsChecked]() { setAllItemsChecked(true); });
  connect(itemsSelectNone, &QPushButton::clicked, this,
          [setAllItemsChecked]() { setAllItemsChecked(false); });
  m_unifiedItemsList = new QListWidget(rightContainer);
  m_unifiedItemsList->setSelectionMode(QAbstractItemView::NoSelection);
  connect(m_unifiedItemsList, &QListWidget::itemChanged, this,
          &ScrapeResultDialog::onItemCheckChanged);
  rightLayout->addWidget(m_unifiedItemsList, 1);
  splitter->addWidget(rightContainer);
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  root->addWidget(splitter, 1);

  // ── Middle: media types ─────────────────────────────────────────
  m_mediaTypesGroup = new QGroupBox(tr("What to scrape"), m_unifiedPage);
  auto *mediaVBox = new QVBoxLayout(m_mediaTypesGroup);

  auto *mediaButtonsRow = new QHBoxLayout;
  auto *mediaSelectAll = new QPushButton(tr("Select all"), m_mediaTypesGroup);
  auto *mediaSelectNone = new QPushButton(tr("Select none"), m_mediaTypesGroup);
  mediaButtonsRow->addWidget(mediaSelectAll);
  mediaButtonsRow->addWidget(mediaSelectNone);
  mediaButtonsRow->addStretch(1);
  mediaVBox->addLayout(mediaButtonsRow);

  auto *mediaGrid = new QGridLayout;
  // Curated list. The leading `_metadata` is a synthetic key that
  // gates the textual ScrapedItem fields (title / description / etc.)
  // rather than a MediaAsset::type — handled specially when building
  // the BatchScrapeRunner filter and at applyResult time.
  //
  // Every other key must equal a MediaAsset::type in lowercase: the
  // runner matches each asset's `type.toLower()` against the filter
  // set, and that set is built by lowercasing these keys. Parser
  // aliases are already collapsed (box-2D → front, sstitle → title,
  // manuel → manual, ss/screenshot → screenshot); every other entry
  // keeps its raw ScreenScraper tag, lowercased.
  struct MediaTypeEntry {
    const char *key;
    const char *label;
    bool defaultOn;
  };
  static constexpr MediaTypeEntry kMediaTypes[] = {
      {"_metadata", "Metadata (title, description, …)", true},
      {"front", "Front cover", true},
      {"box-2d-back", "Back cover", false},
      {"box-2d-side", "Box spine", false},
      {"box-3d", "Box (3D)", false},
      {"box-texture", "Box texture", false},
      {"support-2d", "Cart / disc label", false},
      {"support-3d", "Cart / disc (3D)", false},
      {"support-texture", "Cart / disc texture", false},
      {"screenshot", "Screenshot", false},
      {"title", "Title screen", false},
      {"fanart", "Fanart", false},
      {"background", "Background", false},
      {"steamgrid", "Steam grid", false},
      {"wheel", "Wheel / logo", false},
      {"wheel-carbon", "Wheel (carbon)", false},
      {"wheel-steel", "Wheel (steel)", false},
      {"marquee", "Marquee", false},
      {"screenmarquee", "Screen marquee", false},
      {"screenmarqueesmall", "Screen marquee (small)", false},
      {"bezel-16-9", "Bezel (16:9)", false},
      {"bezel-4-3", "Bezel (4:3)", false},
      {"mixrbv1", "Mix (RBV1)", false},
      {"mixrbv2", "Mix (RBV2)", false},
      {"figurine", "Figurine", false},
      {"pictoliste", "Pictogram (list)", false},
      {"pictocouleur", "Pictogram (colour)", false},
      {"pictomonochrome", "Pictogram (mono)", false},
      {"map", "Map", false},
      {"manual", "Manual", false},
      {"video", "Video", false},
      {"video-normalized", "Video (normalized)", false},
  };
  int row = 0;
  int col = 0;
  constexpr int kColumns = 3;
  for (const auto &mt : kMediaTypes) {
    auto *check = new QCheckBox(tr(mt.label), m_mediaTypesGroup);
    check->setChecked(mt.defaultOn);
    mediaGrid->addWidget(check, row, col);
    m_mediaTypeChecks.insert(QString::fromLatin1(mt.key), check);
    if (++col >= kColumns) {
      col = 0;
      ++row;
    }
  }
  mediaVBox->addLayout(mediaGrid);
  // Bulk-toggle every checkbox in the media-types group, including
  // the synthetic _metadata entry. Captured by value so the lambda
  // survives the local layout pointers going out of scope.
  connect(mediaSelectAll, &QPushButton::clicked, this, [this]() {
    for (auto it = m_mediaTypeChecks.constBegin(); it != m_mediaTypeChecks.constEnd(); ++it) {
      it.value()->setChecked(true);
    }
  });
  connect(mediaSelectNone, &QPushButton::clicked, this, [this]() {
    for (auto it = m_mediaTypeChecks.constBegin(); it != m_mediaTypeChecks.constEnd(); ++it) {
      it.value()->setChecked(false);
    }
  });
  root->addWidget(m_mediaTypesGroup);

  // ── Mode toggle ─────────────────────────────────────────────────
  // Wrap the radio row in a container widget so we can hide the whole
  // row (label + both radios) while a scrape is running. QHBoxLayout
  // alone isn't a widget, so we'd otherwise have to toggle each child.
  m_modeRowContainer = new QWidget(m_unifiedPage);
  auto *modeRow = new QHBoxLayout(m_modeRowContainer);
  modeRow->setContentsMargins(0, 0, 0, 0);
  auto *modeLabel = new QLabel(tr("Mode:"), m_modeRowContainer);
  m_modeAutoRadio = new QRadioButton(tr("Auto-accept (first candidate)"), m_modeRowContainer);
  m_modeInteractiveRadio =
      new QRadioButton(tr("Interactive (pick candidate per item)"), m_modeRowContainer);
  m_modeAutoRadio->setChecked(true);
  modeRow->addWidget(modeLabel);
  modeRow->addWidget(m_modeAutoRadio);
  modeRow->addWidget(m_modeInteractiveRadio);
  modeRow->addStretch(1);
  root->addWidget(m_modeRowContainer);

  // ── Live view: currently-scraping metadata panel ────────────────
  // 10-column QGridLayout: FIVE (label, value) pairs per row.
  // Cross-row alignment is the whole point — every label sits in
  // one of cols {0, 2, 4, 6, 8}, every short value in one of cols
  // {1, 3, 5, 7, 9}, so labels line up vertically across all rows
  // AND every row has the same five-chip rhythm. Wide fields
  // (Description) span all value cols. Custom fields container
  // also spans the row.
  m_liveMetadataGroup = new QGroupBox(tr("Currently scraping"), m_unifiedPage);
  auto *metaOuter = new QVBoxLayout(m_liveMetadataGroup);
  metaOuter->setContentsMargins(8, 8, 8, 8);
  metaOuter->setSpacing(6);
  // ── Interactive candidate picker row ──────────────────────────────
  // Visible only while the service is waiting on the user to pick a
  // candidate (interactive mode). Selecting a row re-fetches detail
  // and refreshes the live metadata fields below. Stays hidden in
  // auto mode so the layout is identical to non-interactive scrapes
  // until the user explicitly turns interactive on.
  m_interactiveCandidateRow = new QWidget(m_liveMetadataGroup);
  auto *candRow = new QHBoxLayout(m_interactiveCandidateRow);
  candRow->setContentsMargins(0, 0, 0, 0);
  candRow->setSpacing(6);
  auto *candLbl = new QLabel(tr("Candidate:"), m_interactiveCandidateRow);
  candLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  candLbl->setMinimumWidth(78);
  candRow->addWidget(candLbl);
  m_interactiveCandidateCombo = new QComboBox(m_interactiveCandidateRow);
  m_interactiveCandidateCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  candRow->addWidget(m_interactiveCandidateCombo, /*stretch=*/1);
  connect(m_interactiveCandidateCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
          [this](int idx) {
            if (m_unifiedPhase == UnifiedPhase::InteractivePicking) {
              interactiveFetchDetail(idx);
            }
          });
  m_interactiveCandidateRow->hide();
  metaOuter->addWidget(m_interactiveCandidateRow);
  // Metadata host uses a plain vertical layout so the dialog can be
  // resized freely. Title + Description rows stretch horizontally to
  // fill the available width; everything else lives inside a backdrop
  // frame whose contents reflow via FlowLayout (typed fields above,
  // custom fields below). This keeps the panel readable on ultrawide
  // screens (more chips per row) AND low-res screens (chips wrap to
  // additional rows instead of overflowing the dialog width).
  auto *metaGridHost = new QWidget(m_liveMetadataGroup);
  auto *metaCol = new QVBoxLayout(metaGridHost);
  metaCol->setContentsMargins(0, 0, 0, 0);
  metaCol->setSpacing(6);

  // Uniform label width across every row so labels visually align.
  constexpr int kLabelW = 90;
  constexpr int kValueChipW = 90;

  auto makeFieldEdit = [this](bool bold = false) {
    auto *edit = new QLineEdit(m_liveMetadataGroup);
    edit->setReadOnly(true);
    edit->setFrame(true);
    if (bold) {
      QFont f = edit->font();
      f.setBold(true);
      edit->setFont(f);
    }
    return edit;
  };
  auto sizedLabel = [this](const QString &text) {
    auto *lbl = new QLabel(text, m_liveMetadataGroup);
    lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lbl->setFixedWidth(kLabelW);
    return lbl;
  };

  // ── Title row: label + stretching bold chip ───────────────────
  // Title is the most prominent field, so it gets its own full-width
  // row that grows with the dialog.
  m_liveMetadataTitle = makeFieldEdit(/*bold=*/true);
  m_liveMetadataTitle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  m_liveMetadataTitle->setMinimumWidth(kValueChipW * 2);
  {
    auto *row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);
    row->addWidget(sizedLabel(tr("Title:")));
    row->addWidget(m_liveMetadataTitle, /*stretch=*/1);
    metaCol->addLayout(row);
  }

  // ── Description row: top-aligned label + multi-line browser ───
  m_liveMetadataDescription = new QTextBrowser(m_liveMetadataGroup);
  m_liveMetadataDescription->setOpenExternalLinks(true);
  m_liveMetadataDescription->setMinimumHeight(80);
  m_liveMetadataDescription->setMaximumHeight(110);
  m_liveMetadataDescription->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
  {
    auto *row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);
    auto *lbl = sizedLabel(tr("Description:"));
    lbl->setAlignment(Qt::AlignRight | Qt::AlignTop);
    row->addWidget(lbl);
    row->addWidget(m_liveMetadataDescription, /*stretch=*/1);
    metaCol->addLayout(row);
  }

  // ── Backdrop frame for the post-description metadata ───────────
  // Wraps the typed short fields AND the custom-fields section in a
  // distinct visual container with a flat alternate-base tint + soft
  // rounded border. Inside, FlowLayouts let the chips wrap as the
  // dialog is resized.
  auto *postDescFrame = new QFrame(m_liveMetadataGroup);
  postDescFrame->setObjectName(QStringLiteral("scraperMetadataBackdrop"));
  postDescFrame->setFrameShape(QFrame::NoFrame);
  postDescFrame->setStyleSheet(QStringLiteral("QFrame#scraperMetadataBackdrop {"
                                              "  background: palette(alternate-base);"
                                              "  border: 1px solid palette(midlight);"
                                              "  border-radius: 10px;"
                                              "}"));
  postDescFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  auto *postOuter = new QVBoxLayout(postDescFrame);
  postOuter->setContentsMargins(12, 14, 12, 14);
  postOuter->setSpacing(8);

  // Helper that builds a fixed-size "chip" container holding a
  // right-aligned label and a read-only QLineEdit. Returns the
  // wrapper widget that the FlowLayout treats as a single item.
  auto makeChipPair = [&](const QString &label, QLineEdit *edit) -> QWidget * {
    auto *w = new QWidget(postDescFrame);
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(4);
    auto *lbl = new QLabel(label, w);
    lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lbl->setFixedWidth(kLabelW);
    edit->setParent(w);
    edit->setFixedWidth(kValueChipW);
    h->addWidget(lbl);
    h->addWidget(edit);
    w->setFixedSize(kLabelW + 4 + kValueChipW, edit->sizeHint().height() + 2);
    return w;
  };

  // ── Combined chip flow ────────────────────────────────────────
  // Single FlowLayout for ALL short metadata chips: typed fields
  // first, then custom fields appended after. Sharing one layout
  // means custom-field chips fill in directly after Tags on the same
  // row (instead of always starting a new row), so the bottom row
  // never has just one orphaned chip when there's horizontal room.
  m_liveMetadataPublisher = new QLineEdit(postDescFrame);
  m_liveMetadataPublisher->setReadOnly(true);
  m_liveMetadataDeveloper = new QLineEdit(postDescFrame);
  m_liveMetadataDeveloper->setReadOnly(true);
  m_liveMetadataReleased = new QLineEdit(postDescFrame);
  m_liveMetadataReleased->setReadOnly(true);
  m_liveMetadataSource = new QLineEdit(postDescFrame);
  m_liveMetadataSource->setReadOnly(true);
  m_liveMetadataGenre = new QLineEdit(postDescFrame);
  m_liveMetadataGenre->setReadOnly(true);
  m_liveMetadataPlayers = new QLineEdit(postDescFrame);
  m_liveMetadataPlayers->setReadOnly(true);
  m_liveMetadataContentRating = new QLineEdit(postDescFrame);
  m_liveMetadataContentRating->setReadOnly(true);
  m_liveMetadataRuntime = new QLineEdit(postDescFrame);
  m_liveMetadataRuntime->setReadOnly(true);
  m_liveMetadataTags = new QLineEdit(postDescFrame);
  m_liveMetadataTags->setReadOnly(true);

  m_liveExtrasContainer = new QWidget(postDescFrame);
  // FlowLayout computes height-for-width so the container grows /
  // shrinks naturally as chips wrap on resize. No min height — that
  // would create dead space on wide windows.
  m_liveExtrasContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  auto *extrasLayout = new FlowLayout(m_liveExtrasContainer, /*margin=*/0,
                                      /*hSp=*/8, /*vSp=*/6);
  m_liveExtrasContainer->setLayout(extrasLayout);
  extrasLayout->addWidget(makeChipPair(tr("Publisher:"), m_liveMetadataPublisher));
  extrasLayout->addWidget(makeChipPair(tr("Developer:"), m_liveMetadataDeveloper));
  extrasLayout->addWidget(makeChipPair(tr("Released:"), m_liveMetadataReleased));
  extrasLayout->addWidget(makeChipPair(tr("Source:"), m_liveMetadataSource));
  extrasLayout->addWidget(makeChipPair(tr("Genre:"), m_liveMetadataGenre));
  extrasLayout->addWidget(makeChipPair(tr("Players:"), m_liveMetadataPlayers));
  extrasLayout->addWidget(makeChipPair(tr("Rating:"), m_liveMetadataContentRating));
  extrasLayout->addWidget(makeChipPair(tr("Runtime:"), m_liveMetadataRuntime));
  extrasLayout->addWidget(makeChipPair(tr("Tags:"), m_liveMetadataTags));
  // populateCustomFields appends custom-key chips AFTER these typed
  // chips. m_typedChipCount marks the boundary so re-renders only
  // tear down the custom chips, leaving typed chips in place.
  m_typedChipCount = extrasLayout->count();
  postOuter->addWidget(m_liveExtrasContainer);

  // Add the backdrop frame to the outer vertical column.
  metaCol->addWidget(postDescFrame);

  // Pre-seed the custom-fields section with every key the SS parser
  // can emit (gathered from screenscraperparser.cpp). The user sees
  // the full placeholder grid the moment the panel opens — values
  // fill in as the scrape produces them. Other providers that emit
  // keys outside this list still get rendered (populateCustomFields
  // adds new keys dynamically), the seeded list just covers the
  // common case so the section isn't empty on first open.
  static const QStringList kKnownSSCustomKeys = {
      QStringLiteral("classification_esrb"),
      QStringLiteral("classification_pegi"),
      QStringLiteral("classification_usk"),
      QStringLiteral("classification_cero"),
      QStringLiteral("cloneof"),
      QStringLiteral("colors"),
      QStringLiteral("controls"),
      QStringLiteral("families"),
      QStringLiteral("languages"),
      QStringLiteral("modes"),
      QStringLiteral("notgame"),
      QStringLiteral("rating"),
      QStringLiteral("region"),
      QStringLiteral("resolution"),
      QStringLiteral("rom_crc"),
      QStringLiteral("rom_filename"),
      QStringLiteral("rom_md5"),
      QStringLiteral("rom_serial"),
      QStringLiteral("rom_sha1"),
      QStringLiteral("rom_size"),
      QStringLiteral("rom_type"),
      QStringLiteral("rotation"),
      QStringLiteral("screenscraper_companyid"),
      QStringLiteral("screenscraper_groupid"),
      QStringLiteral("screenscraper_id"),
      QStringLiteral("topstaff"),
  };
  for (const QString &k : kKnownSSCustomKeys) m_allSeenCustomKeys.insert(k);
  // Initial render with empty values — populateCustomFields handles
  // the persistent-cell creation against the seeded union.
  populateCustomFields({});

  // No fixed min height — title row + description row + backdrop
  // FlowLayouts each compute their own height-for-width, so the
  // panel naturally grows on narrow widths (chips wrap to more
  // rows) and stays compact on wide widths.

  // Attach the typed-fields grid below the candidate row in the
  // group's outer vertical layout.
  metaOuter->addWidget(metaGridHost);

  m_liveMetadataGroup->hide();
  root->addWidget(m_liveMetadataGroup);

  // ── Live view: recent media thumbnails ──────────────────────────
  // Compact horizontal filmstrip — auto-scrolls to keep the newest
  // thumbnail visible, no manual scrollbars. Items are tightly
  // packed (small spacing + no text label) so several covers fit
  // across the dialog width.
  m_liveThumbsGroup = new QGroupBox(tr("Recent media"), m_unifiedPage);
  auto *thumbsLayout = new QVBoxLayout(m_liveThumbsGroup);
  thumbsLayout->setContentsMargins(4, 4, 4, 4);
  m_liveThumbsStrip = new QListWidget(m_liveThumbsGroup);
  m_liveThumbsStrip->setViewMode(QListView::IconMode);
  m_liveThumbsStrip->setIconSize(QSize(96, 96));
  m_liveThumbsStrip->setFlow(QListView::LeftToRight);
  m_liveThumbsStrip->setWrapping(false);
  m_liveThumbsStrip->setMovement(QListView::Static);
  m_liveThumbsStrip->setSelectionMode(QAbstractItemView::NoSelection);
  m_liveThumbsStrip->setMaximumHeight(108);
  m_liveThumbsStrip->setUniformItemSizes(true);
  m_liveThumbsStrip->setSpacing(2);
  // Slightly larger than icon so items fit snugly with minimal gap.
  m_liveThumbsStrip->setGridSize(QSize(100, 100));
  m_liveThumbsStrip->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_liveThumbsStrip->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_liveThumbsStrip->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  m_liveThumbsStrip->setFrameShape(QFrame::NoFrame);
  thumbsLayout->addWidget(m_liveThumbsStrip);
  m_liveThumbsGroup->hide();
  root->addWidget(m_liveThumbsGroup);

  // ── Progress + status (visible during scrape) ───────────────────
  m_unifiedCurrentLabel = new QLabel(m_unifiedPage);
  m_unifiedCurrentLabel->setWordWrap(true);
  m_unifiedCurrentLabel->hide();
  root->addWidget(m_unifiedCurrentLabel);

  m_unifiedProgressBar = new QProgressBar(m_unifiedPage);
  m_unifiedProgressBar->setRange(0, 100);
  m_unifiedProgressBar->setValue(0);
  m_unifiedProgressBar->setTextVisible(true);
  m_unifiedProgressBar->hide();
  root->addWidget(m_unifiedProgressBar);

  m_unifiedTimingLabel = new QLabel(m_unifiedPage);
  m_unifiedTimingLabel->setWordWrap(true);
  m_unifiedTimingLabel->hide();
  root->addWidget(m_unifiedTimingLabel);

  m_unifiedCountsLabel = new QLabel(m_unifiedPage);
  m_unifiedCountsLabel->hide();
  // The error count is rendered as a link when non-zero; rich text
  // is needed for the anchor. Clicking it opens the failure list.
  m_unifiedCountsLabel->setTextFormat(Qt::RichText);
  m_unifiedCountsLabel->setTextInteractionFlags(Qt::LinksAccessibleByMouse |
                                                Qt::LinksAccessibleByKeyboard);
  connect(m_unifiedCountsLabel, &QLabel::linkActivated, this,
          [this](const QString &) { showScrapeErrorDetails(); });
  root->addWidget(m_unifiedCountsLabel);

  // ScreenScraper request-quota readout. Hidden until a live scrape
  // delivers a valid quota via the service's quotaUpdated signal
  // (non-SS providers never do, so the row stays absent for them).
  m_unifiedQuotaLabel = new QLabel(m_unifiedPage);
  m_unifiedQuotaLabel->setWordWrap(true);
  m_unifiedQuotaLabel->hide();
  root->addWidget(m_unifiedQuotaLabel);
}

void ScrapeResultDialog::setScraperContext(const ScraperContext &ctx) {
  m_scraperCtx = ctx;
}

void ScrapeResultDialog::tickValueMarquees() {
  if (!m_liveMetadataGroup) return;
  // Defensive check — the hideEvent stops m_marqueeTimer, but if a
  // late-fired tick lands before that the visibility gate keeps us
  // from doing the findChildren-tree-walk on an invisible dialog.
  if (!isVisible()) return;
  const auto edits = m_liveMetadataGroup->findChildren<QLineEdit *>();
  for (auto *edit : edits) {
    if (!edit) continue;
    const QString text = edit->text();
    if (text.isEmpty()) continue;
    QFontMetrics fm(edit->font());
    const int textW = fm.horizontalAdvance(text);
    // -12 px accounts for the QLineEdit frame + horizontal padding;
    // close enough that "remainder fits" is true once the actual
    // tail of the string is visible.
    const int viewW = edit->width() - 12;
    if (textW <= viewW) {
      // No overflow; ensure we're at the start (in case the cell
      // previously held a longer value mid-marquee).
      if (edit->cursorPosition() != 0) edit->setCursorPosition(0);
      m_marqueePauseTicks.remove(edit);
      continue;
    }
    // Pause-mode: counting down at the rightmost-visible position.
    // When the counter hits zero, snap back to position 0 (no
    // reverse animation) so the marquee restarts L→R.
    if (m_marqueePauseTicks.contains(edit)) {
      int &pause = m_marqueePauseTicks[edit];
      if (--pause <= 0) {
        edit->setCursorPosition(0);
        m_marqueePauseTicks.remove(edit);
      }
      continue;
    }
    // Advancing phase: increment cursor (which scrolls the visible
    // region one character to the right). When the remainder of the
    // text fits in the visible viewport, hold for ~1.5 s before the
    // L→R wrap.
    const int curPos = edit->cursorPosition();
    const int remW = fm.horizontalAdvance(text.mid(curPos));
    if (remW <= viewW) {
      m_marqueePauseTicks.insert(edit, 10); // ~10 × 150 ms = 1.5 s hold
    } else {
      edit->setCursorPosition(qMin(curPos + 1, text.length()));
    }
  }
}

void ScrapeResultDialog::appendThumbAsync(const QString &path) {
  // QImage decode + smooth-scale run on the global QThreadPool;
  // QPixmap conversion stays on the main thread (QPixmap is GUI-
  // thread-only). The watcher is parented to `this` so a dialog
  // close before the worker finishes lets it be cleaned up — the
  // queued `finished` lambda never fires and the decoded bytes
  // are dropped harmlessly. Each completion appends one row and
  // auto-scrolls; out-of-order completion across concurrent items
  // is fine since the strip just shows "most recently completed".
  //
  // ScraperService::itemScraped reports every media path it just
  // wrote — covers, screenshots, AND the manual `.pdf`. Feeding a
  // PDF to QImage routes through Qt's libqpdf imageformats plugin,
  // which is PDFium-backed and calls abort() on some inputs; the
  // SIGTRAP took down kartend at the 1681/1878 mark of a 3 h scrape
  // (Kartend-wquq). Gate on the same helper artworkloaddispatcher
  // already uses so non-image media silently skip the thumb strip.
  if (!ExtensionUtils::isDecodableImagePath(path)) {
    return;
  }
  auto *watcher = new QFutureWatcher<QPair<QString, QImage>>(this);
  connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher]() {
    watcher->deleteLater();
    if (!isVisible()) return;
    const auto pair = watcher->result();
    if (pair.second.isNull()) return;
    auto *row =
        new QListWidgetItem(QIcon(QPixmap::fromImage(pair.second)), QString(), m_liveThumbsStrip);
    row->setToolTip(QFileInfo(pair.first).fileName());
    while (m_liveThumbsStrip->count() > 12) {
      delete m_liveThumbsStrip->takeItem(0);
    }
    m_liveThumbsStrip->scrollToItem(row, QAbstractItemView::PositionAtBottom);
  });
  watcher->setFuture(QtConcurrent::run([path]() {
    QImage img(path);
    if (img.isNull()) return qMakePair(path, QImage());
    return qMakePair(path, img.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  }));
}

void ScrapeResultDialog::applyScrapedItemToLive(const Scraper::ScrapedItem &item) {
  // New item → reset every marquee back to position 0 so each cell
  // restarts the L→R animation from the head of the new value.
  m_marqueePauseTicks.clear();
  auto setFromStart = [](QLineEdit *e, const QString &t) {
    e->setText(t);
    e->setCursorPosition(0);
  };
  setFromStart(m_liveMetadataTitle, item.title);
  setFromStart(m_liveMetadataPublisher, item.publisher);
  setFromStart(m_liveMetadataReleased, item.releaseDate);
  setFromStart(m_liveMetadataDeveloper, item.developer);
  setFromStart(m_liveMetadataGenre, item.genre);
  setFromStart(m_liveMetadataPlayers, item.players);
  setFromStart(m_liveMetadataContentRating, item.contentRating);
  setFromStart(m_liveMetadataRuntime, item.runtimeSeconds > 0
                                          ? formatDuration(qint64(item.runtimeSeconds) * 1000LL)
                                          : QString());
  setFromStart(m_liveMetadataTags, item.tagsJson);
  setFromStart(m_liveMetadataSource, item.sourceProviderId);
  m_liveMetadataDescription->setText(item.description);
  populateCustomFields(item.customFields);
}

void ScrapeResultDialog::interactiveFetchDetail(int idx) {
  if (idx < 0 || idx >= m_candidates.size()) return;
  if (!m_provider) return;
  if (m_applyButton) m_applyButton->setEnabled(false);
  const auto cand = m_candidates[idx];
  m_currentRow = idx;
  QPointer<ScrapeResultDialog> guard(this);
  m_provider->fetchDetail(cand, [guard, idx](ErrorUtils::Result<Scraper::ScrapedItem> r) {
    if (guard.isNull()) return;
    // Ignore stale callbacks if the user
    // switched candidates while this fetch
    // was in flight.
    if (guard->m_currentRow != idx) return;
    if (r.isError()) {
      QMessageBox::warning(guard, tr("Scrape failed"), r.error().message);
      return;
    }
    guard->m_currentDetail = r.value();
    guard->applyScrapedItemToLive(r.value());
    if (guard->m_applyButton) guard->m_applyButton->setEnabled(true);
  });
}

namespace {
/// Shorten provider-specific keys for the label cell so every label
/// fits in the fixed label-column width. Common SS prefixes
/// (classification_/screenscraper_/rom_) get stripped because they
/// add no info once the keys are grouped together. Underscores
/// collapse to spaces. Full original key still shows in the cell's
/// tooltip via the caller.
QString prettifyCustomKey(const QString &key) {
  QString r = key;
  if (r.startsWith(QLatin1String("classification_"))) {
    r = r.mid(QStringLiteral("classification_").length()).toUpper();
  } else if (r.startsWith(QLatin1String("screenscraper_"))) {
    r = r.mid(QStringLiteral("screenscraper_").length());
  } else if (r.startsWith(QLatin1String("rom_"))) {
    r = r.mid(QStringLiteral("rom_").length());
  }
  r.replace(QLatin1Char('_'), QLatin1Char(' '));
  return r;
}
} // namespace

void ScrapeResultDialog::populateCustomFields(const QHash<QString, QString> &fields) {
  if (!m_liveExtrasContainer || !m_liveExtrasContainer->layout()) return;
  auto *layout = m_liveExtrasContainer->layout();
  if (!layout) return;
  // Merge incoming keys into the session-wide union so new keys
  // emitted mid-scrape get a placeholder chip added to the flow.
  bool newKeyAdded = false;
  for (auto it = fields.constBegin(); it != fields.constEnd(); ++it) {
    if (!m_allSeenCustomKeys.contains(it.key())) {
      m_allSeenCustomKeys.insert(it.key());
      newKeyAdded = true;
    }
  }
  // First-pass build or new-key arrival → rebuild every chip-pair so
  // sorted-key order stays stable across the full union. Only the
  // chips from m_typedChipCount onward are torn down — the leading
  // typed-field chips (Publisher … Tags) stay in place.
  if (m_customFieldEdits.isEmpty() || newKeyAdded) {
    while (layout->count() > m_typedChipCount) {
      QLayoutItem *child = layout->takeAt(m_typedChipCount);
      if (!child) break;
      if (auto *w = child->widget()) w->deleteLater();
      delete child;
    }
    m_customFieldEdits.clear();
    QStringList keys = m_allSeenCustomKeys.values();
    std::sort(keys.begin(), keys.end());
    // Custom-field cells share the EXACT same label + chip widths as
    // the typed-fields chips above so columns align visually across
    // both flows.
    constexpr int kCustomLabelW = 90;
    constexpr int kCustomValueW = 90;
    for (const QString &key : keys) {
      auto *chip = new QWidget(m_liveExtrasContainer);
      auto *h = new QHBoxLayout(chip);
      h->setContentsMargins(0, 0, 0, 0);
      h->setSpacing(4);
      auto *lbl = new QLabel(prettifyCustomKey(key) + QLatin1Char(':'), chip);
      lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
      lbl->setFixedWidth(kCustomLabelW);
      lbl->setToolTip(key);
      auto *edit = new QLineEdit(chip);
      edit->setReadOnly(true);
      edit->setFrame(true);
      edit->setFixedWidth(kCustomValueW);
      h->addWidget(lbl);
      h->addWidget(edit);
      chip->setFixedSize(kCustomLabelW + 4 + kCustomValueW, edit->sizeHint().height() + 2);
      layout->addWidget(chip);
      m_customFieldEdits.insert(key, edit);
    }
  }
  // Update value text in every persistent cell — empty string for
  // keys absent from the current item so the slot stays visible but
  // blank rather than disappearing.
  for (auto it = m_customFieldEdits.constBegin(); it != m_customFieldEdits.constEnd(); ++it) {
    const QString value = fields.value(it.key()).trimmed();
    it.value()->setText(value);
    it.value()->setCursorPosition(0);
    it.value()->setToolTip(value);
  }
}

void ScrapeResultDialog::startUnifiedScrape(int preCollectionIndex, const QString &preItemPath) {
  m_mode = Mode::Unified;
  m_unifiedPhase = UnifiedPhase::Setup;
  m_modeStack->setCurrentWidget(m_unifiedPage);
  m_applyButton->hide();
  // Repurpose the dialog button area: hide the legacy Apply, show a
  // dedicated Scrape button + a Close button. Close hides the dialog
  // (the ScraperService keeps running); Cancel stops the active
  // scrape entirely.
  if (!m_scrapeButton) {
    auto *box = qobject_cast<QDialogButtonBox *>(m_applyButton->parent());
    if (box) {
      m_scrapeButton = box->addButton(tr("Scrape"), QDialogButtonBox::ActionRole);
      connect(m_scrapeButton, &QPushButton::clicked, this, &ScrapeResultDialog::onScrapeClicked);
      m_closeButton = box->addButton(tr("Close"), QDialogButtonBox::ActionRole);
      m_closeButton->setToolTip(tr("Hide this window. The scrape keeps running in the background; "
                                   "reopen Scraper from the File menu to see progress."));
      connect(m_closeButton, &QPushButton::clicked, this, [this]() {
        // Mid-interactive close → tell the service to pause so it
        // doesn't fire the next item's picker into a vanished UI.
        // Auto mode + idle: just hide.
        if (m_service && m_service->state() == Scraper::ScraperService::State::RunningInteractive) {
          m_service->pauseInteractive();
        }
        hide();
      });
      m_closeButton->hide();
    }
  }
  if (m_scrapeButton) m_scrapeButton->show();
  // Re-attach to a running service if there is one — we should
  // come up directly in the Live view, not the Setup view. Don't
  // touch the tree / items state in that case: the user might have
  // configured a selection earlier and we shouldn't overwrite it
  // mid-run. Setup view rebuilds the tree as it always has.
  if (m_service && m_service->isActive()) {
    setUnifiedSetupEnabled(false);
    if (m_closeButton) m_closeButton->show();
    // Start the 1-second live tick (the scrapeStarted handler missed
    // this run because we connected after it already fired).
    m_rateSamples.clear();
    if (!m_liveTickTimer) {
      m_liveTickTimer = new QTimer(this);
      m_liveTickTimer->setInterval(1000);
      connect(m_liveTickTimer, &QTimer::timeout, this,
              &ScrapeResultDialog::updateUnifiedProgressLabel);
    }
    m_liveTickTimer->start();
    if (!m_marqueeTimer) {
      m_marqueeTimer = new QTimer(this);
      m_marqueeTimer->setInterval(150);
      connect(m_marqueeTimer, &QTimer::timeout, this, &ScrapeResultDialog::tickValueMarquees);
    }
    m_marqueePauseTicks.clear();
    m_marqueeTimer->start();
    // Sync the Live view from the service's current snapshot so the
    // user immediately sees where the scrape is sitting instead of
    // waiting for the next signal tick.
    updateUnifiedProgressLabel();
    applyScrapedItemToLive(m_service->lastScrapedItem());
    m_unifiedCurrentLabel->setText(tr("Collection: %1 — scraping: %2")
                                       .arg(m_service->currentCollectionName(),
                                            QFileInfo(m_service->currentItemPath()).fileName()));
    m_shownCollectionName = m_service->currentCollectionName();
    // Restore the recent-media thumbnail strip from the service so
    // the user doesn't see an empty band when re-entering. Icon-only
    // rows + auto-scroll to the latest match the same shape as the
    // itemCompleted-driven append path.
    m_liveThumbsStrip->clear();
    QListWidgetItem *restoreLast = nullptr;
    for (const QString &p : m_service->recentMediaPaths()) {
      if (p.isEmpty()) continue;
      // Same PDFium-abort guard as appendThumbAsync — recentMediaPaths
      // mirrors what itemScraped emits, so it includes the manual
      // `.pdf` for every game. QPixmap here runs on the MAIN UI
      // thread; a SIGTRAP here would crash the dialog before the user
      // could even close it.
      if (!ExtensionUtils::isDecodableImagePath(p)) continue;
      QPixmap pm(p);
      if (pm.isNull()) continue;
      auto *row = new QListWidgetItem(
          QIcon(pm.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation)), QString(),
          m_liveThumbsStrip);
      row->setToolTip(QFileInfo(p).fileName());
      restoreLast = row;
    }
    if (restoreLast) {
      m_liveThumbsStrip->scrollToItem(restoreLast, QAbstractItemView::PositionAtBottom);
    }
    // If paused mid-interactive, kick service back into action so the
    // user's resumed UI gets a picker.
    if (m_service->state() == Scraper::ScraperService::State::PausedInteractive) {
      m_service->resumePaused();
    }
    return; // skip populateCollectionTree — keep existing selection
  }
  setUnifiedSetupEnabled(true);
  populateCollectionTree();

  // Right-click flow: pre-check exactly the requested collection +
  // its single item, leaving every other collection in the unchecked
  // default state.
  if (preCollectionIndex >= 0 && m_scraperCtx.collections &&
      preCollectionIndex < m_scraperCtx.collections->size()) {
    for (auto it = m_treeItemToCollectionIndex.constBegin();
         it != m_treeItemToCollectionIndex.constEnd(); ++it) {
      if (it.value() == preCollectionIndex) {
        QSignalBlocker b(m_collectionTree);
        it.key()->setCheckState(0, Qt::Checked);
        m_collectionTree->setCurrentItem(it.key());
        if (!preItemPath.isEmpty()) {
          // Seed the inclusion list with the single requested path so
          // the items-list rebuild ticks only that row.
          m_itemSelectionByCollection[preCollectionIndex] = {preItemPath};
          // Pre-populate the items cache too so we don't need a DB
          // round-trip to display the row immediately.
          if (!m_itemsCacheByCollection.contains(preCollectionIndex)) {
            m_itemsCacheByCollection[preCollectionIndex] = {preItemPath};
          }
        }
        rebuildItemsList(preCollectionIndex);
        break;
      }
    }
  }
}

void ScrapeResultDialog::populateCollectionTree() {
  m_collectionTree->clear();
  m_treeItemToCollectionIndex.clear();
  if (!m_scraperCtx.collections) return;
  const auto &cols = *m_scraperCtx.collections;

  // Build a parent-aware QTreeWidget mirroring the collection
  // hierarchy via CollectionConfig::parentCollectionIndex (root rows
  // are pi == -1). Multi-pass placement: keep iterating until every
  // collection has been parented, since the source list isn't sorted
  // topologically. Bounded by depth; orphans (out-of-range parent)
  // get re-rooted as a defensive last pass.
  QHash<int, QTreeWidgetItem *> itemByIndex;
  QSignalBlocker b(m_collectionTree);
  int remaining = cols.size();
  while (remaining > 0) {
    bool progress = false;
    for (int i = 0; i < cols.size(); ++i) {
      if (itemByIndex.contains(i)) continue;
      const int pi = cols[i].parentCollectionIndex;
      QTreeWidgetItem *item = nullptr;
      if (pi < 0) {
        item = new QTreeWidgetItem(m_collectionTree);
      } else if (itemByIndex.contains(pi)) {
        item = new QTreeWidgetItem(itemByIndex.value(pi));
      } else {
        continue; // parent not placed yet — try again next pass
      }
      item->setText(0, cols[i].name);
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
      item->setCheckState(0, Qt::Unchecked);
      itemByIndex.insert(i, item);
      m_treeItemToCollectionIndex.insert(item, i);
      --remaining;
      progress = true;
    }
    if (!progress) break; // safety: orphan / cycle — bail to the rescue loop.
  }
  for (int i = 0; i < cols.size(); ++i) {
    if (itemByIndex.contains(i)) continue;
    auto *item = new QTreeWidgetItem(m_collectionTree);
    item->setText(0, cols[i].name);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(0, Qt::Unchecked);
    m_treeItemToCollectionIndex.insert(item, i);
  }
  m_collectionTree->expandAll();
}

void ScrapeResultDialog::onCollectionTreeCurrentChanged(QTreeWidgetItem *current,
                                                        QTreeWidgetItem *) {
  if (!current) {
    m_itemsHeaderLabel->setText(tr("Select a collection to see its items."));
    m_unifiedItemsList->clear();
    return;
  }
  const int idx = m_treeItemToCollectionIndex.value(current, -1);
  if (idx < 0) return;
  rebuildItemsList(idx);
}

void ScrapeResultDialog::applyCollectionCheckState(int collectionIndex, bool checked) {
  if (collectionIndex < 0) return;
  if (checked) {
    // Newly-checked collection: default the inclusion set to "every
    // item we know about". The items list rebuild ticks each row.
    if (m_itemsCacheByCollection.contains(collectionIndex)) {
      m_itemSelectionByCollection[collectionIndex] =
          m_itemsCacheByCollection.value(collectionIndex);
    } else {
      // Empty entry signals "include all" until the DB lookup lands;
      // the rebuildItemsList call kicks the DB fetch which populates
      // both caches once paths arrive. Without that fetch
      // m_itemSelectionByCollection[idx] would stay empty and the
      // Scrape button would silently no-op for this collection.
      m_itemSelectionByCollection.insert(collectionIndex, QStringList());
      rebuildItemsList(collectionIndex);
    }
  } else {
    m_itemSelectionByCollection.remove(collectionIndex);
  }
}

void ScrapeResultDialog::onCollectionCheckChanged(QTreeWidgetItem *item, int column) {
  if (column != 0) return;
  const int idx = m_treeItemToCollectionIndex.value(item, -1);
  if (idx < 0) return;
  const bool checked = item->checkState(0) == Qt::Checked;
  applyCollectionCheckState(idx, checked);

  // Cascade the new state through the whole subtree: checking a parent
  // collection selects its subcollections too, unchecking clears them.
  // Tree signals are blocked while the child check states are written
  // so this doesn't re-enter once per child — the per-collection
  // bookkeeping is applied directly instead.
  QSet<int> affected{idx};
  {
    QSignalBlocker blocker(m_collectionTree);
    std::function<void(QTreeWidgetItem *)> cascade = [&](QTreeWidgetItem *parent) {
      for (int i = 0; i < parent->childCount(); ++i) {
        QTreeWidgetItem *child = parent->child(i);
        child->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
        const int childIdx = m_treeItemToCollectionIndex.value(child, -1);
        if (childIdx >= 0) {
          applyCollectionCheckState(childIdx, checked);
          affected.insert(childIdx);
        }
        cascade(child);
      }
    };
    cascade(item);
  }

  // Refresh the items list if the collection currently on screen is
  // one the cascade just touched.
  const auto *cur = m_collectionTree->currentItem();
  const int curIdx =
      cur ? m_treeItemToCollectionIndex.value(const_cast<QTreeWidgetItem *>(cur), -1) : -1;
  if (curIdx >= 0 && affected.contains(curIdx)) {
    rebuildItemsList(curIdx);
  }
}

void ScrapeResultDialog::rebuildItemsList(int collectionIndex) {
  if (!m_scraperCtx.collections || collectionIndex < 0 ||
      collectionIndex >= m_scraperCtx.collections->size()) {
    return;
  }
  const CollectionConfig &cfg = (*m_scraperCtx.collections)[collectionIndex];
  m_itemsHeaderLabel->setText(tr("Items in '%1'").arg(cfg.name));

  // Fetch from DB on first display per session; cache for subsequent
  // tree clicks. Fetch is async — populate cache from the response.
  if (!m_itemsCacheByCollection.contains(collectionIndex)) {
    m_unifiedItemsList->clear();
    auto *placeholder = new QListWidgetItem(tr("Loading items…"), m_unifiedItemsList);
    placeholder->setFlags(placeholder->flags() & ~Qt::ItemIsEnabled);
    if (!m_scraperCtx.databaseManager || !m_scraperCtx.collections) return;
    auto *db = m_scraperCtx.databaseManager;
    CollectionContext context;
    context.config = cfg;
    context.currentIndex = collectionIndex;
    QPointer<ScrapeResultDialog> guard(this);
    auto *connHolder = new QObject(this);
    QObject::connect(
        db, &IDatabaseManager::itemsRangeLoaded, connHolder,
        [guard, connHolder, collectionIndex](
            int /*offset*/, const QStringList &filePaths, const QHash<QString, QString> &,
            const QHash<QString, QString> &, const QHash<QString, QString> &,
            const QHash<QString, int> &fileToCollectionIndex, int requestedCollectionIndex) {
          // itemsRangeLoaded is a shared signal: when a parent collection is
          // cascade-checked the dialog has one fetchItemsRange in flight per
          // collection, and every connected handler sees every emission.
          // Consume ONLY the result for the collection this fetch asked for —
          // otherwise this collection's cache gets populated from another
          // collection's items (the whole-parent-group over-count bug).
          if (requestedCollectionIndex != collectionIndex) return;
          connHolder->deleteLater();
          if (guard.isNull()) return;
          guard->m_itemsCacheByCollection[collectionIndex] = filePaths;
          // Retain each item's owning-collection index so a
          // scrape of a shell parent routes per item rather
          // than dumping everything on the parent.
          guard->m_itemOwnerByCollection[collectionIndex] = fileToCollectionIndex;
          // If the collection was checked before items landed,
          // populate the inclusion set with the full list now.
          if (guard->m_itemSelectionByCollection.contains(collectionIndex) &&
              guard->m_itemSelectionByCollection.value(collectionIndex).isEmpty()) {
            guard->m_itemSelectionByCollection[collectionIndex] = filePaths;
          }
          // Only re-render if the user is still viewing this collection.
          const auto *cur = guard->m_collectionTree->currentItem();
          const int curIdx =
              cur ? guard->m_treeItemToCollectionIndex.value(const_cast<QTreeWidgetItem *>(cur), -1)
                  : -1;
          if (curIdx == collectionIndex) {
            guard->rebuildItemsList(collectionIndex);
          }
        });
    db->fetchItemsRange(context, *m_scraperCtx.collections, 0, std::numeric_limits<int>::max(),
                        QString());
    return;
  }

  // Cache hit — render synchronously.
  const QStringList &paths = m_itemsCacheByCollection.value(collectionIndex);
  const auto *treeRow = [&]() -> QTreeWidgetItem * {
    for (auto it = m_treeItemToCollectionIndex.constBegin();
         it != m_treeItemToCollectionIndex.constEnd(); ++it) {
      if (it.value() == collectionIndex) return it.key();
    }
    return nullptr;
  }();
  const bool collectionChecked = treeRow && treeRow->checkState(0) == Qt::Checked;
  const QStringList &included = m_itemSelectionByCollection.value(collectionIndex);
  const QSet<QString> includedSet(included.begin(), included.end());

  QSignalBlocker b(m_unifiedItemsList);
  m_unifiedItemsList->clear();
  for (const QString &path : paths) {
    auto *row = new QListWidgetItem(QFileInfo(path).fileName(), m_unifiedItemsList);
    row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
    row->setData(Qt::UserRole, path);
    if (!collectionChecked) {
      row->setCheckState(Qt::Unchecked);
      row->setFlags(row->flags() & ~Qt::ItemIsEnabled);
    } else {
      row->setCheckState(includedSet.contains(path) ? Qt::Checked : Qt::Unchecked);
    }
  }
}

void ScrapeResultDialog::onItemCheckChanged(QListWidgetItem *item) {
  if (!item) return;
  const auto *cur = m_collectionTree->currentItem();
  if (!cur) return;
  const int idx = m_treeItemToCollectionIndex.value(const_cast<QTreeWidgetItem *>(cur), -1);
  if (idx < 0) return;
  const QString path = item->data(Qt::UserRole).toString();
  if (path.isEmpty()) return;
  QStringList included = m_itemSelectionByCollection.value(idx);
  if (item->checkState() == Qt::Checked) {
    if (!included.contains(path)) included.append(path);
  } else {
    included.removeAll(path);
  }
  m_itemSelectionByCollection[idx] = included;
}

void ScrapeResultDialog::setUnifiedSetupEnabled(bool enabled) {
  // Setup widgets — collection tree, items list, media-types group,
  // mode toggle — are HIDDEN while a scrape is running. The Live view
  // (metadata + thumbnails + progress) takes their place and gets the
  // full vertical space. Setup widgets reappear when the scrape ends.
  if (m_scrapeButton) m_scrapeButton->setEnabled(enabled);
  if (m_unifiedSplitterContainer) m_unifiedSplitterContainer->setVisible(enabled);
  if (m_mediaTypesGroup) m_mediaTypesGroup->setVisible(enabled);
  if (m_modeRowContainer) m_modeRowContainer->setVisible(enabled);

  const bool showProgress = !enabled;
  m_unifiedCurrentLabel->setVisible(showProgress);
  m_unifiedProgressBar->setVisible(showProgress);
  m_unifiedTimingLabel->setVisible(showProgress);
  m_unifiedCountsLabel->setVisible(showProgress);
  // The quota label is shown only once a live scrape reports a valid
  // quota (the quotaUpdated handler reveals it). Returning to the
  // setup view always hides it; entering the live view leaves it
  // hidden until that first quota update arrives.
  if (m_unifiedQuotaLabel && !showProgress) m_unifiedQuotaLabel->hide();
  if (m_liveMetadataGroup) m_liveMetadataGroup->setVisible(showProgress);
  if (m_liveThumbsGroup) m_liveThumbsGroup->setVisible(showProgress);
  if (m_closeButton) m_closeButton->setVisible(showProgress);
  // No explicit layout invalidation — Qt handles re-flow when widgets
  // hide/show, and forcing it was contributing to the dialog auto-
  // resizing on every toggle. The minimum-size pin in the ctor keeps
  // the window stable.
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
  // Connect to every signal the dialog needs to keep its Live view
  // in sync. UniqueConnection so callers re-setting the same service
  // (e.g. a stale dialog handing off to a fresh one) don't double-
  // wire.
  connect(m_service, &Scraper::ScraperService::scrapeStarted, this, [this](int total) {
    qCInfo(lcDialogTimings) << "DIALOG service.scrapeStarted total=" << total;
    setUnifiedSetupEnabled(false);
    m_unifiedProgressBar->setRange(0, std::max(1, total));
    m_unifiedProgressBar->setValue(m_service->itemsCompleted());
    // Reset rate-window samples. The seen-keys union is left
    // intact so the pre-seeded known SS keys (plus any keys
    // accumulated during prior runs in this session) stay
    // visible — values clear naturally as each item rewrites
    // them.
    m_rateSamples.clear();
    if (!m_liveTickTimer) {
      m_liveTickTimer = new QTimer(this);
      m_liveTickTimer->setInterval(1000);
      connect(m_liveTickTimer, &QTimer::timeout, this,
              &ScrapeResultDialog::updateUnifiedProgressLabel);
    }
    m_liveTickTimer->start();
    // Value-marquee timer: scrolls overflowing chip text L→R
    // then wraps. Lazy-init on first scrapeStart.
    if (!m_marqueeTimer) {
      m_marqueeTimer = new QTimer(this);
      m_marqueeTimer->setInterval(150);
      connect(m_marqueeTimer, &QTimer::timeout, this, &ScrapeResultDialog::tickValueMarquees);
    }
    m_marqueePauseTicks.clear();
    m_marqueeTimer->start();
    updateUnifiedProgressLabel();
  });
  connect(m_service, &Scraper::ScraperService::itemBegan, this,
          [this](int done, int total, const QString &collectionName, const QString &name) {
            Q_UNUSED(total);
            Q_UNUSED(done);
            // Hidden dialog → skip the label-update work. With high
            // batchItemConcurrency this fires several times per second;
            // not worth updating widgets nobody can see.
            if (!isVisible()) return;
            qCDebug(lcDialogTimings) << "DIALOG service.itemBegan name=" << name;
            // Refresh the collection label HERE, not only in the
            // itemCompleted handler below: itemCompleted fires only on a
            // successful scrape, so a collection whose items all error
            // (or all skip) would otherwise leave the label frozen on the
            // last collection that produced a success while the scrape
            // churns on. itemBegan fires for every item whatever the
            // outcome. Gated on an actual collection change so
            // batchItemConcurrency > 1 doesn't re-set the label as each
            // parallel item in the same collection starts.
            if (collectionName != m_shownCollectionName) {
              m_shownCollectionName = collectionName;
              m_unifiedCurrentLabel->setText(tr("Collection: %1").arg(collectionName));
            }
            // The metadata panel and the richer "last scraped" label form
            // are still updated together by the itemCompleted handler so
            // they always describe the same completed item — deliberately
            // not touched here, where many parallel items mid-lookup would
            // wipe the panel every few hundred ms.
            updateUnifiedProgressLabel();
          });
  connect(m_service, &Scraper::ScraperService::itemCompleted, this,
          [this](int done, int total, const Scraper::ScrapedItem &scraped,
                 const QStringList &mediaPaths) {
            Q_UNUSED(done);
            Q_UNUSED(total);
            // Hidden dialog → skip every UI update. The service still
            // tracks recentMediaPaths + lastScrapedItem internally, and
            // startUnifiedScrape rebuilds the thumb strip + metadata
            // from that snapshot when the dialog is reopened. No visible
            // work means no reason to decode + smooth-scale thumbnails
            // on the main thread per completed item.
            if (!isVisible()) return;
            // Don't poke the progress bar here — updateUnifiedProgressLabel
            // (called below) is the single source of truth and reads
            // counters straight from the service. Doing both used to
            // race: this slot would set the value, then the helper would
            // reset it from the legacy m_unifiedItemsCompletedAcross
            // (always 0 in service mode), so the bar stayed at zero.
            // Shared field-population path so auto and interactive
            // modes render to identical widgets.
            applyScrapedItemToLive(scraped);
            // Sync the "currently scraping" label with whatever just
            // landed in the metadata panel — both update together so
            // label and fields always describe the same item even
            // when concurrency has many items in flight.
            QString displayName = scraped.title;
            if (displayName.isEmpty() && !mediaPaths.isEmpty()) {
              displayName = QFileInfo(mediaPaths.first()).completeBaseName();
            }
            if (displayName.isEmpty()) {
              displayName = QFileInfo(m_service->currentItemPath()).fileName();
            }
            m_unifiedCurrentLabel->setText(
                tr("Collection: %1 — last scraped: %2")
                    .arg(m_service->currentCollectionName(), displayName));
            // Append new media paths to the thumb strip via async
            // decode/scale (off the UI thread). Each completed
            // decode auto-scrolls the strip to its own freshly-added
            // row, so the newest cover is always visible. The strip
            // is icon-only — less crowded, fits more thumbnails —
            // and bounded inside the watcher's finished slot so a
            // long batch doesn't grow it unbounded.
            for (const QString &p : mediaPaths) {
              if (p.isEmpty()) continue;
              appendThumbAsync(p);
            }
            updateUnifiedProgressLabel();
          });
  connect(m_service, &Scraper::ScraperService::pickerNeeded, this,
          [this](const QString &itemPath, const QString &itemName,
                 const QList<Scraper::ScrapeCandidate> &candidates,
                 std::shared_ptr<MetadataLookupProvider> provider, const QString &artworkDir) {
            Q_UNUSED(artworkDir);
            Q_UNUSED(itemName);
            // Stay on the unified live view (don't flip to the legacy
            // single-item page). Surface a candidate combo at the top
            // of the metadata panel; the existing live fields show
            // the selected candidate's data; Apply button confirms.
            m_unifiedPhase = UnifiedPhase::InteractivePicking;
            m_mode = Mode::Unified;
            m_interactiveProvider = provider;
            m_interactiveItems = {itemPath};
            m_interactiveCursor = 0;
            m_provider = provider.get();
            m_candidates = candidates;
            m_detailCache.clear();
            m_currentRow = -1;
            m_currentDetail = Scraper::ScrapedItem();
            m_mediaRows.clear();
            // Populate the candidate combo; block signals during the
            // refill so the first-row change doesn't trigger a stray
            // detail fetch before we explicitly call it below.
            {
              QSignalBlocker blocker(m_interactiveCandidateCombo);
              m_interactiveCandidateCombo->clear();
              for (const auto &c : m_candidates) {
                QString label = c.displayName;
                if (!c.subtitle.isEmpty()) label += QStringLiteral(" — ") + c.subtitle;
                if (c.matchScore >= 0) label += QStringLiteral("  (%1)").arg(c.matchScore);
                m_interactiveCandidateCombo->addItem(label);
              }
            }
            m_interactiveCandidateRow->setVisible(m_candidates.size() > 0);
            if (m_applyButton) {
              m_applyButton->show();
              m_applyButton->setEnabled(false);
            }
            if (m_scrapeButton) m_scrapeButton->hide();
            // Fetch detail for the first candidate to populate the
            // live fields. Apply enables when detail lands.
            if (!m_candidates.isEmpty()) interactiveFetchDetail(0);
          });
  connect(m_service, &Scraper::ScraperService::scrapeFinished, this,
          [this](const Scraper::ScraperService::Summary &s) {
            qCInfo(lcDialogTimings) << "DIALOG service.scrapeFinished scraped=" << s.scraped
                                    << "skipped=" << s.skipped << "errors=" << s.errors;
            if (m_liveTickTimer) m_liveTickTimer->stop();
            if (m_marqueeTimer) m_marqueeTimer->stop();
            m_marqueePauseTicks.clear();
            if (m_interactiveCandidateRow) m_interactiveCandidateRow->hide();
            // Reset phase so a subsequent Scrape click isn't rejected by
            // the "if (m_unifiedPhase != Setup) return;" guard in
            // onScrapeClicked. Interactive runs leave the phase at
            // InteractivePicking; auto runs leave it at Setup. We
            // unconditionally snap back here.
            m_unifiedPhase = UnifiedPhase::Setup;
            setUnifiedSetupEnabled(true);
            if (m_scrapeButton) m_scrapeButton->show();
            if (m_applyButton) m_applyButton->hide();
            // Quota-exhausted stop: setUnifiedSetupEnabled(true) hid
            // the progress label, but the user needs to see WHY the
            // scrape ended early — and when they can resume. Re-show
            // the current-status label with the quota message. The
            // reset time comes from the live quota readout when we
            // have one (the label still holds it); otherwise fall
            // back to the generic "midnight UTC" wording.
            if (s.quotaExhausted) {
              // m_lastQuotaResetText is the local-time HH:mm captured
              // from the last live quota update; fall back to the
              // generic wording when no quota update arrived (e.g.
              // the very first item hit 430 before any ssuser block
              // was parsed).
              const QString resetText =
                  m_lastQuotaResetText.isEmpty() ? tr("midnight UTC") : m_lastQuotaResetText;
              m_unifiedCurrentLabel->setText(
                  tr("Scrape stopped — ScreenScraper's daily quota is exhausted. "
                     "Resume after it resets (%1).")
                      .arg(resetText));
              m_unifiedCurrentLabel->show();
            }
            emit unifiedScrapeFinished(s.scraped, s.skipped, s.errors, s.firstFailures);
          });
  connect(m_service, &Scraper::ScraperService::scrapePaused, this, [this]() {
    m_unifiedCurrentLabel->setText(tr("Scrape paused — close to keep paused, or "
                                      "reopen to continue."));
  });
  connect(m_service, &Scraper::ScraperService::quotaUpdated, this,
          [this](const Scraper::QuotaStatus &quota) {
            if (!m_unifiedQuotaLabel) return;
            // dailyMax 0 = quota unknown (SS didn't report a ceiling);
            // keep the row hidden rather than showing "N / 0".
            if (!quota.valid || quota.dailyMax <= 0) {
              m_unifiedQuotaLabel->hide();
              return;
            }
            m_lastQuotaResetText = quota.resetAtUtc.toLocalTime().toString(QStringLiteral("HH:mm"));
            m_unifiedQuotaLabel->setText(tr("ScreenScraper: %1 / %2 requests today · resets %3")
                                             .arg(quota.dailyUsed)
                                             .arg(quota.dailyMax)
                                             .arg(m_lastQuotaResetText));
            m_unifiedQuotaLabel->show();
          });
}

int ScrapeResultDialog::totalCheckedItemCount() const {
  int total = 0;
  for (auto it = m_itemSelectionByCollection.constBegin();
       it != m_itemSelectionByCollection.constEnd(); ++it) {
    total += it.value().size();
  }
  return total;
}

void ScrapeResultDialog::updateUnifiedProgressLabel() {
  // Service-driven path: counters live on the ScraperService, not on
  // the legacy m_unified* fields. Read from whichever is the source
  // of truth for the active run.
  int total = 0;
  int done = 0;
  qint64 startMs = 0;
  int scraped = 0;
  int skipped = 0;
  int errors = 0;
  if (m_service && m_service->isActive()) {
    total = m_service->totalItems();
    done = m_service->itemsCompleted();
    startMs = m_service->startedAtUnixMs();
    const auto s = m_service->summary();
    scraped = s.scraped;
    skipped = s.skipped;
    errors = s.errors;
  } else {
    total = totalCheckedItemCount();
    done = m_unifiedItemsCompletedAcross;
    startMs = m_unifiedStartMs;
    scraped = m_unifiedScrapedTotal;
    skipped = m_unifiedSkippedTotal;
    errors = m_unifiedErrorsTotal;
  }
  if (total <= 0) return;
  m_unifiedProgressBar->setRange(0, total);
  m_unifiedProgressBar->setValue(done);
  const qint64 elapsedMs = std::max<qint64>(1, QDateTime::currentMSecsSinceEpoch() - startMs);
  QString etaStr = QStringLiteral("—");
  if (done > 0 && total > done) {
    const qint64 etaMs =
        static_cast<qint64>((double(elapsedMs) / double(done)) * double(total - done));
    etaStr = formatDuration(etaMs);
  }
  QString rateStr = QStringLiteral("0 KiB/s");
  if (m_service) {
    // Sliding-window rate: keep samples from the last ~10 seconds
    // and compute (newest.bytes - oldest.bytes) / window-duration.
    // Total-bytes ÷ total-elapsed underreports because lookup-API
    // calls + provider throttling create long no-download stretches
    // that dilute the average.
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 bytes = m_service->totalBytesDownloaded();
    constexpr qint64 kWindowMs = 10000;
    m_rateSamples.append({nowMs, bytes});
    while (m_rateSamples.size() > 1 && nowMs - m_rateSamples.first().first > kWindowMs) {
      m_rateSamples.removeFirst();
    }
    if (m_rateSamples.size() >= 2) {
      const auto &oldest = m_rateSamples.first();
      const auto &newest = m_rateSamples.last();
      const qint64 deltaMs = std::max<qint64>(1, newest.first - oldest.first);
      const qint64 deltaBytes = std::max<qint64>(0, newest.second - oldest.second);
      const double mibPerSec = (deltaBytes / (1024.0 * 1024.0)) / (deltaMs / 1000.0);
      if (mibPerSec >= 1.0) {
        rateStr = tr("%1 MiB/s").arg(mibPerSec, 0, 'f', 1);
      } else {
        rateStr = tr("%1 KiB/s").arg(mibPerSec * 1024.0, 0, 'f', 0);
      }
    }
  }
  m_unifiedTimingLabel->setText(tr("Items %1/%2 · Elapsed %3 · ETA %4 · %5")
                                    .arg(done)
                                    .arg(total)
                                    .arg(formatDuration(elapsedMs), etaStr, rateStr));
  int mediaWritten = 0;
  if (m_service) mediaWritten = m_service->summary().mediaWritten;
  // Render the error count as a clickable link when there are errors,
  // so the user can open the recorded failure messages. Substituted
  // into the %4 slot rather than baked into the tr() string so the
  // translatable text stays markup-free.
  const QString errorsField =
      errors > 0 ? QStringLiteral("<a href=\"kartend:scrape-errors\">%1</a>").arg(errors)
                 : QString::number(errors);
  m_unifiedCountsLabel->setText(tr("Scraped %1 items, %2 media  ·  Skipped %3  ·  Errors %4")
                                    .arg(QString::number(scraped), QString::number(mediaWritten),
                                         QString::number(skipped), errorsField));
}

void ScrapeResultDialog::showScrapeErrorDetails() {
  // Gather failure messages from both the service summary (live /
  // service-driven runs) and the in-dialog accumulator (the fallback
  // orchestration path); dedupe so a message recorded by both isn't
  // listed twice.
  QStringList failures = m_unifiedFailures;
  if (m_service) {
    for (const QString &failure : m_service->summary().firstFailures) {
      if (!failures.contains(failure)) {
        failures.append(failure);
      }
    }
  }

  // The total error count comes straight off the summary; failures is
  // what was actually retained (capped — see kMaxReportedFailures).
  const int totalErrors = m_service ? m_service->summary().errors : failures.size();

  // A resizable dialog with a scrollable list — a misconfigured
  // collection can report hundreds of failures, far past what a
  // QMessageBox can show without clipping.
  QDialog dlg(this);
  dlg.setWindowTitle(tr("Scrape errors"));
  auto *layout = new QVBoxLayout(&dlg);
  if (failures.isEmpty()) {
    // The error counter advanced but no message was captured.
    layout->addWidget(
        new QLabel(tr("No further error detail was recorded for this scrape."), &dlg));
  } else {
    QString header = tr("The scrape reported the following errors:");
    if (failures.size() < totalErrors) {
      // More items errored than messages were retained — say so rather
      // than letting the user assume the list is complete.
      header = tr("Showing %1 of %2 errors:").arg(failures.size()).arg(totalErrors);
    }
    layout->addWidget(new QLabel(header, &dlg));
    auto *list = new QListWidget(&dlg);
    list->addItems(failures);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list->setWordWrap(true);
    layout->addWidget(list);
  }
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
  connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  layout->addWidget(buttons);
  dlg.resize(640, 420);
  dlg.exec();
}

void ScrapeResultDialog::onScrapeClicked() {
  if (m_unifiedPhase != UnifiedPhase::Setup) return;
  if (!m_scraperCtx.collections) return;
  // Walk the tree in display order (including subcollections under
  // their parents) so the user sees a predictable collection
  // sequence in the progress label. Recursive walk because the tree
  // is now hierarchical (top-level + subcollection rows).
  QList<QTreeWidgetItem *> rowsInOrder;
  std::function<void(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *item) {
    if (!item) return;
    rowsInOrder.append(item);
    for (int i = 0; i < item->childCount(); ++i) walk(item->child(i));
  };
  for (int i = 0; i < m_collectionTree->topLevelItemCount(); ++i) {
    walk(m_collectionTree->topLevelItem(i));
  }
  // Translate checked rows into ScraperService::CollectionJob entries.
  // Each job carries the collection uuid + artwork dir resolved here
  // (the service's persistence layer keys jobs by these so resume
  // can survive a config reorder).
  QList<Scraper::ScraperService::CollectionJob> serviceQueue;
  // Legacy/test fallback queue uses the dialog's own CollectionJob
  // shape; populated in parallel so the in-dialog orchestration still
  // runs when no service is wired.
  m_unifiedQueue.clear();
  m_unifiedQueueCursor = 0;
  m_unifiedScrapedTotal = 0;
  m_unifiedSkippedTotal = 0;
  m_unifiedErrorsTotal = 0;
  m_unifiedFailures.clear();
  m_unifiedItemsCompletedAcross = 0;
  m_unifiedCancelled = false;
  m_unifiedStartMs = QDateTime::currentMSecsSinceEpoch();
  // Resolve every checked item to its owning collection, then emit one
  // job per owner. A "shell" parent collection displays the items of
  // its subcollections; checking the parent row pulls those items in,
  // but each item's scraped artwork + metadata must land on the
  // subcollection that owns it — not the parent. ScrapeJobGrouping does
  // the (pure, unit-tested) grouping; owners keep tree display order so
  // the progress label stays predictable.
  QList<int> checkedOrder;
  for (QTreeWidgetItem *row : rowsInOrder) {
    if (row->checkState(0) == Qt::Checked) {
      checkedOrder.append(m_treeItemToCollectionIndex.value(row, -1));
    }
  }
  const auto ownerGroups = ScrapeJobGrouping::byOwningCollection(
      checkedOrder, m_itemSelectionByCollection, m_itemOwnerByCollection,
      static_cast<int>(m_scraperCtx.collections->size()));
  for (const auto &group : ownerGroups) {
    const int owner = group.first;
    const QStringList &items = group.second;
    if (items.isEmpty()) continue;
    const CollectionConfig &cfg = (*m_scraperCtx.collections)[owner];
    const QString expandedMediaDir = PathUtils::validateAndExpandPath(cfg.mediaDirectory, cfg.name);
    const QString uuid = CollectionUtils::computeCollectionUuid(cfg.name, expandedMediaDir);
    const QString artworkDir = PathUtils::validateAndExpandPath(cfg.artworkDirectory, cfg.name);

    Scraper::ScraperService::CollectionJob sJob;
    sJob.collectionIndex = owner;
    sJob.collectionUuid = uuid;
    sJob.collectionName = cfg.name;
    sJob.artworkDir = artworkDir;
    sJob.items = items;
    serviceQueue.append(sJob);

    CollectionJob job;
    job.collectionIndex = owner;
    job.collectionName = cfg.name;
    job.items = items;
    m_unifiedQueue.append(job);
  }
  if (serviceQueue.isEmpty()) {
    QMessageBox::information(this, tr("Scraper"),
                             tr("Pick at least one collection (and one item) before scraping."));
    return;
  }
  // Translate user picks into runner config. Filter keys are
  // lowercased: the runner matches each asset's `type.toLower()`
  // against this set, so a mixed-case key (e.g. "support-2D") would
  // otherwise never match and that media type would silently never
  // download.
  QSet<QString> mediaFilter;
  bool writeMetadata = true;
  for (auto it = m_mediaTypeChecks.constBegin(); it != m_mediaTypeChecks.constEnd(); ++it) {
    if (it.key() == QLatin1String("_metadata")) {
      writeMetadata = it.value()->isChecked();
      continue;
    }
    if (it.value()->isChecked()) mediaFilter.insert(it.key().toLower());
  }
  const auto mode = m_modeAutoRadio->isChecked() ? Scraper::ScraperService::Mode::Auto
                                                 : Scraper::ScraperService::Mode::Interactive;

  if (m_service) {
    // Production path: hand the queue off to the long-lived service
    // and let it drive. The dialog's signal handlers (wired in
    // setScraperService) flip the UI into Live view + advance progress
    // from there. Close button now relevant.
    qCInfo(lcDialogTimings) << "DIALOG onScrapeClicked: service path, queue size="
                            << serviceQueue.size() << "mode="
                            << (mode == Scraper::ScraperService::Mode::Auto ? "auto"
                                                                            : "interactive")
                            << "writeMetadata=" << writeMetadata << "mediaFilter=" << mediaFilter;
    setUnifiedSetupEnabled(false);
    if (m_closeButton) m_closeButton->show();
    m_service->startScrape(serviceQueue, mode, mediaFilter, writeMetadata);
    return;
  }

  // Fallback path: no service wired — run the orchestration in the
  // dialog itself (matches the v1 behaviour for tests / unit harnesses
  // that haven't been migrated to the service yet). This path keeps
  // working but does NOT survive dialog close / app exit.
  setUnifiedSetupEnabled(false);
  m_unifiedProgressBar->setRange(0, totalCheckedItemCount());
  m_unifiedProgressBar->setValue(0);
  updateUnifiedProgressLabel();
  startNextCollectionInQueue();
}

void ScrapeResultDialog::startNextCollectionInQueue() {
  if (m_unifiedCancelled || m_unifiedQueueCursor >= m_unifiedQueue.size()) {
    // All done — fire summary signal, leave the dialog open with the
    // final state visible (caller can dismiss).
    m_unifiedPhase = UnifiedPhase::Done;
    m_unifiedCurrentLabel->setText(tr("Finished."));
    emit unifiedScrapeFinished(m_unifiedScrapedTotal, m_unifiedSkippedTotal, m_unifiedErrorsTotal,
                               m_unifiedFailures);
    accept();
    return;
  }
  const CollectionJob &job = m_unifiedQueue[m_unifiedQueueCursor];
  m_unifiedCurrentLabel->setText(tr("Collection: %1  (%2 of %3)")
                                     .arg(job.collectionName)
                                     .arg(m_unifiedQueueCursor + 1)
                                     .arg(m_unifiedQueue.size()));
  updateUnifiedProgressLabel();
  if (m_modeAutoRadio->isChecked()) {
    m_unifiedPhase = UnifiedPhase::AutoRunning;
    runAutoCollection(job.collectionIndex, job.items);
  } else {
    m_unifiedPhase = UnifiedPhase::InteractiveLookingUp;
    runInteractiveCollection(job.collectionIndex, job.items);
  }
}

void ScrapeResultDialog::runAutoCollection(int collectionIndex, const QStringList &items) {
  if (!m_scraperCtx.providerBuilder || !m_scraperCtx.databaseManager ||
      !m_scraperCtx.generalSettings || !m_scraperCtx.collections) {
    ++m_unifiedErrorsTotal;
    ++m_unifiedQueueCursor;
    startNextCollectionInQueue();
    return;
  }
  std::shared_ptr<MetadataLookupProvider> provider = m_scraperCtx.providerBuilder(collectionIndex);
  if (!provider) {
    m_unifiedFailures.append(
        tr("%1: no provider applies").arg(m_unifiedQueue[m_unifiedQueueCursor].collectionName));
    m_unifiedErrorsTotal += items.size();
    m_unifiedItemsCompletedAcross += items.size();
    ++m_unifiedQueueCursor;
    updateUnifiedProgressLabel();
    startNextCollectionInQueue();
    return;
  }
  const CollectionConfig &cfg = (*m_scraperCtx.collections)[collectionIndex];
  const QString expandedMediaDir = PathUtils::validateAndExpandPath(cfg.mediaDirectory, cfg.name);
  const QString uuid = CollectionUtils::computeCollectionUuid(cfg.name, expandedMediaDir);
  const QString artworkDir = PathUtils::validateAndExpandPath(cfg.artworkDirectory, cfg.name);
  const Scraper::RescrapeMode rescrapeMode =
      static_cast<Scraper::RescrapeMode>(m_scraperCtx.generalSettings->scraperOptions.rescrapeMode);
  const int itemConcurrency = m_scraperCtx.generalSettings->scraperOptions.batchItemConcurrency;
  const int skipRecentDays = m_scraperCtx.generalSettings->scraperOptions.skipRecentScrapeDays;

  // Translate the user's media-type checkboxes into the runner's
  // filter set. The synthetic `_metadata` key gates text-field
  // persistence and is consumed here (stripped from the filter set,
  // routed to setWriteMetadata instead). Empty media filter → runner
  // falls back to legacy "front only" behaviour; non-empty → runner
  // fetches every matching type per item in parallel.
  QSet<QString> mediaFilter;
  bool writeMetadata = true;
  for (auto it = m_mediaTypeChecks.constBegin(); it != m_mediaTypeChecks.constEnd(); ++it) {
    if (it.key() == QLatin1String("_metadata")) {
      writeMetadata = it.value()->isChecked();
      continue;
    }
    if (it.value()->isChecked()) mediaFilter.insert(it.key().toLower());
  }

  auto *runner = new Scraper::BatchScrapeRunner(
      m_scraperCtx.databaseManager, std::move(provider), uuid, items, artworkDir,
      /*fetchPrimaryCover=*/true, rescrapeMode, itemConcurrency, skipRecentDays, this);
  runner->setMediaTypeFilter(mediaFilter);
  runner->setWriteMetadata(writeMetadata);
  m_batchRunner = runner;

  connect(
      runner, &Scraper::BatchScrapeRunner::progress, this,
      [this](int done, int total, const QString &name) {
        // `done` is per-collection; aggregate across queue items
        // for the dialog's outer progress.
        const int totalAcross = totalCheckedItemCount();
        const int prior = m_unifiedItemsCompletedAcross - 0; // unused but documents intent
        Q_UNUSED(prior);
        m_unifiedProgressBar->setRange(0, totalAcross);
        m_unifiedProgressBar->setValue(m_unifiedItemsCompletedAcross + done);
        m_unifiedCurrentLabel->setText(
            tr("Scraping: %1  (item %2 of %3 in this collection)").arg(name).arg(done).arg(total));
        updateUnifiedProgressLabel();
      });
  connect(runner, &Scraper::BatchScrapeRunner::finished, this,
          [this, runner, items](const Scraper::BatchScrapeRunner::Summary &s) {
            m_unifiedScrapedTotal += s.scraped;
            m_unifiedSkippedTotal += s.skipped;
            m_unifiedErrorsTotal += s.errors;
            m_unifiedFailures.append(s.firstFailures);
            m_unifiedItemsCompletedAcross += items.size();
            m_batchRunner = nullptr;
            runner->deleteLater();
            ++m_unifiedQueueCursor;
            startNextCollectionInQueue();
          });
  runner->start();
}

void ScrapeResultDialog::runInteractiveCollection(int collectionIndex, const QStringList &items) {
  if (!m_scraperCtx.providerBuilder) {
    ++m_unifiedErrorsTotal;
    ++m_unifiedQueueCursor;
    startNextCollectionInQueue();
    return;
  }
  m_interactiveProvider = m_scraperCtx.providerBuilder(collectionIndex);
  m_interactiveItems = items;
  m_interactiveCursor = 0;
  m_interactiveCollectionIndex = collectionIndex;
  if (!m_interactiveProvider) {
    m_unifiedFailures.append(
        tr("%1: no provider applies").arg(m_unifiedQueue[m_unifiedQueueCursor].collectionName));
    m_unifiedErrorsTotal += items.size();
    m_unifiedItemsCompletedAcross += items.size();
    ++m_unifiedQueueCursor;
    updateUnifiedProgressLabel();
    startNextCollectionInQueue();
    return;
  }
  interactiveNextItem();
}

void ScrapeResultDialog::interactiveNextItem() {
  if (m_unifiedCancelled || m_interactiveCursor >= m_interactiveItems.size()) {
    // End of items for this collection — advance the outer queue.
    ++m_unifiedQueueCursor;
    startNextCollectionInQueue();
    return;
  }
  const QString filePath = m_interactiveItems[m_interactiveCursor];
  m_unifiedCurrentLabel->setText(tr("Looking up: %1").arg(QFileInfo(filePath).fileName()));
  // Issue the lookup; once candidates land we flip to the single-item
  // page and let the user pick.
  const QString queryText = QFileInfo(filePath).completeBaseName();
  MetadataLookupProvider::LookupContext ctx{queryText, filePath};
  QPointer<ScrapeResultDialog> guard(this);
  m_interactiveProvider->lookup(ctx,
                                [guard](ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> r) {
                                  if (guard.isNull()) return;
                                  guard->interactiveOnLookupResult(r);
                                });
}

void ScrapeResultDialog::interactiveOnLookupResult(
    ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> result) {
  if (m_unifiedCancelled) {
    ++m_unifiedQueueCursor;
    startNextCollectionInQueue();
    return;
  }
  if (result.isError() || result.value().isEmpty()) {
    if (result.isError()) {
      ++m_unifiedErrorsTotal;
      m_unifiedFailures.append(QStringLiteral("%1: %2").arg(
          QFileInfo(m_interactiveItems[m_interactiveCursor]).fileName(), result.error().message));
    } else {
      ++m_unifiedSkippedTotal;
    }
    ++m_unifiedItemsCompletedAcross;
    ++m_interactiveCursor;
    updateUnifiedProgressLabel();
    interactiveNextItem();
    return;
  }
  // Flip to the single-item picker page for this item. Reuses the
  // existing candidate-picker UI by overwriting m_provider /
  // m_candidates and re-running the candidate-list population the
  // ctor would normally do. After the user clicks Apply or Cancel we
  // catch the result via interactiveOnApplied / interactiveOnSkipped.
  m_unifiedPhase = UnifiedPhase::InteractivePicking;
  m_provider = m_interactiveProvider.get();
  m_candidates = result.value();
  m_detailCache.clear();
  m_currentRow = -1;
  m_currentDetail = Scraper::ScrapedItem();
  m_mediaRows.clear();
  m_candidateList->clear();
  for (const auto &c : m_candidates) {
    auto *item = new QListWidgetItem(m_candidateList);
    QString label = c.displayName;
    if (!c.subtitle.isEmpty()) label += QStringLiteral("\n  ") + c.subtitle;
    if (c.matchScore >= 0) label += QStringLiteral("  (%1)").arg(c.matchScore);
    item->setText(label);
  }
  if (m_candidateList && m_candidates.size() <= 1) {
    m_candidateList->hide();
  } else {
    m_candidateList->show();
  }
  m_modeStack->setCurrentWidget(m_singleItemPage);
  m_applyButton->show();
  m_applyButton->setEnabled(false);
  if (m_scrapeButton) m_scrapeButton->hide();
  if (!m_candidates.isEmpty()) m_candidateList->setCurrentRow(0);
}

void ScrapeResultDialog::interactiveOnApplied() {
  // Called when the user's Apply finishes (m_result populated).
  // Honour the `_metadata` checkbox by stripping textual fields when
  // unchecked — applyResult downstream will then preserve whatever's
  // in the DB instead of overwriting with the scrape's text.
  Result delivered = m_result;
  auto *metaCheck = m_mediaTypeChecks.value(QStringLiteral("_metadata"));
  if (metaCheck && !metaCheck->isChecked()) {
    delivered.item.title.clear();
    delivered.item.description.clear();
    delivered.item.genre.clear();
    delivered.item.developer.clear();
    delivered.item.publisher.clear();
    delivered.item.releaseDate.clear();
    delivered.item.contentRating.clear();
    delivered.item.players.clear();
    delivered.item.tagsJson.clear();
    delivered.item.customFields.clear();
    delivered.item.sourceProviderId.clear();
    delivered.item.runtimeSeconds = -1;
  }
  if (m_scraperCtx.applyResult) {
    m_scraperCtx.applyResult(m_interactiveCollectionIndex, m_interactiveItems[m_interactiveCursor],
                             delivered);
  }
  ++m_unifiedScrapedTotal;
  ++m_unifiedItemsCompletedAcross;
  ++m_interactiveCursor;
  updateUnifiedProgressLabel();
  // Switch back to the unified page for the next item's lookup phase.
  m_modeStack->setCurrentWidget(m_unifiedPage);
  if (m_scrapeButton) m_scrapeButton->show();
  m_applyButton->hide();
  m_unifiedPhase = UnifiedPhase::InteractiveLookingUp;
  interactiveNextItem();
}

void ScrapeResultDialog::finishCurrentApply() {
  // Unified interactive picker: don't close the dialog — advance to
  // the next item instead.
  if (m_mode == Mode::Unified && m_unifiedPhase == UnifiedPhase::InteractivePicking) {
    // Service-driven path: persist via the caller's applyResult hook,
    // then tell the service to advance. The service emits
    // `pickerNeeded` for the next item, which the dialog's signal
    // handler flips us into. Stays in the same window throughout.
    if (m_service) {
      Result delivered = m_result;
      auto *metaCheck = m_mediaTypeChecks.value(QStringLiteral("_metadata"));
      if (metaCheck && !metaCheck->isChecked()) {
        delivered.item.title.clear();
        delivered.item.description.clear();
        delivered.item.genre.clear();
        delivered.item.developer.clear();
        delivered.item.publisher.clear();
        delivered.item.releaseDate.clear();
        delivered.item.contentRating.clear();
        delivered.item.players.clear();
        delivered.item.tagsJson.clear();
        delivered.item.customFields.clear();
        delivered.item.sourceProviderId.clear();
        delivered.item.runtimeSeconds = -1;
      }
      if (m_scraperCtx.applyResult && !m_interactiveItems.isEmpty()) {
        // The service is the source of truth for which collection
        // is being processed (the dialog may be reattached to a
        // resumed run where m_interactiveCollectionIndex is stale).
        const int idx = m_service->currentCollectionIndex();
        m_scraperCtx.applyResult(idx, m_interactiveItems.first(), delivered);
      }
      // Stay on the unified page. Apply button hides until the next
      // pickerNeeded signal arrives (which re-enables it with the
      // next item's candidates). The interactive candidate row also
      // hides momentarily — the next pickerNeeded re-shows it.
      if (m_applyButton) m_applyButton->hide();
      if (m_interactiveCandidateRow) m_interactiveCandidateRow->hide();
      m_service->applyPick(delivered.item);
      return;
    }
    interactiveOnApplied();
    return;
  }
  accept();
}

void ScrapeResultDialog::interactiveOnSkipped() {
  ++m_unifiedSkippedTotal;
  ++m_unifiedItemsCompletedAcross;
  ++m_interactiveCursor;
  updateUnifiedProgressLabel();
  m_modeStack->setCurrentWidget(m_unifiedPage);
  if (m_scrapeButton) m_scrapeButton->show();
  m_applyButton->hide();
  m_unifiedPhase = UnifiedPhase::InteractiveLookingUp;
  interactiveNextItem();
}

void ScrapeResultDialog::onCandidateSelected(int row) {
  m_currentRow = row;
  if (row < 0 || row >= m_candidates.size()) {
    m_detailStack->setCurrentIndex(STACK_EMPTY);
    m_applyButton->setEnabled(false);
    return;
  }

  // Cache hit — re-render without an HTTP roundtrip.
  if (m_detailCache.contains(row)) {
    m_currentDetail = m_detailCache.value(row);
    m_detailText->setHtml(renderDetailHtml(m_currentDetail));
    populateMediaCheckboxes(m_currentDetail);
    m_detailStack->setCurrentIndex(STACK_DETAIL);
    m_applyButton->setEnabled(!m_healthBlocksApply);
    return;
  }

  m_detailStack->setCurrentIndex(STACK_LOADING);
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
      m_detailText->setHtml(QStringLiteral("<p style='color:red'>%1</p>")
                                .arg(result.error().message.toHtmlEscaped()));
      m_mediaList->clear();
      m_mediaRows.clear();
      m_detailStack->setCurrentIndex(STACK_DETAIL);
      m_applyButton->setEnabled(false);
      return;
    }
    m_currentDetail = result.value();
    m_detailCache.insert(row, m_currentDetail);
    m_detailText->setHtml(renderDetailHtml(m_currentDetail));
    populateMediaCheckboxes(m_currentDetail);
    m_detailStack->setCurrentIndex(STACK_DETAIL);
    m_applyButton->setEnabled(!m_healthBlocksApply);
  });
}

void ScrapeResultDialog::populateMediaCheckboxes(const Scraper::ScrapedItem &item) {
  m_mediaList->clear();
  m_mediaRows.clear();
  for (const auto &asset : item.media) {
    auto *itemWidget = new QListWidgetItem(m_mediaList);
    QString label = asset.label.isEmpty() ? asset.type : asset.label;
    // Append a scope hint so users understand that group/company-scoped
    // checkboxes affect a *family* of games (theme background applied
    // to every Final Fantasy entry, publisher logo applied to every
    // Capcom title) rather than this single one. Without it, scraping
    // a game with a busy theme can land 30 MB of "background" art the
    // user assumed was per-game.
    if (asset.scope == Scraper::MediaScope::Group) {
      label += QStringLiteral(" — ") + tr("theme/family (shared)");
    } else if (asset.scope == Scraper::MediaScope::Company) {
      label += QStringLiteral(" — ") + tr("publisher (shared)");
    }
    auto *checkbox = new QCheckBox(label, m_mediaList);
    checkbox->setChecked(true); // Pre-check everything; user unchecks what they don't want.
    m_mediaList->setItemWidget(itemWidget, checkbox);
    itemWidget->setSizeHint(checkbox->sizeHint());
    m_mediaRows.append({checkbox, asset});
  }
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
    for (const auto &row : m_mediaRows) {
      if (row.first->isChecked()) {
        selected.append(row.second);
      }
    }
  }

  if (selected.isEmpty() || !m_provider) {
    finishCurrentApply();
    return;
  }

  m_applyButton->setEnabled(false);
  m_candidateList->setEnabled(false);
  m_downloadsTotal = selected.size();
  m_downloadsPending = selected.size();
  m_downloadedBytes = 0;
  m_downloadStartMs = QDateTime::currentMSecsSinceEpoch();
  m_statusLabel->setText(tr("Downloading %1 media items…").arg(m_downloadsTotal));

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
        finishCurrentApply();
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
            guard->finishCurrentApply();
          }
        });
  }
  qCInfo(lcDialogTimings) << "DIALOG dispatch loop returned in" << applyTimer->elapsed() << "ms"
                          << "(should be near zero — all calls are async)";
}

void ScrapeResultDialog::downloadNextSelectedMedia() {
  // Kept for ABI stability (the header still declares it). The parallel
  // dispatch in onApply replaces the serial chain; nothing to do here.
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
  m_statusLabel->setText(tr("Downloaded %1 of %2 (%3) · ETA %4")
                             .arg(completed)
                             .arg(m_downloadsTotal)
                             .arg(rateStr)
                             .arg(etaStr));
}

void ScrapeResultDialog::setBatchRunner(Scraper::BatchScrapeRunner *runner,
                                        const QString &collectionName, int totalItems) {
  m_mode = Mode::Batch;
  m_batchRunner = runner;
  m_batchCollectionName = collectionName;
  m_batchTotalItems = std::max(1, totalItems);
  m_batchStartMs = QDateTime::currentMSecsSinceEpoch();
  // Flip to the batch page; the single-item splitter stays alive but
  // hidden. Apply is meaningless in batch mode — the runner drives
  // every per-item decision — so we hide it. Cancel keeps its slot in
  // the button row and forwards to runner->cancel() via the lambda in
  // buildUi.
  if (m_modeStack) m_modeStack->setCurrentWidget(m_batchPage);
  if (m_applyButton) m_applyButton->hide();

  m_batchHeaderLabel->setText(tr("Batch scraping '%1'…").arg(collectionName));
  m_batchCurrentLabel->setText(tr("Preparing…"));
  m_batchProgressBar->setRange(0, m_batchTotalItems);
  m_batchProgressBar->setValue(0);
  m_batchTimingLabel->setText(tr("Elapsed 0s · ETA —"));
  m_batchCountsLabel->setText(tr("Scraped 0  ·  Skipped 0  ·  Errors 0"));

  if (!runner) return;
  connect(runner, &Scraper::BatchScrapeRunner::progress, this,
          &ScrapeResultDialog::updateBatchProgress);
  connect(runner, &Scraper::BatchScrapeRunner::finished, this,
          [this](const Scraper::BatchScrapeRunner::Summary &s) {
            m_batchSummary = s;
            // Snap the bar to full on completion so the user sees a
            // finished state for a moment before the dialog closes.
            m_batchProgressBar->setValue(m_batchTotalItems);
            m_batchCurrentLabel->setText(tr("Finished."));
            const qint64 elapsedMs =
                std::max<qint64>(1, QDateTime::currentMSecsSinceEpoch() - m_batchStartMs);
            m_batchTimingLabel->setText(tr("Elapsed %1 · ETA —").arg(formatDuration(elapsedMs)));
            m_batchCountsLabel->setText(tr("Scraped %1  ·  Skipped %2  ·  Errors %3")
                                            .arg(s.scraped)
                                            .arg(s.skipped)
                                            .arg(s.errors));
            accept();
          });
}

void ScrapeResultDialog::updateBatchProgress(int done, int total, const QString &currentName) {
  if (total > 0 && total != m_batchProgressBar->maximum()) {
    m_batchProgressBar->setRange(0, total);
    m_batchTotalItems = total;
  }
  m_batchProgressBar->setValue(done);
  m_batchCurrentLabel->setText(currentName.isEmpty() ? tr("Scraping…")
                                                     : tr("Now scraping: %1").arg(currentName));

  const qint64 elapsedMs =
      std::max<qint64>(1, QDateTime::currentMSecsSinceEpoch() - m_batchStartMs);
  QString etaStr = QStringLiteral("—");
  if (done > 0 && total > done) {
    const qint64 etaMs =
        static_cast<qint64>((double(elapsedMs) / double(done)) * double(total - done));
    etaStr = formatDuration(etaMs);
  }
  m_batchTimingLabel->setText(tr("Elapsed %1 · ETA %2").arg(formatDuration(elapsedMs), etaStr));
}
