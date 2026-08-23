// Sibling translation unit for DetailsPane: the Collection tab's summary.
// setCollectionSummary caches the manager-pushed snapshot on the widget;
// renderCollectionSummary rebuilds the Details card from it (via
// DetailsPaneMetadataView) whenever the Collection tab renders. Mechanical
// move out of detailspane.cpp — no state changed hands; every member
// referenced here still lives on DetailsPane.

#include "detailsformat.h"
#include "detailspane.h"
#include "detailspanegalleryview.h"
#include "detailspanemetadataview.h"
#include "ui_detailspane.h"

#include <QFrame>
#include <QLayout>
#include <QScrollArea>

void DetailsPane::setCollectionSummary(const CollectionSummary &summary) {
  m_collectionSummary = summary;
  // A new collection context invalidates any selection-scoped subcollection
  // summary — without this, switching collections while a subcollection
  // tile was selected would leave the old child's overview on screen.
  m_selectionSummary = CollectionSummary{};
  // Re-render right away when the user is currently viewing the
  // Collection tab (so live edits in settings or fresh scan results land
  // immediately), or when the Item area is showing the no-item overview
  // this summary feeds (Kartend-um69l). Otherwise the cache is updated
  // silently and applies on the next tab switch / deselect.
  if (m_activeTab == DetailsPaneTab::Collection) {
    renderCollectionSummary();
  } else if (m_activeTab == DetailsPaneTab::Item && !m_hasItemDisplayed) {
    applyTabVisibility();
  }
}

void DetailsPane::renderCollectionSummary() {
  // The Collection tab follows the SELECTION's owning collection when one
  // is pushed (an aggregated view's child item, or a selected subcollection
  // tile); otherwise it describes the viewed collection (Kartend-um69l,
  // user decision 2026-08-23).
  const CollectionSummary &active =
      m_selectionSummary.isValid() ? m_selectionSummary : m_collectionSummary;
  setArtworkSectionVisible(false);
  setFileInfoRowsVisible(false);
  if (m_galleryView) {
    m_galleryView->hideSection();
  }
  ui->titleLabel->setText(tr("Collection Information"));
  ui->itemNameValue->setText(active.name);

  // Kartend-4wxmp: drive the metadata view directly (the pass-through
  // ensureDetailsSection/clearDetailsSection/appendDetailRow forwarders were
  // removed). ensureDetailsSection() is what creates m_detailsContainer, so a
  // non-null container past the guard implies m_metadataView is non-null.
  if (m_metadataView) m_metadataView->ensureDetailsSection();
  if (!m_detailsContainer) {
    return;
  }
  DetailsPaneMetadataView *mv = m_metadataView;
  mv->clearDetailsSection();

  // Scraped collection metadata first (Kartend-445su) — the description
  // reads as the card's lede, mirroring the Item tab's layout.
  mv->appendDetailRow(tr("Description"), active.scrapedDescription, /*wrap=*/true);
  mv->appendDetailRow(tr("Manufacturer"), active.scrapedManufacturer);
  mv->appendDetailRow(tr("Released"), active.scrapedReleaseDate);
  if (!active.type.trimmed().isEmpty()) {
    mv->appendDetailRow(tr("Type"), active.type);
  }
  if (active.itemCount >= 0) {
    mv->appendDetailRow(tr("Items"), QString::number(active.itemCount));
  }
  mv->appendDetailRow(tr("Last scanned"), DetailsFormat::formatLastScanned(active.lastScanned));
  if (!active.parentName.trimmed().isEmpty()) {
    mv->appendDetailRow(tr("Parent"), active.parentName);
  }
  mv->appendDetailRow(tr("Media"), active.mediaDirectory, /*wrap=*/true);
  mv->appendDetailRow(tr("Artwork"), active.artworkDirectory, /*wrap=*/true);
  mv->appendDetailRow(tr("Video"), active.videoDirectory, /*wrap=*/true);
  mv->appendDetailRow(tr("Manuals"), active.manualDirectory, /*wrap=*/true);
  if (!active.extensions.isEmpty()) {
    mv->appendDetailRow(tr("Extensions"), active.extensions.join(QStringLiteral(", ")),
                        /*wrap=*/true);
  }
  // Kartend-ecky: persistent warning surface for launcher paths that
  // don't resolve on this host. One row per offending launcher so a
  // multi-launcher collection makes it clear which entry needs fixing.
  for (const QString &issue : active.launcherPathIssues) {
    mv->appendDetailRow(tr("⚠ Launcher path"), issue, /*wrap=*/true);
  }

  // pull the just-built summary rows under the active sidebar-
  // font override so the no-selection view doesn't render in a different font
  // than the per-item view.
  applySidebarFont(m_activeSidebarFontFamily, m_activeSidebarFontPointSize);

  capMetadataCardToContent();

  m_detailsContainer->show();
}

