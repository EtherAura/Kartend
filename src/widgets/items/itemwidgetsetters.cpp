// Simple property setters for ItemWidget.
// Extracted from itemwidget.cpp during LOC-reduction refactor.
// These remain ItemWidget members; pure translation-unit split.
#include "itemwidget.h"
#include "uiconstants.h"

#include <QLabel>
#include <QLoggingCategory>
#include <QPushButton>
#include <QString>

Q_DECLARE_LOGGING_CATEGORY(lcItemWidget)

void ItemWidget::setPulseOpacity(qreal opacity) {
  m_pulseOpacity = opacity;
  scheduleSelectionBorderUpdate();
}

// Set font size
void ItemWidget::setFontSize(int fontSize) {
  if (m_fontSize == fontSize) {
    return;
  }
  m_fontSize = fontSize;
  applyDimensions();
}

// Set hide titles
void ItemWidget::setHideTitles(bool hide) {
  if (m_hideTitles == hide) {
    return;
  }
  m_hideTitles = hide;
  applyDimensions();
}

void ItemWidget::setHideSubcollectionTitles(bool hide) {
  if (m_hideSubcollectionTitles == hide) {
    return;
  }
  m_hideSubcollectionTitles = hide;
  applyDimensions();
}

// Set corner radius for artwork clipping
void ItemWidget::setCornerRadius(int radius) {
  if (m_cornerRadius == radius) {
    return;
  }
  m_cornerRadius = radius;
  onArtworkChanged();
}

// Set list mode - hides artwork and shows text-only row layout
void ItemWidget::setListMode(bool listMode) {
  if (m_isListMode == listMode) {
    return;
  }
  m_isListMode = listMode;

  if (imageLabel) {
    imageLabel->setVisible(!listMode);
  }

  if (nameLabel && listMode) {
    // In list mode, left-align and expand text
    nameLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  } else if (nameLabel) {
    // Grid mode: center text below artwork
    nameLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
  }

  applyDimensions();
}

// Set the parent collection name for display in list mode
void ItemWidget::setCollectionName(const QString &name) {
  m_collectionName = name;

  qCDebug(lcItemWidget) << "setCollectionName:" << name << "isListMode=" << m_isListMode;

  // Create collection label lazily if needed
  if (m_isListMode && !m_collectionName.isEmpty()) {
    if (!m_collectionLabel) {
      m_collectionLabel = new QLabel(this);
      m_collectionLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
      m_collectionLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }
    m_collectionLabel->setText(m_collectionName);
    m_collectionLabel->show();
    applyDimensions(); // Position the newly created/shown label
  } else if (m_collectionLabel) {
    m_collectionLabel->hide();
  }
}

// Set whether artwork exists for this item (controls artwork button visibility)
void ItemWidget::setHasArtwork(bool hasArtwork) {
  m_hasArtwork = hasArtwork;

  qCDebug(lcItemWidget) << "setHasArtwork:" << hasArtwork << "isListMode=" << m_isListMode;

  // Create artwork button lazily if needed
  if (m_isListMode && m_hasArtwork) {
    if (!m_artworkButton) {
      m_artworkButton = new QPushButton(this);
      m_artworkButton->setIcon(UIConstants::Icons::fromTheme(UIConstants::Icons::IMAGE));
      m_artworkButton->setFlat(true);
      m_artworkButton->setCursor(Qt::PointingHandCursor);
      // Click emits signal for artwork preview
      connect(m_artworkButton, &QPushButton::clicked, this, [this]() {
        if (!filePath.isEmpty()) {
          emit artworkPreviewRequested(filePath, m_artworkDirectory);
        }
      });
    }
    m_artworkButton->show();
    applyDimensions(); // Position the newly created/shown button
  } else if (m_artworkButton) {
    m_artworkButton->hide();
  }
}

// Set collection column width for list mode (resizable)
void ItemWidget::setCollectionColumnWidth(int width) {
  if (m_collectionColumnWidth != width) {
    m_collectionColumnWidth = width;
    if (m_isListMode) {
      applyDimensions(); // Re-layout with new column width
    }
  }
}

// Set artwork column width for list mode (resizable)
void ItemWidget::setArtworkColumnWidth(int width) {
  if (m_artworkColumnWidth != width) {
    m_artworkColumnWidth = width;
    if (m_isListMode) {
      applyDimensions(); // Re-layout with new column width
    }
  }
}
