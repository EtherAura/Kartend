// Lifecycle / reset / item-content setters extracted from itemwidget.cpp.
// These remain ItemWidget members; this is a translation-unit split.
#include "itemwidget.h"
#include "propertyutils.h"
#include "uiconstants.h"
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QLabel>
#include <QLayout>
#include <QPixmap>
#include <QPointer>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <Qt>
#include <QTimer>

// Set file path
void ItemWidget::setFilePath(const QString &path) {
  filePath = path;
}

// Reset widget state for reuse from pool
void ItemWidget::resetForReuse() {
  // Clear selection state and stop any running pulse animation
  // This prevents stale selection rectangles when widgets are recycled
  if (isSelectedState) {
    isSelectedState = false;
    if (pulseAnimation && pulseAnimation->state() == QAbstractAnimation::Running) {
      pulseAnimation->stop();
    }
    m_pulseOpacity = UIConstants::Animation::PULSE_OPACITY_LOW;
  }
  if (m_pulseDelayTimer && m_pulseDelayTimer->isActive()) {
    m_pulseDelayTimer->stop();
  }

  m_isSubcollection = false;
  m_subcollectionIndex = -1;
  m_isVirtualFolder = false;
  m_isListMode = false;     // Reset list mode for widget reuse
  m_rowIndex = -1;          // Reset row index for widget reuse
  m_hasArtwork = false;     // Reset artwork flag
  m_collectionName.clear(); // Clear collection name
  m_virtualFolderPath.clear();
  m_hideSubfolderTitle = false;
  filePath.clear();
  itemName.clear();
  storedPixmap = QPixmap(); // Clear stored artwork
  m_placeholderArtworkPixmap = QPixmap();
  // Don't generate placeholder here - onArtworkChanged() will be called after
  // configuration and will generate the placeholder with correct dimensions
  if (triangleIndicator) {
    triangleIndicator->hide();
  }
  // Hide list mode elements
  if (m_collectionLabel) {
    m_collectionLabel->hide();
  }
  if (m_artworkButton) {
    m_artworkButton->hide();
  }
  if (m_folderIconLabel) {
    m_folderIconLabel->hide();
  }
}

// Set as subcollection
void ItemWidget::setAsSubcollection(int index, const QString &name) {
  m_isSubcollection = true;
  m_subcollectionIndex = index;

  // Create folder icon label lazily
  if (!m_folderIconLabel) {
    m_folderIconLabel = new QLabel(this);
    m_folderIconLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_folderIconLabel->setAlignment(Qt::AlignCenter);
  }
  QIcon folderIcon = UIConstants::Icons::fromTheme(UIConstants::Icons::SUBCOLLECTION);
  m_folderIconLabel->setPixmap(folderIcon.pixmap(16, 16));
  m_folderIconLabel->setVisible(true);

  setItemName(name);
  applyDimensions();
}

// Set as virtual folder (subfolder navigation without subcollection)
void ItemWidget::setAsVirtualFolder(const QString &folderPath, const QString &displayName,
                                    bool hideTitle) {
  m_isVirtualFolder = true;
  m_virtualFolderPath = folderPath;
  m_hideSubfolderTitle = hideTitle;

  // Create folder icon label lazily
  if (!m_folderIconLabel) {
    m_folderIconLabel = new QLabel(this);
    m_folderIconLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_folderIconLabel->setAlignment(Qt::AlignCenter);
  }
  // Use documents folder icon to distinguish from subcollection icon
  QIcon folderIcon = UIConstants::Icons::fromTheme(UIConstants::Icons::VIRTUAL_FOLDER);
  m_folderIconLabel->setPixmap(folderIcon.pixmap(16, 16));
  m_folderIconLabel->setVisible(true);

  setItemName(displayName);
  applyDimensions();
}

// Set item name
void ItemWidget::setItemName(const QString &name) {
  const bool nameChanged = (itemName != name);
  itemName = name;
  // Kartend-skd9: the placeholder-art title overlay is baked into the
  // pixmap by onArtworkChanged(). configureBaseWidget calls onArtworkChanged
  // once with an empty itemName (during resetForReuse), and the dimension-
  // driven re-render is gated by setItemDimensions's "same dimensions" early
  // return — so a recycled widget with identical dimensions but a brand-new
  // name keeps the previous placeholder (no title) until something else
  // forces a redraw. Schedule one deferred refresh so the overlay catches
  // up to the current name.
  if (nameChanged && s_showTitleInPlaceholder) {
    QPointer<ItemWidget> ptr = this;
    QTimer::singleShot(0, this, [ptr]() {
      if (ptr) {
        ptr->onArtworkChanged();
      }
    });
  }
  if (nameLabel) {
    // Show title if: regular item with titles visible, OR subcollection with
    // subcollection titles visible, OR virtual folder with subfolder titles
    // visible
    bool shouldShowTitle = false;
    if (m_isVirtualFolder) {
      shouldShowTitle = !m_hideSubfolderTitle;
    } else if (m_isSubcollection) {
      shouldShowTitle = !m_hideSubcollectionTitles;
    } else {
      // Kartend-029m: hideTitles is a grid-mode concern. In list mode the row
      // IS the title -- suppressing it leaves a blank row with no fallback.
      shouldShowTitle = m_isListMode || !m_hideTitles;
    }

    if (!shouldShowTitle) {
      // Keep the label visible but empty to reserve layout space
      nameLabel->setText("");
      nameLabel->setVisible(true);
    } else {
      nameLabel->setText(itemName);
      QFont font = this->font();
      font.setPointSize(m_fontSize);
      nameLabel->setFont(font);
      applyTitleTint();
      nameLabel->setVisible(true);
      nameLabel->raise();
    }
  }
}

// Sets the current pixmap while respecting DeferArtworkUpdate
void ItemWidget::setArtworkPixmap(const QPixmap &pixmap) {
  if (!imageLabel) {
    return;
  }

  bool shouldDefer = property(PropertyKeys::DeferArtworkUpdate).toBool();
  if (shouldDefer) {
    storedPixmap = pixmap;
    return;
  }

  storedPixmap = pixmap;
  QPointer<ItemWidget> ptr = this;
  // Defer artwork display until next event loop iteration - allows
  // the pixmap to be stored before triggering the visual update
  QTimer::singleShot(0, this, [ptr]() {
    if (ptr) {
      ptr->onArtworkChanged();
    }
  });
  if (nameLabel) {
    nameLabel->raise();
  }
}

void ItemWidget::setPlaceholderArtworkPixmap(const QPixmap &pixmap) {
  m_placeholderArtworkPixmap = pixmap;
  if (storedPixmap.isNull()) {
    onArtworkChanged();
  }
}
