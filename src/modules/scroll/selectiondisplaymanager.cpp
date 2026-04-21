// SelectionDisplayManager: owns selection overlay, state tracker, list header,
// and artwork preview overlay. Extracted from ScrollManager (Kartend-3u5).
#include "selectiondisplaymanager.h"

#include "artworkpreviewoverlay.h"
#include "itemwidget.h"
#include "itemwidgetfactory.h"
#include "listheaderwidget.h"
#include "selectionoverlaymanager.h"
#include "selectionstatetracker.h"
#include "settingsutils.h"
#include "uiconstants.h"

#include <QHash>
#include <QLoggingCategory>
#include <QScrollArea>
#include <QWidget>

namespace {
Q_LOGGING_CATEGORY(lcSelectionDisplay, "kartend.selectiondisplay")
}

SelectionDisplayManager::SelectionDisplayManager(QObject *parent)
    : QObject(parent),
      m_overlay(std::make_unique<SelectionOverlayManager>(this)),
      m_stateTracker(std::make_unique<SelectionStateTracker>()) {}

SelectionDisplayManager::~SelectionDisplayManager() { destroyListHeader(); }

void SelectionDisplayManager::applyGeneralSettings(
    const GeneralSettings *settings) {
  if (!settings) {
    return;
  }
  if (settings->listCollectionColumnWidth > 0) {
    m_collectionColumnWidth = settings->listCollectionColumnWidth;
  }
  if (settings->listArtworkColumnWidth > 0) {
    m_artworkColumnWidth = settings->listArtworkColumnWidth;
  }
}

void SelectionDisplayManager::destroyListHeader() {
  if (m_listHeader) {
    m_listHeader->deleteLater();
    m_listHeader = nullptr;
  }
}

// ─────────────────────────────────────────────────────────────────────────
// List header
// ─────────────────────────────────────────────────────────────────────────

// Header is parented to viewport (not virtual container) so it stays fixed
// while scrolling.
void SelectionDisplayManager::updateListHeader() {
  if (!m_context || !m_metrics) {
    return;
  }
  const bool isListMode = (m_context->config.viewType == ViewType::List);

  if (isListMode && m_mediaScrollArea && m_mediaScrollArea->viewport()) {
    QWidget *viewport = m_mediaScrollArea->viewport();
    if (!m_listHeader) {
      // Parent to viewport so header stays fixed while content scrolls
      m_listHeader = new ListHeaderWidget(viewport);
      // Connect column click signal to sort handler
      connect(m_listHeader, &ListHeaderWidget::columnClicked, this,
              &SelectionDisplayManager::onListColumnClicked);
      // Connect column width change signals for drag-to-resize
      connect(m_listHeader, &ListHeaderWidget::columnWidthChanged, this,
              &SelectionDisplayManager::onListColumnWidthChanged);
      connect(m_listHeader, &ListHeaderWidget::artworkColumnWidthChanged, this,
              &SelectionDisplayManager::onListArtworkColumnWidthChanged);
    }
    // Always sync column widths - settings may have been loaded after header
    // creation
    m_listHeader->setCollectionColumnWidth(m_collectionColumnWidth);
    m_listHeader->setArtworkColumnWidth(m_artworkColumnWidth);

    // Position header at top of viewport - calculate x position based on
    // container position
    int headerWidth = m_metrics->itemWidth + (m_metrics->margins * 2);
    int containerX = m_virtualContainer ? m_virtualContainer->x() : 0;
    m_listHeader->setGeometry(containerX, 0, headerWidth,
                              UIConstants::ListView::HEADER_HEIGHT);
    m_listHeader->show();
    m_listHeader->raise(); // Keep above scrolling content
    m_listHeader->setAttribute(Qt::WA_TransparentForMouseEvents,
                               false); // Ensure header receives clicks
    qCDebug(lcSelectionDisplay)
        << "updateListHeader: header geometry=" << m_listHeader->geometry()
        << "raised, accepts mouse events";
  } else if (m_listHeader) {
    // Hide header in grid mode
    m_listHeader->hide();
  }
}