// Collection summaries are short and static — there is no marquee and
// the metadata card is the only content. Left uncapped, m_metadataScroll's
// Expanding policy stretches the styled backdrop bubble to the full sidebar
// height; on a sparse summary (a not-yet-scraped subcollection shows just
// Items + Last scanned) that reads as an oversized empty card. Cap the
// scroll area to the rows' real, wrap-aware height so the bubble hugs the
// summary and matches a scraped collection's tighter card.
// clearDetailsSection lifts the cap again for the Item tab's per-item path.
void DetailsPane::capMetadataCardToContent() {
  // Measure on the NEXT event-loop tick, not inline: the rows were
  // appended in this very call stack, and their rich-text size hints only
  // resolve once the layout has polished the new labels. An inline
  // measurement reads the PREVIOUS render's geometry — 8px when that was
  // a cleared card, which is exactly the collapsed sliver the guest
  // showed on the Collection tab after an item render (Kartend-um69l).
  QTimer::singleShot(0, this, [this]() {
    if (!m_metadataScroll || !m_metadataBackdrop) {
      return;
    }
    // Only cap while a summary is still what's on display; the Item tab's
    // per-item path may have taken over during the tick (its
    // clearDetailsSection lifts the cap, and re-capping would shrink the
    // full-height metadata card).
    const bool summaryShowing = (m_activeTab == DetailsPaneTab::Collection) ||
                                (m_activeTab == DetailsPaneTab::Item && !m_hasItemDisplayed);
    if (!summaryShowing) {
      return;
    }
    if (QLayout *inner = m_metadataBackdrop->layout()) {
      inner->activate();
    }
    const int width = m_metadataScroll->viewport() ? m_metadataScroll->viewport()->width() : 0;
    if (width > 0) {
      // heightForWidth resolves the wrapped path rows; sizeHint suffices
      // when no row wraps. Width unknown (pane not yet shown) → leave the
      // card uncapped rather than risk clipping a row.
      const int contentHeight = m_metadataBackdrop->hasHeightForWidth()
                                    ? m_metadataBackdrop->heightForWidth(width)
                                    : m_metadataBackdrop->sizeHint().height();
      m_metadataScroll->setMaximumHeight(contentHeight);
    }
  });
}

void DetailsPane::setSelectionCollectionSummary(const CollectionSummary &summary) {
  m_selectionSummary = summary;
  // Re-render whichever surface is showing selection-scoped collection
  // info right now: the Item area's overview (subcollection tile, no item)
  // or the Collection tab, which follows the selection's owning collection
  // (user decision 2026-08-23).
  if (m_activeTab == DetailsPaneTab::Item && !m_hasItemDisplayed) {
    applyTabVisibility();
  } else if (m_activeTab == DetailsPaneTab::Collection) {
    renderCollectionSummary();
  }
}

void DetailsPane::clearSelectionCollectionSummary() {
  if (!m_selectionSummary.isValid()) {
    return;
  }
  m_selectionSummary = CollectionSummary{};
  if (m_activeTab == DetailsPaneTab::Item && !m_hasItemDisplayed) {
    applyTabVisibility();
  } else if (m_activeTab == DetailsPaneTab::Collection) {
    renderCollectionSummary();
  }
}

// The Item area's no-item state (Kartend-um69l): the current collection —
// or the selected subcollection — rendered in the same skeleton as an item:
// artwork box, name row, details card. Deliberately shows NO filesystem
// paths; those belong to the Collection tab, and published captures must
// not carry them.
void DetailsPane::renderItemAreaCollectionOverview() {
  const CollectionSummary &s =
      m_selectionSummary.isValid() ? m_selectionSummary : m_collectionSummary;

  if (!s.isValid()) {
    // Nothing pushed yet (startup, or an out-of-range collection) — keep
    // the legacy placeholder rather than an empty-string name row.
    ui->itemNameValue->setText(tr("No item selected"));
    setArtworkSectionVisible(false);
    if (m_detailsContainer) m_detailsContainer->hide();
    return;
  }

  ui->titleLabel->setText(m_selectionSummary.isValid() ? tr("Subcollection") : tr("Collection"));
  ui->itemNameValue->setText(s.name);

  // Representative artwork in the item preview slot. Missing art hides the
  // box — a dimmed empty tile under a "Collection" heading reads as broken
  // artwork rather than as a deliberate placeholder.
  if (!s.artworkPath.isEmpty()) {
    m_artworkSource = QPixmap(s.artworkPath);
    m_primaryArtworkPath = s.artworkPath;
  } else {
    m_artworkSource = QPixmap();
    m_primaryArtworkPath.clear();
  }
  applyPreviewSize();
  setArtworkSectionVisible(!m_artworkSource.isNull());

  if (m_metadataView) m_metadataView->ensureDetailsSection();
  if (!m_detailsContainer) {
    return;
  }
  DetailsPaneMetadataView *mv = m_metadataView;
  mv->clearDetailsSection();
  // Scraped metadata leads (Kartend-445su): a scraped collection reads
  // like a scraped item — description, then facts.
  mv->appendDetailRow(tr("Description"), s.scrapedDescription, /*wrap=*/true);
  mv->appendDetailRow(tr("Manufacturer"), s.scrapedManufacturer);
  mv->appendDetailRow(tr("Released"), s.scrapedReleaseDate);
  if (!s.type.trimmed().isEmpty()) {
    mv->appendDetailRow(tr("Type"), s.type);
  }
  if (s.itemCount >= 0) {
    mv->appendDetailRow(tr("Items"), QString::number(s.itemCount));
  }
  mv->appendDetailRow(tr("Last scanned"), DetailsFormat::formatLastScanned(s.lastScanned));
  if (!s.parentName.trimmed().isEmpty()) {
    mv->appendDetailRow(tr("Parent"), s.parentName);
  }

  applySidebarFont(m_activeSidebarFontFamily, m_activeSidebarFontPointSize);
  capMetadataCardToContent();
  m_detailsContainer->show();
}
