#include "singleitemview.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
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

} // namespace

SingleItemScrapeView::SingleItemScrapeView(QWidget *parent) : QWidget(parent) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  auto *splitter = new QSplitter(Qt::Horizontal, this);

  m_candidateList = new QListWidget(splitter);
  m_candidateList->setMinimumWidth(220);
  connect(m_candidateList, &QListWidget::currentRowChanged, this,
          &SingleItemScrapeView::candidateRowChanged);
  splitter->addWidget(m_candidateList);

  auto *rightContainer = new QWidget(splitter);
  auto *rightLayout = new QVBoxLayout(rightContainer);
  rightLayout->setContentsMargins(0, 0, 0, 0);

  m_detailStack = new QStackedWidget(rightContainer);
  auto *emptyLabel = new QLabel(tr("Pick a candidate from the list."), rightContainer);
  emptyLabel->setAlignment(Qt::AlignCenter);
  emptyLabel->setStyleSheet("color: palette(mid);");
  auto *loadingLabel = new QLabel(tr("Loading details…"), rightContainer);
  loadingLabel->setAlignment(Qt::AlignCenter);
  loadingLabel->setStyleSheet("color: palette(mid);");
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
