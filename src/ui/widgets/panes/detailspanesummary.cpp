// Sibling translation unit for DetailsPane: the Collection tab's summary.
// setCollectionSummary caches the manager-pushed snapshot on the widget;
// renderCollectionSummary rebuilds the Details card from it (via
// DetailsPaneMetadataView) whenever the Collection tab renders. Mechanical
// move out of detailspane.cpp — no state changed hands; every member
// referenced here still lives on DetailsPane.

#include "detailsformat.h"
#include <QLocale>

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
  // tile was selected would leave the old child's overview on screen. The
  // owner summary is selection-scoped too (Kartend-6i10t).
  m_selectionSummary = CollectionSummary{};
  m_ownerSummary = CollectionSummary{};
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
  // The Collection tab describes the PARENT of whatever is selected
  // (Kartend-6i10t, user decision 2026-08-23): a displayed item's owning
  // collection when the manager pushed one, else the viewed collection —
  // which is also a selected subcollection TILE's parent in context, so a
  // tile deliberately does NOT surface here (its own info lives in the
  // Item area's overview).
  const CollectionSummary &active = m_ownerSummary.isValid() ? m_ownerSummary : m_collectionSummary;
  // Kartend-5b5r1: the Collection tab shows the collection's artwork and
  // scraped-image gallery too — same item-styled look as the overview.
  if (!active.artworkPath.isEmpty()) {
    m_artworkSource = QPixmap(active.artworkPath);
    m_primaryArtworkPath = active.artworkPath;
  } else {
    m_artworkSource = QPixmap();
    m_primaryArtworkPath.clear();
  }
  applyPreviewSize();
  setArtworkSectionVisible(!m_artworkSource.isNull());
  setFileInfoRowsVisible(false);
  setArtworkGallery(active.galleryEntries);
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
  mv->appendDetailRow(tr("Developer"), active.scrapedDeveloper);
  mv->appendDetailRow(tr("Publisher"), active.scrapedPublisher);
  mv->appendDetailRow(tr("Genre"), active.scrapedGenre);
  mv->appendDetailRow(tr("Country"), active.scrapedCountry);
  mv->appendDetailRow(tr("Released"), active.scrapedReleaseDate);
  mv->appendDetailRow(tr("Website"), active.scrapedWebsite, /*wrap=*/true,
                      /*placeholder=*/false, /*linkify=*/true);
  for (const auto &spec : active.scrapedSpecs) {
    mv->appendDetailRow(spec.first, spec.second);
  }
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

  m_detailsContainer->show();
}

void DetailsPane::setSelectionCollectionSummary(const CollectionSummary &summary) {
  m_selectionSummary = summary;
  // Re-render whichever no-item surface shows the selected subcollection
  // right now: the Item area's overview or the File tab's on-disk story.
  // The Collection tab is deliberately NOT driven from here (Kartend-6i10t)
  // — it describes the selection's PARENT via the owner channel.
  if ((m_activeTab == DetailsPaneTab::Item || m_activeTab == DetailsPaneTab::File) &&
      !m_hasItemDisplayed) {
    applyTabVisibility();
  }
}

void DetailsPane::clearSelectionCollectionSummary() {
  if (!m_selectionSummary.isValid()) {
    return;
  }
  m_selectionSummary = CollectionSummary{};
  if ((m_activeTab == DetailsPaneTab::Item || m_activeTab == DetailsPaneTab::File) &&
      !m_hasItemDisplayed) {
    applyTabVisibility();
  }
}

void DetailsPane::setOwnerCollectionSummary(const CollectionSummary &summary) {
  m_ownerSummary = summary;
  if (m_activeTab == DetailsPaneTab::Collection) {
    renderCollectionSummary();
  }
}

void DetailsPane::clearOwnerCollectionSummary() {
  if (!m_ownerSummary.isValid()) {
    return;
  }
  m_ownerSummary = CollectionSummary{};
  if (m_activeTab == DetailsPaneTab::Collection) {
    renderCollectionSummary();
  }
}

// The Item area's no-item state (Kartend-um69l): the current collection —
// or the selected subcollection — rendered in the same skeleton as an item:
// artwork box, name row, details card. Deliberately shows NO filesystem
// paths; those belong to the Collection tab, and published captures must
// not carry them.
// Kartend-6i10t: the File tab's collection story — where the collection
// lives on disk and how much of it there is. Same item-styled card as the
// other collection surfaces; no scraped text (that is the other tabs' job).
void DetailsPane::renderCollectionFileOverview() {
  const CollectionSummary &s =
      m_selectionSummary.isValid() ? m_selectionSummary : m_collectionSummary;
  setArtworkSectionVisible(false);
  setFileInfoRowsVisible(false);
  if (m_galleryView) {
    m_galleryView->hideSection();
  }
  ui->titleLabel->setText(tr("File Information"));
  ui->itemNameValue->setText(s.name);

  if (m_metadataView) m_metadataView->ensureDetailsSection();
  if (!m_detailsContainer) {
    return;
  }
  DetailsPaneMetadataView *mv = m_metadataView;
  mv->clearDetailsSection();
  mv->appendDetailRow(tr("Media"), s.mediaDirectory, /*wrap=*/true);
  mv->appendDetailRow(tr("Artwork"), s.artworkDirectory, /*wrap=*/true);
  mv->appendDetailRow(tr("Video"), s.videoDirectory, /*wrap=*/true);
  mv->appendDetailRow(tr("Manuals"), s.manualDirectory, /*wrap=*/true);
  if (s.itemCount >= 0) {
    mv->appendDetailRow(tr("Items"), QString::number(s.itemCount));
  }
  // Only with a real sum: 0 with thousands of items just means the scan
  // has not stored sizes yet (file_size defaults to 0) — "0 bytes" there
  // reads as breakage, not as fact (Kartend-6i10t user report).
  if (s.totalSizeBytes > 0) {
    mv->appendDetailRow(tr("Size on disk"), QLocale().formattedDataSize(s.totalSizeBytes));
  }
  mv->appendDetailRow(tr("Last scanned"), DetailsFormat::formatLastScanned(s.lastScanned));
  if (!s.extensions.isEmpty()) {
    mv->appendDetailRow(tr("Extensions"), s.extensions.join(QStringLiteral(", ")),
                        /*wrap=*/true);
  }

  applySidebarFont(m_activeSidebarFontFamily, m_activeSidebarFontPointSize);
  m_detailsContainer->show();
}

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

  // Kartend-5b5r1: every scraped system image rides the same gallery strip
  // items get; clicking a thumb swaps the big preview above.
  setArtworkGallery(s.galleryEntries);

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
  mv->appendDetailRow(tr("Developer"), s.scrapedDeveloper);
  mv->appendDetailRow(tr("Publisher"), s.scrapedPublisher);
  mv->appendDetailRow(tr("Genre"), s.scrapedGenre);
  mv->appendDetailRow(tr("Country"), s.scrapedCountry);
  mv->appendDetailRow(tr("Released"), s.scrapedReleaseDate);
  mv->appendDetailRow(tr("Website"), s.scrapedWebsite, /*wrap=*/true,
                      /*placeholder=*/false, /*linkify=*/true);
  for (const auto &spec : s.scrapedSpecs) {
    mv->appendDetailRow(spec.first, spec.second);
  }
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
  m_detailsContainer->show();
}
