// Lifecycle / reset / item-content setters extracted from itemwidget.cpp.
// These remain ItemWidget members; this is a translation-unit split.
#include "itemwidget.h"
#include "propertyutils.h"
#include "uiconstants/animation.h"
#include "uiconstants/icons.h"
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
  if (m_pulseDelayTimer.isActive()) {
    m_pulseDelayTimer.stop();
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
  storedPixmap = QPixmap();   // Clear stored artwork
  m_storedIsComposed = false; // The cleared pixmap is no worker-composed card
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
  // Screen readers announce the tile by its title regardless of whether the
  // visible nameLabel is hidden (grid mode without titles still benefits).
  setAccessibleName(name);
  // the placeholder-art title overlay is baked into the
  // pixmap by onArtworkChanged(). configureBaseWidget calls onArtworkChanged
  // once with an empty itemName (during resetForReuse), and the dimension-
  // driven re-render is gated by setItemDimensions's "same dimensions" early
  // return — so a recycled widget with identical dimensions but a brand-new
  // name keeps the previous placeholder (no title) until something else
  // forces a redraw. Schedule one deferred refresh so the overlay catches
  // up to the current name.
  if (nameChanged && s_showTitleInPlaceholder) {
    QPointer<ItemWidget> ptr = this;
    // Defer the placeholder re-render one event-loop tick: setItemDimensions's
    // "same dimensions" early return skipped onArtworkChanged synchronously,
    // and triggering it from inside configureBaseWidget recurses through the
    // widget-pool reuse path. singleShot(0) breaks the reentrancy.
    QTimer::singleShot(0, this, [ptr]() {
      if (ptr) {
        ptr->onArtworkChanged();
      }
    });
  }
  if (nameLabel) {
    if (!shouldPaintTitle()) {
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

  // Kartend-63wg: raw artwork — onArtworkChanged still scales + composites it.
  m_storedIsComposed = false;

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

// Kartend-63wg: set a worker-composed final card. Mirrors setArtworkPixmap's
// defer handling, but flags the stored pixmap as already-composed so
// onArtworkChanged sets it straight onto the label (no scale/composite/mask).
void ItemWidget::setComposedArtwork(const QPixmap &card) {
  if (!imageLabel) {
    return;
  }

  m_storedIsComposed = true;

  bool shouldDefer = property(PropertyKeys::DeferArtworkUpdate).toBool();
  if (shouldDefer) {
    storedPixmap = card;
    return;
  }

  storedPixmap = card;
  QPointer<ItemWidget> ptr = this;
  // Defer to the next event-loop iteration so the card is stored before the
  // visual update runs — same store-then-apply ordering as setArtworkPixmap.
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
