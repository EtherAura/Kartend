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
  // Re-render right away when the user is currently viewing the
  // Collection tab (so live edits in settings or fresh scan results land
  // immediately). On Item/File tabs the cache is updated silently and
  // applies the next time the user switches to Collection.
  if (m_activeTab == DetailsPaneTab::Collection) {
    renderCollectionSummary();
  }
}

void DetailsPane::renderCollectionSummary() {
  setArtworkSectionVisible(false);
  setFileInfoRowsVisible(false);
  if (m_galleryView) {
    m_galleryView->hideSection();
  }
  ui->titleLabel->setText(tr("Collection Information"));
  ui->itemNameValue->setText(m_collectionSummary.name);

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

  if (!m_collectionSummary.type.trimmed().isEmpty()) {
    mv->appendDetailRow(tr("Type"), m_collectionSummary.type);
  }
  if (m_collectionSummary.itemCount >= 0) {
    mv->appendDetailRow(tr("Items"), QString::number(m_collectionSummary.itemCount));
  }
  mv->appendDetailRow(tr("Last scanned"),
                      DetailsFormat::formatLastScanned(m_collectionSummary.lastScanned));
  if (!m_collectionSummary.parentName.trimmed().isEmpty()) {
    mv->appendDetailRow(tr("Parent"), m_collectionSummary.parentName);
  }
  mv->appendDetailRow(tr("Media"), m_collectionSummary.mediaDirectory, /*wrap=*/true);
  mv->appendDetailRow(tr("Artwork"), m_collectionSummary.artworkDirectory, /*wrap=*/true);
  mv->appendDetailRow(tr("Video"), m_collectionSummary.videoDirectory, /*wrap=*/true);
  mv->appendDetailRow(tr("Manuals"), m_collectionSummary.manualDirectory, /*wrap=*/true);
  if (!m_collectionSummary.extensions.isEmpty()) {
    mv->appendDetailRow(tr("Extensions"), m_collectionSummary.extensions.join(QStringLiteral(", ")),
                        /*wrap=*/true);
  }
  // Kartend-ecky: persistent warning surface for launcher paths that
  // don't resolve on this host. One row per offending launcher so a
  // multi-launcher collection makes it clear which entry needs fixing.
  for (const QString &issue : m_collectionSummary.launcherPathIssues) {
    mv->appendDetailRow(tr("⚠ Launcher path"), issue, /*wrap=*/true);
  }

  // pull the just-built summary rows under the active sidebar-
  // font override so the no-selection view doesn't render in a different font
  // than the per-item view.
  applySidebarFont(m_activeSidebarFontFamily, m_activeSidebarFontPointSize);

  // Collection summaries are short and static — there is no marquee and
  // on this tab the metadata card is the only content. Left uncapped,
  // m_metadataScroll's Expanding policy stretches the styled backdrop
  // bubble to the full sidebar height; on a sparse summary (a not-yet-
  // scraped subcollection shows just Items + Last scanned) that reads as
  // an oversized empty card. Cap the scroll area to the rows' real,
  // wrap-aware height so the bubble hugs the summary and matches a
  // scraped collection's tighter card. clearDetailsSection lifts the cap
  // again for the Item tab.
  if (m_metadataScroll && m_metadataBackdrop) {
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
  }

  m_detailsContainer->show();
}
