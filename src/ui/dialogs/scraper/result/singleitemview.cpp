#include "singleitemview.h"

#include "metadatalookupprovider.h"
#include "uiconstants/color.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPointer>
#include <QSplitter>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace {

constexpr int kStackEmptyIndex = 0;
constexpr int kStackLoadingIndex = 1;
constexpr int kStackDetailIndex = 2;

int pageToIndex(SingleItemScrapeView::DetailStackPage page) {
  switch (page) {
  case SingleItemScrapeView::DetailStackPage::Empty:
    return kStackEmptyIndex;
  case SingleItemScrapeView::DetailStackPage::Loading:
    return kStackLoadingIndex;
  case SingleItemScrapeView::DetailStackPage::Detail:
    return kStackDetailIndex;
  }
  return kStackEmptyIndex;
}

QString renderDetailHtml(const Scraper::ScrapedItem &item) {
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

SingleItemScrapeView::SingleItemScrapeView(QWidget *parent) : QWidget(parent) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  auto *splitter = new QSplitter(Qt::Horizontal, this);

  m_candidateList = new QListWidget(splitter);
  m_candidateList->setMinimumWidth(220);
  connect(m_candidateList, &QListWidget::currentRowChanged, this,
          &SingleItemScrapeView::onCandidateSelected);
  splitter->addWidget(m_candidateList);

  auto *rightContainer = new QWidget(splitter);
  auto *rightLayout = new QVBoxLayout(rightContainer);
  rightLayout->setContentsMargins(0, 0, 0, 0);

  m_detailStack = new QStackedWidget(rightContainer);
  auto *emptyLabel = new QLabel(tr("Pick a candidate from the list."), rightContainer);
  emptyLabel->setAlignment(Qt::AlignCenter);
  emptyLabel->setStyleSheet(UIConstants::Color::mutedLabelStyleSheet());
  auto *loadingLabel = new QLabel(tr("Loading details…"), rightContainer);
  loadingLabel->setAlignment(Qt::AlignCenter);
  loadingLabel->setStyleSheet(UIConstants::Color::mutedLabelStyleSheet());
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

  m_detailStack->insertWidget(kStackEmptyIndex, emptyLabel);
  m_detailStack->insertWidget(kStackLoadingIndex, loadingLabel);
  m_detailStack->insertWidget(kStackDetailIndex, detailContainer);
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
  layout->addWidget(splitter, 1);
}

void SingleItemScrapeView::setDetailPage(DetailStackPage page) {
  m_detailStack->setCurrentIndex(pageToIndex(page));
}

void SingleItemScrapeView::setProviderAndCandidates(MetadataLookupProvider *provider,
                                                    QList<Scraper::ScrapeCandidate> candidates) {
  m_provider = provider;
  m_candidates = std::move(candidates);
  m_detailCache.clear();
  m_currentRow = -1;
  m_currentDetail = {};

  m_candidateList->clear();
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
  } else {
    setDetailPage(DetailStackPage::Empty);
    emit detailFailed();
  }
  // ScreenScraper's jeuInfos.php returns exactly one matched game per
  // request — the candidate list ends up with one entry and the user
  // has nothing to choose between. Hide the panel entirely in that
  // case to claim the horizontal room for the description / media
  // checkboxes. Multi-candidate providers (MusicBrainz / OpenLibrary
  // / TMDB) keep the list since picking between candidates is the
  // whole point of their search response.
  m_candidateList->setVisible(m_candidates.size() > 1);
}

void SingleItemScrapeView::onCandidateSelected(int row) {
  m_currentRow = row;
  if (row < 0 || row >= m_candidates.size()) {
    setDetailPage(DetailStackPage::Empty);
    emit detailFailed();
    return;
  }

  // Cache hit — re-render without an HTTP roundtrip.
  if (m_detailCache.contains(row)) {
    m_currentDetail = m_detailCache.value(row);
    m_detailText->setHtml(renderDetailHtml(m_currentDetail));
    populateMediaCheckboxes(m_currentDetail);
    setDetailPage(DetailStackPage::Detail);
    emit detailLoaded(m_currentDetail);
    return;
  }

  setDetailPage(DetailStackPage::Loading);
  emit detailLoading();

  if (!m_provider) {
    return;
  }
  const Scraper::ScrapeCandidate cand = m_candidates[row];
  // QPointer guard: fetchDetail may complete after the view (and the
  // owning dialog) is destroyed — e.g. user cancels mid-fetch and the
  // caller closes exec(). Without the guard the callback would touch
  // a freed `this`.
  QPointer<SingleItemScrapeView> guard(this);
  m_provider->fetchDetail(cand, [guard, row](ErrorUtils::Result<Scraper::ScrapedItem> result) {
    if (guard.isNull()) return;
    // Bail if the user moved on to a different candidate while we
    // were waiting — only update the UI when we're still showing
    // the row this callback is for.
    if (guard->m_currentRow != row) {
      return;
    }
    if (result.isError()) {
      // Kartend-e6oyu: show the enriched summary (status + server-detail
      // snippet) so a one-off lookup failure is diagnosable, not a bare
      // "HTTP request failed".
      guard->m_detailText->setHtml(QStringLiteral("<p style='color:red'>%1</p>")
                                       .arg(result.error().userFacingSummary().toHtmlEscaped()));
      guard->m_mediaList->clear();
      guard->clearMediaRows();
      guard->setDetailPage(DetailStackPage::Detail);
      emit guard->detailFailed();
      return;
    }
    guard->m_currentDetail = result.value();
    guard->m_detailCache.insert(row, guard->m_currentDetail);
    guard->m_detailText->setHtml(renderDetailHtml(guard->m_currentDetail));
    guard->populateMediaCheckboxes(guard->m_currentDetail);
    guard->setDetailPage(DetailStackPage::Detail);
    emit guard->detailLoaded(guard->m_currentDetail);
  });
}

void SingleItemScrapeView::populateMediaCheckboxes(const Scraper::ScrapedItem &item) {
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
    } else if (asset.scope == Scraper::MediaScope::Platform) {
      label += QStringLiteral(" — ") + tr("platform (shared)");
    }
    auto *checkbox = new QCheckBox(label, m_mediaList);
    checkbox->setChecked(true); // Pre-check everything; user unchecks what they don't want.
    m_mediaList->setItemWidget(itemWidget, checkbox);
    itemWidget->setSizeHint(checkbox->sizeHint());
    m_mediaRows.append({checkbox, asset});
  }
}

void SingleItemScrapeView::clearMediaRows() {
  m_mediaList->clear();
  m_mediaRows.clear();
}