void SelectionDisplayManager::onListColumnClicked(ListSortColumn column) {
  if (!m_listHeader) {
    return;
  }

  // Toggle direction if clicking the same column
  static ListSortColumn lastColumn = ListSortColumn::Name;
  static bool ascending = true;

  if (column == lastColumn) {
    ascending = !ascending;
  } else {
    lastColumn = column;
    ascending = true;
  }

  m_listHeader->setSortColumn(column, ascending);

  // Determine sort mode based on column and direction and emit signal
  if (column == ListSortColumn::Name) {
    SortMode newSortMode =
        ascending ? SortMode::NameAscending : SortMode::NameDescending;
    qCDebug(lcSelectionDisplay)
        << "onListColumnClicked: Name column, ascending=" << ascending
        << "sortMode=" << static_cast<int>(newSortMode);
    emit sortModeChangeRequested(newSortMode);
  } else if (column == ListSortColumn::Collection) {
    SortMode newSortMode = ascending ? SortMode::CollectionAscending
                                     : SortMode::CollectionDescending;
    qCDebug(lcSelectionDisplay)
        << "onListColumnClicked: Collection column, ascending=" << ascending
        << "sortMode=" << static_cast<int>(newSortMode);
    emit sortModeChangeRequested(newSortMode);
  } else if (column == ListSortColumn::Artwork) {
    SortMode newSortMode =
        ascending ? SortMode::ArtworkFirst : SortMode::ArtworkLast;
    qCDebug(lcSelectionDisplay)
        << "onListColumnClicked: Artwork column, ascending=" << ascending
        << "sortMode=" << static_cast<int>(newSortMode);
    emit sortModeChangeRequested(newSortMode);
  }
}

void SelectionDisplayManager::onListColumnWidthChanged(int collectionWidth) {
  m_collectionColumnWidth = collectionWidth;

  // Update factory so new widgets get the correct width
  if (m_widgetFactory) {
    m_widgetFactory->setCollectionColumnWidth(collectionWidth);
  }

  // Update all visible widgets with the new column width
  if (m_activeWidgets) {
    for (auto it = m_activeWidgets->begin(); it != m_activeWidgets->end();
         ++it) {
      ItemWidget *widget = it.value();
      if (widget && widget->isListMode()) {
        widget->setCollectionColumnWidth(collectionWidth);
      }
    }
  }

  // Emit signal for persistence
  emit listColumnWidthChanged(collectionWidth);
}

void SelectionDisplayManager::onListArtworkColumnWidthChanged(int artworkWidth) {
  m_artworkColumnWidth = artworkWidth;

  // Update factory so new widgets get the correct width
  if (m_widgetFactory) {
    m_widgetFactory->setArtworkColumnWidth(artworkWidth);
  }

  // Update all visible widgets with the new column width
  if (m_activeWidgets) {
    for (auto it = m_activeWidgets->begin(); it != m_activeWidgets->end();
         ++it) {
      ItemWidget *widget = it.value();
      if (widget && widget->isListMode()) {
        widget->setArtworkColumnWidth(artworkWidth);
      }
    }
  }

  // Emit signal for persistence
  emit listArtworkColumnWidthChanged(artworkWidth);
}

// ─────────────────────────────────────────────────────────────────────────
// Artwork preview overlay
// ─────────────────────────────────────────────────────────────────────────

bool SelectionDisplayManager::isArtworkPreviewVisible() const {
  return m_artworkPreviewOverlay && m_artworkPreviewOverlay->isVisible();
}

bool SelectionDisplayManager::hideArtworkPreview() {
  if (isArtworkPreviewVisible()) {
    m_artworkPreviewOverlay->hideOverlay();
    return true;
  }
  return false;
}

void SelectionDisplayManager::showArtworkPreview(const QString &filePath,
                                                 const QString &artworkDir) {
  if (!m_mediaScrollArea) {
    return;
  }

  // Create overlay lazily on first use
  if (!m_artworkPreviewOverlay) {
    m_artworkPreviewOverlay =
        std::make_unique<ArtworkPreviewOverlay>(m_mediaScrollArea);
  }

  // Show artwork preview using the item's collection artwork directory
  m_artworkPreviewOverlay->showArtworkForFile(filePath, artworkDir);
}
