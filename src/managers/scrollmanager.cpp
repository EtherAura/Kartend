// Manages virtual scrolling, widget pooling, and grid layout for large item collections.
#include "scrollmanager.h"
#include "artworkmanager.h"
#include "databasemanager.h"
#include "gridutils.h"
#include "itemwidget.h"
#include "propertyutils.h"
#include "timerutils.h"
#include "uiconstants.h"
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QTextStream>
#include <QTimer>
#include <QWidget>
#include <algorithm>

#ifdef KARTEND_DEBUG_LOGGING
#include <QLoggingCategory>
Q_LOGGING_CATEGORY(lcScrollManager, "kartend.scrollmanager")
#define debugLog(msg) qCDebug(lcScrollManager) << msg
#else
#define debugLog(msg) do {} while(0)
#endif

// Initializes timers for throttle, arrow-key updates, and a short idle window
// to treat any scrollbar interaction as user-driven scrolling
ScrollManager::ScrollManager(QObject *parent) : QObject(parent) {
  // Throttle timer - only fires once per interval, ignores subsequent triggers
  m_scrollTimer = new QTimer(this);
  m_scrollTimer->setSingleShot(true);
  m_scrollTimer->setInterval(UIConstants::SCROLL_THROTTLE_DELAY);
  connect(m_scrollTimer, &QTimer::timeout, this,
          &ScrollManager::onThrottledUpdate);

  m_arrowKeyViewUpdateTimer = new QTimer(this);
  m_arrowKeyViewUpdateTimer->setSingleShot(true);
  m_arrowKeyViewUpdateTimer->setInterval(
      UIConstants::ARROW_KEY_VIEW_UPDATE_INTERVAL_MS);
  connect(m_arrowKeyViewUpdateTimer, &QTimer::timeout, this,
          &ScrollManager::onArrowKeyViewUpdate);

  // Debounce timer - restarts on each trigger, fires after inactivity
  m_userScrollIdleTimer = new TimerUtils::DebouncedTimer(UIConstants::USER_SCROLL_IDLE_TIMER_MS, this);
  connect(m_userScrollIdleTimer, &TimerUtils::DebouncedTimer::triggered, this,
          [this]() { m_userScrollbarActive = false; });
}

// Destructor disconnects scroll events, clears timers, deletes widgets and
// container
ScrollManager::~ScrollManager() {
  m_destroying = true;
  disconnectScrollEvents();

  TimerUtils::stopAndDisconnectTimers(
      {m_scrollTimer, m_arrowKeyViewUpdateTimer});

  for (auto it = m_activeWidgets.begin(); it != m_activeWidgets.end(); ++it) {
    if (MediaItemWidget *widget = it.value()) {
      widget->hide();
      widget->deleteLater();
    }
  }
  m_activeWidgets.clear();
  clearWidgetPool();
  cleanupVirtualContainer();
}

// Widget pool management for recycling MediaItemWidgets
MediaItemWidget *ScrollManager::acquireWidget() {
  if (!m_widgetPool.isEmpty()) {
    MediaItemWidget *widget = m_widgetPool.takeLast();
    ++m_poolMetrics.hits;
    return widget;
  }
  ++m_poolMetrics.misses;
  return new MediaItemWidget(m_virtualContainer);
}

void ScrollManager::releaseWidget(MediaItemWidget *widget) {
  if (widget == nullptr) return;
  
  widget->hide();
  // Disconnect specific signals we connect, not the wildcard
  QObject::disconnect(widget, &MediaItemWidget::clicked, nullptr, nullptr);
  QObject::disconnect(widget, &MediaItemWidget::doubleClicked, nullptr, nullptr);
  QObject::disconnect(widget, &MediaItemWidget::subcollectionClicked, nullptr, nullptr);
  QObject::disconnect(widget, &MediaItemWidget::subcollectionDoubleClicked, nullptr, nullptr);
  
  widget->setSelected(false);
  widget->setFilePath(QString());
  widget->setItemName(QString());
  widget->setArtworkPixmap(QPixmap());
  
  const int optimalSize = calculateOptimalPoolSize();
  if (m_widgetPool.size() < optimalSize) {
    m_widgetPool.append(widget);
    ++m_poolMetrics.releases;
  } else {
    widget->deleteLater();
    ++m_poolMetrics.discards;
  }
}

// Calculates optimal pool size based on visible items with buffer
auto ScrollManager::calculateOptimalPoolSize() const -> int {
  if (m_metrics.itemsPerRow <= 0) {
    return UIConstants::Widget::Pool::MIN_SIZE;
  }
  
  int visibleRows = (getLastVisibleRow() - getFirstVisibleRow()) + 1 + UIConstants::BUFFER_ROWS;
  int visibleWidgets = visibleRows * m_metrics.itemsPerRow;
  int poolSize = visibleWidgets * UIConstants::Widget::Pool::BUFFER_MULTIPLIER;
  
  return std::clamp(poolSize, 
                    UIConstants::Widget::Pool::MIN_SIZE, 
                    UIConstants::Widget::Pool::MAX_SIZE);
}

void ScrollManager::clearWidgetPool() {
  for (MediaItemWidget *widget : m_widgetPool) {
    if (widget) {
      widget->deleteLater();
    }
  }
  m_widgetPool.clear();
}

void ScrollManager::setupReferences(const ScrollManagerSetup &setup) {
  m_gridContainer = setup.gridContainer;
  m_mediaScrollArea = setup.mediaScrollArea;
  m_artworkManager = setup.artworkManager;
  m_collections = setup.collections;
  m_hierarchyCache = setup.hierarchyCache;

  if (m_mediaScrollArea != nullptr) {
    m_mediaScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if (auto *horizontalScrollbar = m_mediaScrollArea->horizontalScrollBar()) {
      horizontalScrollbar->setValue(0);
      horizontalScrollbar->hide();
    }
  }
}

// Initializes virtual scrolling and prepares virtual container; primes mappings
// for aggregated views.
void ScrollManager::setupVirtualScrolling(int totalCount, const CollectionContext &context) {
  if ((m_gridContainer == nullptr) || (m_mediaScrollArea == nullptr)) {
    return;
  }

  cleanup();

  m_lastSelectedIndex = -1;
  m_committedSelectedIndex = -1;
  m_lastSelectedRow = -1;

  m_context = context;
  // m_filePaths and m_fileNames will be populated on demand
  m_filePaths.clear();
  m_fileNames.clear();
  
  // Pre-fill m_filePaths with empty strings to reserve space
  // But wait, m_filePaths is a QStringList (QList<QString>).
  // If totalCount is huge, this is fine.
  // However, we need to handle subcollections separately?
  // The original code had m_subcollections + m_filePaths.
  // Now totalCount includes items. Subcollections are separate?
  // DatabaseWorker::fetchItemCount returns count of ITEMS.
  // Subcollections are handled by ScrollManager::initializeSubcollections.
  
  initializeSubcollections();
  
  if (!m_context.filePaths.isEmpty()) {
    m_filePaths = m_context.filePaths;
    m_fileNames = m_context.fileNames;
  } else {
    // The total items in the grid = subcollections + items.
    // We need to know how many items are there.
    int itemCount = totalCount - m_subcollections.size();
    if (itemCount < 0) {
      itemCount = 0;
    }
    
    // Resize m_filePaths to itemCount with empty strings
    m_filePaths.reserve(itemCount);
    for(int i=0; i<itemCount; ++i) m_filePaths.append(QString());
  }

  m_totalItems = m_subcollections.size() + m_filePaths.size();
  
  if (m_totalItems == 0) {
    setupEmptyVirtualScrolling();
    return;
  }

  setupNormalVirtualScrolling();
}

void ScrollManager::receiveItemsRange(int offset, const QStringList &filePaths, const QHash<QString, QString> &fileNames) {
    if (offset < 0 || offset >= m_filePaths.size()) return;
    
    int subCount = m_subcollections.size();
    
    for (int i = 0; i < filePaths.size(); ++i) {
        int index = offset + i;
        if (index < m_filePaths.size()) {
            m_filePaths[index] = filePaths[i];
            m_fileNames[filePaths[i]] = fileNames.value(filePaths[i]);
            
            // Release placeholder widgets so they get re-created with actual data
            int visualIndex = subCount + index;
            if (MediaItemWidget *widget = m_activeWidgets.value(visualIndex, nullptr)) {
                releaseWidget(widget);
                m_activeWidgets.remove(visualIndex);
            }
        }
    }
    
    // Trigger update of visible widgets
    updateVirtualView();
}

void ScrollManager::initializeSubcollections() {
  m_subcollections.clear();
  if ((m_collections != nullptr) && m_context.currentIndex >= 0) {
    // Use cache for O(1) lookup if available
    if (m_hierarchyCache != nullptr && m_hierarchyCache->isValid()) {
      m_subcollections = m_hierarchyCache->directChildren(m_context.currentIndex);
    } else {
      // Fallback to O(n) scan
      m_subcollections = CollectionUtils::directChildrenOf(m_context.currentIndex, *m_collections);
    }
  }
}

void ScrollManager::setupFilePathMappings() {
  m_filePathToDisplayName.clear();
  if (m_context.config.showAllSubcollectionItems) {
    for (auto it = m_fileNames.constBegin(); it != m_fileNames.constEnd();
         ++it) {
      m_filePathToDisplayName[it.key()] = it.value();
    }
  }
}

void ScrollManager::processRelativeFilePaths() {
  m_rawToFullPath.clear();
  if (!m_context.config.showAllSubcollectionItems) {
    return;
  }

  for (const QString &rawEntry : m_filePaths) {
    if (QDir::isAbsolutePath(rawEntry)) {
      m_rawToFullPath.insert(rawEntry, rawEntry);
      continue;
    }

    QString matchedFullPath;
    for (auto it = m_fileNames.constBegin(); it != m_fileNames.constEnd();
         ++it) {
      const QString &key = it.key();
      if (key.endsWith("/" + rawEntry) ||
          key.endsWith(QDir::separator() + rawEntry) || key == rawEntry) {
        matchedFullPath = key;
        break;
      }
    }

    if (matchedFullPath.isEmpty() && (m_databaseManager != nullptr) &&
        (m_collections != nullptr)) {
      int ownerIndex = m_databaseManager->getCollectionIndexForFile(rawEntry);
      if (ownerIndex >= 0 && ownerIndex < m_collections->size()) {
        const QString mediaDir = (*m_collections)[ownerIndex].mediaDirectory;
        if (!mediaDir.trimmed().isEmpty()) {
          matchedFullPath = QDir(mediaDir).absoluteFilePath(rawEntry);
        }
      }
    }

    if (!matchedFullPath.isEmpty()) {
      m_rawToFullPath.insert(rawEntry, matchedFullPath);
    }
  }
}

void ScrollManager::setupEmptyVirtualScrolling() {
  calculateVirtualMetrics();
  createVirtualContainer();
  if (m_virtualContainer != nullptr) {
    m_virtualContainer->setVisible(true);
  }
  emit virtualScrollSetupComplete();
}

void ScrollManager::setupNormalVirtualScrolling() {
  calculateVirtualMetrics();
  createVirtualContainer();
  updateVirtualView();
  positionVirtualContainer();
  if (m_virtualContainer != nullptr) {
    m_virtualContainer->setVisible(true);
  }
  emit virtualScrollSetupComplete();
}

void ScrollManager::cleanup() {
  if (m_destroying) {
    return;
  }
  if (m_activeWidgets.isEmpty() && (m_virtualContainer == nullptr) &&
      m_filePaths.isEmpty() && m_subcollections.isEmpty()) {
    return;
  }

  disconnectScrollEvents();

  for (auto it = m_activeWidgets.begin(); it != m_activeWidgets.end(); ++it) {
    if (MediaItemWidget *widget = it.value()) {
      releaseWidget(widget);
    }
  }
  m_activeWidgets.clear();

  cleanupVirtualContainer();
  clearWidgetPool(); // Pool widgets are tied to the old virtual container
  m_filePaths.clear();
  m_fileNames.clear();
  m_subcollections.clear();
  m_filteredIndices.clear();
  m_currentFilter.clear();
  m_isFiltered = false;
  m_totalItems = 0;
}

void ScrollManager::updateGridWidth(int newGridWidth) {
  if (m_context.config.gridWidth == newGridWidth) {
    return;
  }
  m_context.config.gridWidth = newGridWidth;
  if (m_virtualContainer == nullptr) {
    return;
  }
  calculateVirtualMetrics();
  positionVirtualContainer();
  for (auto it = m_activeWidgets.begin(); it != m_activeWidgets.end(); ++it) {
    MediaItemWidget *widget = it.value();
    if (widget == nullptr) {
      continue;
    }
    QPoint position = getItemPosition(it.key());
    widget->setGeometry(position.x(), position.y(), m_metrics.itemWidth,
                        m_metrics.itemHeight);
  }
  updateVirtualView();
}

// Updates active widgets for current viewport and triggers artwork updates
// unless suppression is enforced without selection allowance
void ScrollManager::updateVirtualView() {
  if (m_destroying || QApplication::closingDown()) {
    return;
  }
  if ((m_virtualContainer == nullptr) || (m_mediaScrollArea == nullptr)) {
    return;
  }
  if (m_metrics.itemsPerRow <= 0) {
    return;
  }

  QSet<int> needed = calculateNeededIndices();

  for (int visualIndex : needed) {
    ensureWidgetForIndex(visualIndex);
  }

  removeUnneededWidgets(needed);
  updateArtworkIfAllowed();

  if (m_selectionOverlay != nullptr) {
    if (m_forceSelectionOverlayVisible && m_lastSelectedIndex >= 0) {
      refreshSelectionOverlayState();
    }
    m_selectionOverlay->raise();
  }
}

auto ScrollManager::calculateNeededIndices() const -> QSet<int> {
  int firstVisible = getFirstVisibleRow();
  int lastVisible = getLastVisibleRow();
  int startRow = qMax(0, firstVisible - 1);
  int endRow = lastVisible + 1;

  int maxRow =
      ((m_totalItems + m_metrics.itemsPerRow - 1) / m_metrics.itemsPerRow) - 1;
  if (maxRow < 0) {
    return {};
  }
  endRow = std::min(endRow, maxRow);

  QSet<int> needed;
  for (int rowIndex = startRow; rowIndex <= endRow; ++rowIndex) {
    for (int columnIndex = 0; columnIndex < m_metrics.itemsPerRow;
         ++columnIndex) {
      int visualIndex = (rowIndex * m_metrics.itemsPerRow) + columnIndex;
      if (visualIndex < m_totalItems) {
        needed.insert(visualIndex);
      }
    }
  }
  return needed;
}

void ScrollManager::removeUnneededWidgets(const QSet<int> &needed) {
  QList<int> existing = m_activeWidgets.keys();
  for (int visualIndex : existing) {
    if (!needed.contains(visualIndex)) {
      if (MediaItemWidget *widget = m_activeWidgets.value(visualIndex)) {
        releaseWidget(widget);
      }
      m_activeWidgets.remove(visualIndex);
    }
  }
}

void ScrollManager::updateArtworkIfAllowed() {
  if (!QApplication::closingDown() && m_artworkManager) {
    const bool suppressArtwork =
        (m_mediaScrollArea != nullptr) &&
        m_mediaScrollArea->property(PropertyKeys::SuppressArtwork).toBool();
    const bool allowDuringSelection =
        (m_mediaScrollArea != nullptr) &&
        m_mediaScrollArea->property(PropertyKeys::AllowArtworkDuringSelection)
            .toBool();
    if (!suppressArtwork || allowDuringSelection) {
      m_artworkManager->updateViewportArtwork();
    }
  }
}

auto ScrollManager::getEffectiveHorizontalSpacing() const -> int {
  return m_context.config.horizontalSpacing;
}

auto ScrollManager::getFirstVisibleRow() const -> int {
  if (m_mediaScrollArea == nullptr) {
    return 0;
  }
  int scrollOffsetY = m_mediaScrollArea->verticalScrollBar()->value();
  int rowHeight = m_metrics.itemHeight + m_metrics.verticalSpacing;
  return rowHeight > 0
             ? qMax(0, (scrollOffsetY - m_metrics.margins) / rowHeight)
             : 0;
}

auto ScrollManager::getLastVisibleRow() const -> int {
  if (m_mediaScrollArea == nullptr) {
    return 0;
  }
  int scrollOffsetY = m_mediaScrollArea->verticalScrollBar()->value();
  int viewportHeight = m_mediaScrollArea->viewport()->height();
  int rowHeight = m_metrics.itemHeight + m_metrics.verticalSpacing;
  if (rowHeight <= 0) {
    return 0;
  }
  return (scrollOffsetY + viewportHeight - m_metrics.margins) / rowHeight;
}

// Performs arrow key recentering unless suppressed by user scroll or timing
// properties
// Handles arrow key scroll animation to center selected item
void ScrollManager::onArrowKeyViewUpdate() {
  if ((m_mediaScrollArea == nullptr) || (m_virtualContainer == nullptr) ||
      m_lastSelectedIndex < 0 || m_lastSelectedIndex >= m_totalItems) {
    return;
  }

  QScrollBar *verticalScrollBar = m_mediaScrollArea->verticalScrollBar();
  if (verticalScrollBar == nullptr) {
    return;
  }

  if (shouldSkipArrowKeyUpdate()) {
    if (auto *arrowKeyAnimation =
            verticalScrollBar->findChild<QPropertyAnimation *>(
                "arrowKeyScrollAnim")) {
      if (arrowKeyAnimation->state() == QAbstractAnimation::Running) {
        arrowKeyAnimation->stop();
      }
    }
    return;
  }

  int viewportHeight = (m_mediaScrollArea->viewport() != nullptr)
                           ? m_mediaScrollArea->viewport()->height()
                           : 0;
  if (viewportHeight <= 0) {
    return;
  }

  int target = calculateCenterScrollTarget(m_lastSelectedIndex, viewportHeight);
  int current = verticalScrollBar->value();

  if (qAbs(current - target) <= 2) {
    updateVirtualView();
    return;
  }

  setupAndStartCenterAnimation(verticalScrollBar, current, target);
}

auto ScrollManager::shouldSkipArrowKeyUpdate() const -> bool {
  if (m_userScrollbarActive ||
      m_mediaScrollArea->property(PropertyKeys::SuppressArrowCenter).toBool() ||
      m_mediaScrollArea->property(PropertyKeys::UserScrollActive).toBool()) {
    return true;
  }

  qint64 until =
      m_mediaScrollArea->property(PropertyKeys::SuppressArrowCenterUntilMs)
          .toLongLong();
  return (until > 0 && QDateTime::currentMSecsSinceEpoch() < until);
}

auto ScrollManager::calculateCenterScrollTarget(int selectedIndex,
                                                int viewportHeight) const
    -> int {
  QPoint itemPos = getItemPosition(selectedIndex);
  int itemY = itemPos.y();
  int itemHeight =
      (m_metrics.itemHeight > 0 ? m_metrics.itemHeight
                                : UIConstants::DEFAULT_ITEM_HEIGHT);
  int margins =
      m_metrics.margins > 0 ? m_metrics.margins : UIConstants::GRID_MARGINS;

  int target = (margins + itemY) + (itemHeight / 2) - (viewportHeight / 2);
  QScrollBar *verticalScrollBar = m_mediaScrollArea->verticalScrollBar();
  return qBound(0, target, verticalScrollBar->maximum());
}

void ScrollManager::setupAndStartCenterAnimation(QScrollBar *scrollBar,
                                                 int current, int target) {
  int rowStride = m_metrics.itemHeight + m_metrics.verticalSpacing;
  int singleRowDuration = UIConstants::CENTER_SCROLL_BASE_DURATION +
                          UIConstants::CENTER_SCROLL_PER_ROW;
  singleRowDuration = std::max(
      UIConstants::CENTER_SCROLL_MIN_DURATION,
      std::min(singleRowDuration, UIConstants::CENTER_SCROLL_MAX_DURATION));

  static constexpr double DEFAULT_PIXELS_PER_MILLISECOND = 0.5;
  double pixelsPerMillisecond = (rowStride > 0 && singleRowDuration > 0)
                                    ? static_cast<double>(rowStride) /
                                          static_cast<double>(singleRowDuration)
                                    : DEFAULT_PIXELS_PER_MILLISECOND;

  auto *animation =
      scrollBar->findChild<QPropertyAnimation *>("arrowKeyScrollAnim");
  if (animation == nullptr) {
    animation = new QPropertyAnimation(scrollBar, "value", scrollBar);
    animation->setObjectName("arrowKeyScrollAnim");
    animation->setEasingCurve(QEasingCurve::OutCubic);
  }

  if (animation->state() == QAbstractAnimation::Running) {
    animation->stop();
  }

  int distance = std::abs(current - target);
  int duration =
      (pixelsPerMillisecond > 0.0)
          ? static_cast<int>(std::round(static_cast<double>(distance) /
                                        pixelsPerMillisecond))
          : singleRowDuration;
  duration =
      std::max(UIConstants::CENTER_SCROLL_MIN_DURATION,
               std::min(duration, UIConstants::CENTER_SCROLL_MAX_DURATION));

  animation->setDuration(duration);
  animation->setStartValue(current);
  animation->setEndValue(target);

  QObject::disconnect(animation, nullptr, this, nullptr);
  connect(animation, &QPropertyAnimation::valueChanged, this,
          [this]() { updateVirtualView(); });

  if (m_mediaScrollArea != nullptr) {
    m_mediaScrollArea->setProperty(PropertyKeys::ProgrammaticScroll, true);
  }
  connect(animation, &QPropertyAnimation::finished, this, [this]() {
    if (m_mediaScrollArea) {
      m_mediaScrollArea->setProperty(PropertyKeys::ProgrammaticScroll, false);
    }
  });

  animation->start();
}

void ScrollManager::setupSelectionOverlay() {
  if (m_selectionOverlay.isNull()) {
    m_selectionOverlay = new QWidget(m_virtualContainer);
    m_selectionOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_selectionOverlay->setAttribute(Qt::WA_NoSystemBackground, true);
    m_selectionOverlay->setStyleSheet(
        QString("background: transparent; border:%1px solid "
                "palette(highlight); border-radius:%2px;")
            .arg(UIConstants::BORDER_WIDTH_SELECTION)
            .arg(UIConstants::BORDER_RADIUS));
    // QPointer auto-nullifies when widget is destroyed, no manual tracking needed
  }
}

void ScrollManager::setupSelectionAnimation() {
  if (m_selectionOverlayAnim.isNull()) {
    m_selectionOverlayAnim =
        new QPropertyAnimation(m_selectionOverlay, "geometry", this);
    m_selectionOverlayAnim->setEasingCurve(QEasingCurve::Linear);
    QObject::disconnect(m_selectionOverlayAnim, nullptr, this, nullptr);
    connect(
        m_selectionOverlayAnim, &QPropertyAnimation::finished, this, [this]() {
          debugLog(QString("animation finished callback: restarting=%1 forceVisible=%2")
                   .arg(m_restartingSelectionAnim).arg(m_forceSelectionOverlayVisible));
          // If we're restarting the animation, don't run finish logic
          if (m_restartingSelectionAnim) {
            return;
          }
          
          // During click-hold, keep GlideAnimating true so widget doesn't draw its own border
          // and keep overlay visible - next animation will start shortly
          const bool keepVisible = shouldKeepSelectionOverlayVisible();
          if (!keepVisible) {
            if (m_gridContainer) {
              m_gridContainer->setProperty(PropertyKeys::GlideAnimating, false);
            }
          }
          
          if (m_committedSelectedIndex >= 0 &&
              m_committedSelectedIndex != m_lastSelectedIndex) {
            if (auto *prevSel =
                    m_activeWidgets.value(m_committedSelectedIndex, nullptr)) {
              prevSel->setSelected(false);
            }
          }
          if (auto *newSel =
                  m_activeWidgets.value(m_lastSelectedIndex, nullptr)) {
            newSel->setSelected(true);
          }
          m_committedSelectedIndex = m_lastSelectedIndex;
          
          debugLog(QString("  anim finish: keepVisible=%1").arg(keepVisible));
          if (m_selectionOverlay) {
            if (keepVisible) {
              m_selectionOverlay->raise();
            } else {
              debugLog("  HIDING overlay in anim finish callback");
              m_selectionOverlay->hide();
            }
          }
          updateVirtualView();
        });
  }
}

auto ScrollManager::selectionOverlayRectForWidget(MediaItemWidget *widget) const
    -> QRect {
  if (widget == nullptr) {
    return {};
  }
  QRect borderRect = widget->selectionBorderRectInParent();
  if (borderRect.isValid()) {
    return borderRect;
  }

  QRect widgetRect = widget->geometry();
  if (!widgetRect.isValid()) {
    return {};
  }

  const int inset = UIConstants::COLLECTION_ITEM_SPACING;
  return widgetRect.adjusted(-inset, -inset, inset, inset);
}

auto ScrollManager::selectionOverlayRectForIndex(int visualIndex) const
    -> QRect {
  if (visualIndex < 0 || visualIndex >= m_totalItems) {
    return {};
  }
  QPoint pos = getItemPosition(visualIndex);
  const int inset = UIConstants::COLLECTION_ITEM_SPACING;
  return QRect(pos.x() - inset, pos.y() - inset,
               m_metrics.itemWidth + 2 * inset,
               m_metrics.itemHeight + 2 * inset);
}

void ScrollManager::applySelectionOverlayToWidget(MediaItemWidget *widget) {
  if (widget == nullptr) {
    return;
  }
  setupSelectionOverlay();
  if (m_selectionOverlay == nullptr) {
    return;
  }

  const QRect rect = selectionOverlayRectForWidget(widget);
  if (!rect.isValid()) {
    return;
  }

  m_selectionOverlay->setGeometry(rect);
  m_selectionOverlay->show();
  m_selectionOverlay->raise();
}

auto ScrollManager::shouldKeepSelectionOverlayVisible() const -> bool {
  return m_forceSelectionOverlayVisible;
}

void ScrollManager::refreshSelectionOverlayState() {
  debugLog(QString("refreshSelectionOverlayState: m_lastSelectedIndex=%1 forceVisible=%2")
           .arg(m_lastSelectedIndex).arg(m_forceSelectionOverlayVisible));
  
  if (!shouldKeepSelectionOverlayVisible()) {
    if (m_selectionOverlay != nullptr) {
      debugLog("  HIDING overlay (not force visible)");
      m_selectionOverlay->hide();
    }

    bool glideWasActive = false;
    if (m_gridContainer != nullptr) {
      glideWasActive =
          m_gridContainer->property(PropertyKeys::GlideAnimating).toBool();
      m_gridContainer->setProperty(PropertyKeys::GlideAnimating, false);
    }

    if (glideWasActive && m_lastSelectedIndex >= 0) {
      ensureWidgetForIndex(m_lastSelectedIndex);
      if (auto *selectedWidget =
              m_activeWidgets.value(m_lastSelectedIndex, nullptr)) {
        debugLog("  requesting widget repaint for selection border");
        selectedWidget->update();
      }
    }
    return;
  }

  // Don't interfere with an ongoing selection overlay animation
  if ((m_selectionOverlayAnim != nullptr) &&
      m_selectionOverlayAnim->state() == QAbstractAnimation::Running) {
    debugLog("  animation running, just show/raise");
    if (m_selectionOverlay != nullptr) {
      m_selectionOverlay->show();
      m_selectionOverlay->raise();
    }
    return;
  }

  // During click-hold, we respect the current visibility state (managed by move handlers)
  // If visible, keep it on top. If hidden (e.g. wrapped row), keep it hidden.
  if (m_forceSelectionOverlayVisible) {
    if (m_selectionOverlay != nullptr && m_selectionOverlay->isVisible()) {
      debugLog("  click-hold mode, raising visible overlay");
      m_selectionOverlay->raise();
    } else {
      debugLog("  click-hold mode, overlay hidden (respecting state)");
    }
    return;
  }

  if (m_lastSelectedIndex < 0) {
    return;
  }

  setupSelectionOverlay();
  if (m_selectionOverlay == nullptr) {
    return;
  }

  // Position overlay directly (non-click-hold case or initial positioning)
  QRect rect = selectionOverlayRectForIndex(m_lastSelectedIndex);
  debugLog(QString("  setting geometry to rect: x=%1 y=%2 w=%3 h=%4")
           .arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height()));
  if (rect.isValid()) {
    m_selectionOverlay->setGeometry(rect);
    m_selectionOverlay->show();
    m_selectionOverlay->raise();
    m_selectionOverlay->update();
  }
}

void ScrollManager::setForceSelectionOverlayVisible(bool force) {
  debugLog(QString("setForceSelectionOverlayVisible: %1").arg(force));
  if (m_forceSelectionOverlayVisible == force) {
    return;
  }
  m_forceSelectionOverlayVisible = force;
  refreshSelectionOverlayState();
}

void ScrollManager::handleHorizontalMoveAnimation(int selectedIndex,
                                                  int prevIndex) {
  debugLog(QString("handleHorizontalMoveAnimation: prev=%1 sel=%2 forceVisible=%3")
           .arg(prevIndex).arg(selectedIndex).arg(m_forceSelectionOverlayVisible));
  
  setupSelectionOverlay();
  setupSelectionAnimation();

  if (m_selectionOverlay == nullptr || m_selectionOverlayAnim == nullptr) {
    debugLog("  overlay or anim is null, returning");
    return;
  }

  // Calculate target rect for the new selection
  QRect targetRect = selectionOverlayRectForIndex(selectedIndex);
  if (!targetRect.isValid()) {
    ensureWidgetForIndex(selectedIndex);
    if (auto *w = m_activeWidgets.value(selectedIndex, nullptr)) {
      targetRect = selectionOverlayRectForWidget(w);
    }
  }
  if (!targetRect.isValid()) {
    debugLog("  targetRect invalid, returning");
    return;
  }

  debugLog(QString("  targetRect: x=%1 y=%2").arg(targetRect.x()).arg(targetRect.y()));

  // Get current overlay position for animation
  QRect currentRect = m_selectionOverlay->geometry();
  
  // If overlay isn't visible or valid, try to start from previous selection
  if (!m_selectionOverlay->isVisible() || !currentRect.isValid()) {
    QRect startRect = selectionOverlayRectForIndex(prevIndex);
    if (!startRect.isValid()) {
      ensureWidgetForIndex(prevIndex);
      if (auto *w = m_activeWidgets.value(prevIndex, nullptr)) {
        startRect = selectionOverlayRectForWidget(w);
      }
    }
    if (startRect.isValid()) {
      currentRect = startRect;
    } else {
      currentRect = targetRect;
    }
    m_selectionOverlay->setGeometry(currentRect);
  }

  // Update widget selection states
  if (m_committedSelectedIndex >= 0 && m_committedSelectedIndex != selectedIndex) {
    if (auto *prevWidget = m_activeWidgets.value(m_committedSelectedIndex, nullptr)) {
      prevWidget->setSelected(false);
    }
  }
  ensureWidgetForIndex(selectedIndex);
  if (auto *currWidget = m_activeWidgets.value(selectedIndex, nullptr)) {
    currWidget->setSelected(true);
  }
  m_committedSelectedIndex = selectedIndex;

  // Show and raise overlay AFTER widget operations
  m_selectionOverlay->show();
  m_selectionOverlay->raise();

  if (m_gridContainer != nullptr) {
    m_gridContainer->setProperty(PropertyKeys::GlideAnimating, true);
  }

  // Handle animation - stop if running and get current position
  if (m_selectionOverlayAnim->state() == QAbstractAnimation::Running) {
    currentRect = m_selectionOverlay->geometry();
    m_restartingSelectionAnim = true;
    m_selectionOverlayAnim->stop();
    m_restartingSelectionAnim = false;
  }

  // Calculate animation duration based on distance
  int deltaX = std::abs(currentRect.center().x() - targetRect.center().x());
  int deltaY = std::abs(currentRect.center().y() - targetRect.center().y());
  double distance = std::sqrt(static_cast<double>(deltaX * deltaX + deltaY * deltaY));

  static constexpr double PIXELS_PER_SECOND = 1500.0;
  static constexpr int MIN_DURATION = 50;
  static constexpr int MAX_DURATION = 300;

  int duration = static_cast<int>(std::round((distance / PIXELS_PER_SECOND) * 1000.0));
  duration = std::clamp(duration, MIN_DURATION, MAX_DURATION);

  // Start the glide animation
  m_selectionOverlayAnim->setDuration(duration);
  m_selectionOverlayAnim->setStartValue(currentRect);
  m_selectionOverlayAnim->setEndValue(targetRect);
  m_selectionOverlayAnim->start();
  
  debugLog(QString("  animation started: from (%1,%2) to (%3,%4) duration=%5")
           .arg(currentRect.x()).arg(currentRect.y())
           .arg(targetRect.x()).arg(targetRect.y())
           .arg(duration));
}

void ScrollManager::handleDirectSelectionUpdate(int selectedIndex) {
  const bool keepOverlay = shouldKeepSelectionOverlayVisible();
  if ((m_selectionOverlay != nullptr) && m_selectionOverlay->isVisible()) {
    if ((m_selectionOverlayAnim != nullptr) &&
        m_selectionOverlayAnim->state() == QAbstractAnimation::Running) {
      m_selectionOverlayAnim->stop();
    }
    if (!keepOverlay) {
      m_selectionOverlay->hide();
      if (m_gridContainer != nullptr) {
        m_gridContainer->setProperty(PropertyKeys::GlideAnimating, false);
      }
    }
  }

  if (m_committedSelectedIndex >= 0 &&
      m_committedSelectedIndex != selectedIndex) {
    if (auto *prevSel =
            m_activeWidgets.value(m_committedSelectedIndex, nullptr)) {
      prevSel->setSelected(false);
    }
  }
  ensureWidgetForIndex(selectedIndex);
  auto *currSel = m_activeWidgets.value(selectedIndex, nullptr);
  if (currSel != nullptr) {
    currSel->setSelected(true);
    // Force update to ensure border is drawn if we are in widget-border mode
    currSel->update();
  }
  
  if (keepOverlay) {
    // For direct updates (non-gliding) during click-hold (e.g. row wrap),
    // use widget border instead of overlay to avoid visual glitches.
    // This ensures we always have a visible selection indicator.
    if (m_gridContainer != nullptr) {
      m_gridContainer->setProperty(PropertyKeys::GlideAnimating, false);
    }
    if (m_selectionOverlay != nullptr) {
      m_selectionOverlay->hide();
      debugLog("  handleDirectSelectionUpdate: hiding overlay, using widget border");
    }
  }
  m_committedSelectedIndex = selectedIndex;
}

void ScrollManager::prewarmSurroundingWidgets(int selectedIndex) {
  const int itemsPerRow =
      (m_metrics.itemsPerRow > 0 ? m_metrics.itemsPerRow : 1);
  const int prewarmRows =
      (UIConstants::BUFFER_ROWS > 0 ? UIConstants::BUFFER_ROWS : 2);
  const int halfWindow = prewarmRows * itemsPerRow;
  int start = std::max(0, selectedIndex - halfWindow);
  int end = std::min(m_totalItems - 1, selectedIndex + halfWindow);
  for (int visualIndex = start; visualIndex <= end; ++visualIndex) {
    ensureWidgetForIndex(visualIndex);
  }
}

void ScrollManager::scheduleArrowKeyUpdate(int selectedIndex) {
  bool extendedHold =
      ((m_gridContainer != nullptr) &&
       m_gridContainer->property(PropertyKeys::ArrowKeyScrolling).toBool());
  bool suppressArrowCenter =
      ((m_mediaScrollArea != nullptr) &&
       m_mediaScrollArea->property(PropertyKeys::SuppressArrowCenter).toBool());
  bool userScrollActiveProp =
      ((m_mediaScrollArea != nullptr) &&
       m_mediaScrollArea->property(PropertyKeys::UserScrollActive).toBool());
  bool programmatic =
      ((m_mediaScrollArea != nullptr) &&
       m_mediaScrollArea->property(PropertyKeys::ProgrammaticScroll).toBool());

  if (m_arrowKeyViewUpdateTimer == nullptr) {
    m_arrowKeyViewUpdateTimer = new QTimer(this);
    m_arrowKeyViewUpdateTimer->setSingleShot(true);
    connect(m_arrowKeyViewUpdateTimer, &QTimer::timeout, this,
            &ScrollManager::onArrowKeyViewUpdate);
  }

  bool timeSuppressed = false;
  if (m_mediaScrollArea != nullptr) {
    qint64 until =
        m_mediaScrollArea->property(PropertyKeys::SuppressArrowCenterUntilMs)
            .toLongLong();
    if (until > 0 && QDateTime::currentMSecsSinceEpoch() < until) {
      timeSuppressed = true;
    }
  }

  if (!suppressArrowCenter && !timeSuppressed && !userScrollActiveProp &&
      !m_userScrollbarActive && !programmatic) {
    static constexpr int ARROW_KEY_UPDATE_DELAY_EXTENDED_MS = 16;
    static constexpr int ARROW_KEY_UPDATE_DELAY_NORMAL_MS = 8;
    const int delayMs = extendedHold ? ARROW_KEY_UPDATE_DELAY_EXTENDED_MS
                                     : ARROW_KEY_UPDATE_DELAY_NORMAL_MS;
    m_arrowKeyViewUpdateTimer->start(delayMs);
  }
}

// Updates selection visuals and manages prewarming, overlay animation, and
// arrow-centering properties
void ScrollManager::updateSelectionForIndex(int selectedIndex) {
  debugLog(QString("updateSelectionForIndex: sel=%1 lastSel=%2 forceVisible=%3")
           .arg(selectedIndex).arg(m_lastSelectedIndex).arg(m_forceSelectionOverlayVisible));
  
  if (m_destroying || (m_mediaScrollArea == nullptr) || selectedIndex < 0 ||
      selectedIndex >= m_totalItems) {
    debugLog("  early return due to invalid state");
    return;
  }

  int prevIndex = m_lastSelectedIndex;
  const bool sameSelection = (prevIndex == selectedIndex);

  if (!sameSelection) {
    m_lastSelectedIndex = selectedIndex;
    debugLog(QString("  updated m_lastSelectedIndex to %1").arg(selectedIndex));

    if (prevIndex >= 0) {
      int delta = selectedIndex - prevIndex;
      if (delta == 0) {
        m_selectionDirection = 0;
      } else if (delta > 0) {
        m_selectionDirection = 1;
      } else {
        m_selectionDirection = -1;
      }
    } else {
      m_selectionDirection = 0;
    }
    m_lastSelectedRow =
        GridUtils::computeItemRow(selectedIndex, m_metrics.itemsPerRow);
  }

  prewarmSurroundingWidgets(selectedIndex);

  auto getWidget = [&](int visual) -> MediaItemWidget * {
    ensureWidgetForIndex(visual);
    return m_activeWidgets.value(visual, nullptr);
  };

  MediaItemWidget *currentWidget = getWidget(selectedIndex);
  
  // During click-holds, we must update overlay even if widget is null
  const bool keepOverlay = shouldKeepSelectionOverlayVisible();
  
  if (currentWidget == nullptr) {
    debugLog(QString("  currentWidget is null for index %1").arg(selectedIndex));
    // Widget not available, but during click-hold we still update overlay position
    if (keepOverlay && !sameSelection) {
      setupSelectionOverlay();
      if (m_selectionOverlay != nullptr) {
        QRect rect = selectionOverlayRectForIndex(selectedIndex);
        if (rect.isValid()) {
          m_selectionOverlay->setGeometry(rect);
          m_selectionOverlay->show();
          m_selectionOverlay->raise();
          debugLog(QString("  positioned overlay directly at (%1,%2)").arg(rect.x()).arg(rect.y()));
        }
      }
    }
    return;
  }

  if (sameSelection) {
    debugLog("  same selection, returning");
    
    // If an animation is running, we MUST let it finish to achieve the "glide" effect.
    // Interrupting it here would cause the overlay to disappear and the widget border 
    // to snap back immediately, defeating the purpose of the animation.
    if ((m_selectionOverlayAnim != nullptr) &&
        m_selectionOverlayAnim->state() == QAbstractAnimation::Running) {
      debugLog("  animation running during same-selection update - letting it continue");
      // Ensure overlay is visible
      if (m_selectionOverlay != nullptr && !m_selectionOverlay->isVisible()) {
        m_selectionOverlay->show();
        m_selectionOverlay->raise();
      }
      // Do NOT disable GlideAnimating or stop animation.
      // The animation finished handler will take care of cleanup.
      return;
    }

    if (keepOverlay) {
      applySelectionOverlayToWidget(currentWidget);
      if (m_selectionOverlay != nullptr && !m_selectionOverlay->isVisible()) {
        // Fallback to index-based positioning if widget-based failed
        QRect rect = selectionOverlayRectForIndex(selectedIndex);
        if (rect.isValid()) {
          m_selectionOverlay->setGeometry(rect);
          m_selectionOverlay->show();
          m_selectionOverlay->raise();
        }
      }
    } else if ((m_selectionOverlay != nullptr) &&
               m_selectionOverlay->isVisible()) {
      m_selectionOverlay->hide();
    }
    if (m_committedSelectedIndex >= 0 &&
        m_committedSelectedIndex != selectedIndex) {
      if (auto *prevSel =
              m_activeWidgets.value(m_committedSelectedIndex, nullptr)) {
        prevSel->setSelected(false);
      }
    }
    currentWidget->setSelected(true);
    m_committedSelectedIndex = selectedIndex;
    if (m_gridContainer != nullptr && !keepOverlay) {
      m_gridContainer->setProperty(PropertyKeys::GlideAnimating, false);
    }
    scheduleArrowKeyUpdate(selectedIndex);
    return;
  }

  bool isHorizontalMove = false;
  calculateMovementDirection(selectedIndex, prevIndex, m_metrics.itemsPerRow,
                             isHorizontalMove);
  
  debugLog(QString("  isHorizontalMove=%1").arg(isHorizontalMove));

  if (isHorizontalMove) {
    handleHorizontalMoveAnimation(selectedIndex, prevIndex);
  } else {
    handleDirectSelectionUpdate(selectedIndex);
  }
  scheduleArrowKeyUpdate(selectedIndex);
}

// Resolves a raw media entry to its full absolute file path, matching prior
// logic
auto ScrollManager::resolveToFullPath(const QString &rawEntry) const
    -> QString {
  if (m_context.config.showAllSubcollectionItems) {
    if (QDir::isAbsolutePath(rawEntry)) {
      return rawEntry;
    }

    QString mappedFullPath = m_rawToFullPath.value(rawEntry);
    if (!mappedFullPath.isEmpty()) {
      return mappedFullPath;
    }

    for (auto it = m_fileNames.constBegin(); it != m_fileNames.constEnd();
         ++it) {
      const QString &key = it.key();
      if (key.endsWith("/" + rawEntry) ||
          key.endsWith(QDir::separator() + rawEntry) || key == rawEntry) {
        return key;
      }
    }

    if ((m_databaseManager != nullptr) && (m_collections != nullptr)) {
      int ownerIndex = m_databaseManager->getCollectionIndexForFile(rawEntry);
      if (ownerIndex >= 0 && ownerIndex < m_collections->size()) {
        const QString mediaDir = (*m_collections)[ownerIndex].mediaDirectory;
        if (!mediaDir.trimmed().isEmpty()) {
          return QDir(mediaDir).absoluteFilePath(rawEntry);
        }
      }
    }
    return {};
  }

  const QString mediaDir = m_context.config.mediaDirectory.trimmed();
  if (mediaDir.isEmpty()) {
    return {};
  }
  return QDir(mediaDir).absoluteFilePath(rawEntry);
}

auto ScrollManager::getSubcollectionName(int subcollectionIndex) const
    -> QString {
  if ((m_collections == nullptr) || subcollectionIndex < 0 ||
      subcollectionIndex >= m_collections->size()) {
    return {};
  }
  return (*m_collections)[subcollectionIndex].name;
}

void ScrollManager::setDatabaseManager(DatabaseManager *manager) {
  m_databaseManager = manager;
}

void ScrollManager::recenterVirtualContainer() { positionVirtualContainer(); }

auto ScrollManager::getCurrentAlignment() const -> HorizontalAlignment {
  if ((m_collections == nullptr) || m_context.currentIndex < 0 ||
      m_context.currentIndex >= m_collections->size()) {
    return HorizontalAlignment::Center;
  }
  return (*m_collections)[m_context.currentIndex].horizontalAlignment;
}

void ScrollManager::applyFilter(const QString &searchText) {
  QString trimmedQuery = searchText.trimmed();
  if (trimmedQuery.isEmpty()) {
    clearFilter();
    return;
  }
  m_currentFilter = trimmedQuery;
  m_isFiltered = true;
  rebuildFilteredIndices();
  m_totalItems = m_filteredIndices.size();
  calculateVirtualMetrics();
  positionVirtualContainer();
  for (auto it = m_activeWidgets.begin(); it != m_activeWidgets.end(); ++it) {
    if (it.value() != nullptr) {
      it.value()->hide();
    }
  }
  m_activeWidgets.clear();
  if ((m_mediaScrollArea != nullptr) &&
      (m_mediaScrollArea->verticalScrollBar() != nullptr)) {
    m_mediaScrollArea->verticalScrollBar()->setValue(0);
  }

  int subcollectionCount = m_subcollections.size();
  int visibleFiles = 0;
  for (int actualIndex : m_filteredIndices) {
    if (actualIndex >= subcollectionCount) {
      ++visibleFiles;
    }
  }
  int totalFiles = m_filePaths.size();

  emit filterChanged(visibleFiles, totalFiles);
  updateVirtualView();
}

void ScrollManager::cleanupActiveWidgets() {
  for (auto it = m_activeWidgets.begin(); it != m_activeWidgets.end(); ++it) {
    if (MediaItemWidget *widget = it.value()) {
      releaseWidget(widget);
    }
  }
  m_activeWidgets.clear();
}

void ScrollManager::clearFilter() {
  if (!m_isFiltered) {
    emit filterChanged(m_filePaths.size(), m_filePaths.size());
    return;
  }
  m_isFiltered = false;
  m_currentFilter.clear();
  m_filteredIndices.clear();
  m_totalItems = m_subcollections.size() + m_filePaths.size();
  calculateVirtualMetrics();
  positionVirtualContainer();
  for (auto it = m_activeWidgets.begin(); it != m_activeWidgets.end(); ++it) {
    if (it.value() != nullptr) {
      it.value()->hide();
    }
  }
  m_activeWidgets.clear();
  emit filterChanged(m_filePaths.size(), m_filePaths.size());
  updateVirtualView();
}

auto ScrollManager::getFilteredIndex(int visualIndex) const -> int {
  if (!m_isFiltered) {
    return visualIndex;
  }
  if (visualIndex < 0 || visualIndex >= m_filteredIndices.size()) {
    return -1;
  }
  return m_filteredIndices[visualIndex];
}

auto ScrollManager::getScrollbarWidth() const -> int {
  if (m_mediaScrollArea == nullptr) {
    return 0;
  }
  QScrollBar *verticalScrollbar = m_mediaScrollArea->verticalScrollBar();
  if (verticalScrollbar == nullptr) {
    return 0;
  }
  if (verticalScrollbar->isVisible()) {
    return verticalScrollbar->width();
  }
  if (willNeedVerticalScrollbar()) {
    int barWidth = verticalScrollbar->sizeHint().width();
    static constexpr int DEFAULT_SCROLLBAR_WIDTH = 16;
    return barWidth > 0 ? barWidth : DEFAULT_SCROLLBAR_WIDTH;
  }
  return 0;
}

auto ScrollManager::willNeedVerticalScrollbar() const -> bool {
  if (m_mediaScrollArea == nullptr) {
    return false;
  }
  return m_metrics.totalHeight > m_mediaScrollArea->viewport()->height();
}

auto ScrollManager::getTotalItems() const -> int { return m_totalItems; }

void ScrollManager::enforceScrollContentConstraints() {
  if ((m_gridContainer == nullptr) || (m_mediaScrollArea == nullptr)) {
    return;
  }
  m_gridContainer->setMinimumHeight(m_metrics.totalHeight);
  m_gridContainer->setMaximumHeight(m_metrics.totalHeight);
}

void ScrollManager::recreateLayout() {
  if (m_filePaths.isEmpty() && m_subcollections.isEmpty()) {
    return;
  }
  calculateVirtualMetrics();
  positionVirtualContainer();
  for (auto it = m_activeWidgets.begin(); it != m_activeWidgets.end(); ++it) {
    MediaItemWidget *widget = it.value();
    if (widget == nullptr) {
      continue;
    }
    widget->setHideTitles(m_context.config.hideTitles);
    widget->setHideSubcollectionTitles(
        m_context.config.hideSubcollectionTitles);
    widget->setFontSize(m_context.config.fontSize);
    widget->setItemDimensions(m_metrics.itemWidth, m_metrics.itemHeight);
    QPoint position = getItemPosition(it.key());
    widget->setGeometry(position.x(), position.y(), m_metrics.itemWidth,
                        m_metrics.itemHeight);
  }
  updateVirtualView();
}

void ScrollManager::centerHorizontalScrollbar(
    int /*currentCollectionIndex*/,
    const QList<CollectionConfig> & /*collections*/) {
  positionVirtualContainer();
}

void ScrollManager::handleLayoutChange() {
  if (m_destroying || QApplication::closingDown()) {
    return;
  }
  calculateVirtualMetrics();
  positionVirtualContainer();
  for (auto it = m_activeWidgets.begin(); it != m_activeWidgets.end(); ++it) {
    MediaItemWidget *widget = it.value();
    if (widget == nullptr) {
      continue;
    }
    widget->setItemDimensions(m_metrics.itemWidth, m_metrics.itemHeight);
    QPoint position = getItemPosition(it.key());
    widget->setGeometry(position.x(), position.y(), m_metrics.itemWidth,
                        m_metrics.itemHeight);
  }
  updateVirtualView();

  // Clear user scroll state after layout change - scroll position changes
  // during resize should not block subsequent programmatic centering
  if (m_mediaScrollArea != nullptr) {
    m_mediaScrollArea->setProperty(PropertyKeys::UserScrollActive, false);
  }
}

void ScrollManager::notifyUserActivity() {
  if (m_artworkManager) {
    m_artworkManager->updateUserActivity();
  }
}

auto ScrollManager::getCurrentGridWidth() const -> int {
  return m_context.config.gridWidth;
}

void ScrollManager::updateContextForSubcollection(int subcollectionIndex) {
  if ((m_collections == nullptr) || subcollectionIndex < 0 ||
      subcollectionIndex >= m_collections->size()) {
    return;
  }
  m_context.currentIndex = subcollectionIndex;
  m_context.config = (*m_collections)[subcollectionIndex];
  // Use cache for O(1) lookup if available
  if (m_hierarchyCache != nullptr && m_hierarchyCache->isValid()) {
    m_subcollections = m_hierarchyCache->directChildren(subcollectionIndex);
  } else {
    // Fallback to O(n) scan
    m_subcollections = CollectionUtils::directChildrenOf(subcollectionIndex, *m_collections);
  }
  m_totalItems = m_subcollections.size() + m_filePaths.size();
  calculateVirtualMetrics();
  positionVirtualContainer();
  updateVirtualView();
}

/* Apply subcollection filter showing only items belonging to the selected
 * subcollection (and its descendants) */
// Applies filtering to show only items belonging to specified subcollection
void ScrollManager::applySubcollectionFilter(int subcollectionIndex) {
  if ((m_collections == nullptr) || subcollectionIndex < 0 ||
      subcollectionIndex >= m_collections->size()) {
    return;
  }
  if (m_filePaths.isEmpty() && m_subcollections.isEmpty()) {
    return;
  }

  m_isFiltered = true;
  m_currentFilter = (*m_collections)[subcollectionIndex].name;
  m_filteredIndices.clear();

  if (m_context.currentIndex != subcollectionIndex) {
    updateContextForSubcollection(subcollectionIndex);
  }

  QSet<int> targetCollections;
  determineTargetCollections(subcollectionIndex, targetCollections);

  for (int index = 0; index < m_subcollections.size(); ++index) {
    m_filteredIndices.append(index);
  }
  int subcollectionStartIndex = m_subcollections.size();

  for (int mediaIndex = 0; mediaIndex < m_filePaths.size(); ++mediaIndex) {
    const QString &entry = m_filePaths[mediaIndex];
    if (itemBelongsToTargetCollections(entry, targetCollections)) {
      m_filteredIndices.append(subcollectionStartIndex + mediaIndex);
    }
  }

  rebuildFilteredView();
}

void ScrollManager::determineTargetCollections(int subcollectionIndex,
                                               QSet<int> &targetCollections) {
  targetCollections.insert(subcollectionIndex);
  QList<int> descendants;
  // Use cache for O(1) lookup if available
  if (m_hierarchyCache != nullptr && m_hierarchyCache->isValid()) {
    descendants = m_hierarchyCache->allDescendants(subcollectionIndex);
  } else {
    // Fallback to O(n) recursive scan
    descendants = CollectionUtils::collectDescendantIndices(subcollectionIndex, *m_collections);
  }
  for (int descendant : descendants) {
    targetCollections.insert(descendant);
  }
}

auto ScrollManager::itemBelongsToTargetCollections(
    const QString &entry, const QSet<int> &targetCollections) -> bool {
  if (m_databaseManager != nullptr) {
    int collectionIndexForEntry =
        m_databaseManager->getCollectionIndexForFile(entry);
    if (collectionIndexForEntry >= 0 &&
        targetCollections.contains(collectionIndexForEntry)) {
      return true;
    }
    if (collectionIndexForEntry < 0) {
      for (auto it = m_fileNames.constBegin(); it != m_fileNames.constEnd();
           ++it) {
        if (it.key().endsWith("/" + entry) ||
            it.key().endsWith(QDir::separator() + entry) || it.key() == entry) {
          int altCollectionIndex =
              m_databaseManager->getCollectionIndexForFile(it.key());
          if (altCollectionIndex >= 0 &&
              targetCollections.contains(altCollectionIndex)) {
            return true;
          }
          break;
        }
      }
    }
  } else {
    QString display = m_filePathToDisplayName.value(entry);
    if (display.isEmpty()) {
      display = QFileInfo(entry).completeBaseName();
    }
    return display.contains(m_currentFilter, Qt::CaseInsensitive);
  }
  return false;
}

void ScrollManager::rebuildFilteredView() {
  m_totalItems = m_filteredIndices.size();
  calculateVirtualMetrics();
  positionVirtualContainer();

  for (auto it = m_activeWidgets.begin(); it != m_activeWidgets.end(); ++it) {
    if (it.value() != nullptr) {
      it.value()->hide();
      it.value()->deleteLater();
    }
  }
  m_activeWidgets.clear();

  if ((m_mediaScrollArea != nullptr) &&
      (m_mediaScrollArea->verticalScrollBar() != nullptr)) {
    m_mediaScrollArea->verticalScrollBar()->setValue(0);
  }

  emit filterChanged(m_totalItems,
                     m_subcollections.size() + m_filePaths.size());
  updateVirtualView();
  enforceScrollContentConstraints();
}

auto ScrollManager::getEffectiveViewportWidth() const -> int {
  if (m_mediaScrollArea == nullptr) {
    return 0;
  }
  int viewportWidth = m_mediaScrollArea->viewport()->width();
  static constexpr int MIN_EFFECTIVE_VIEWPORT_WIDTH = 200;
  return qMax(MIN_EFFECTIVE_VIEWPORT_WIDTH, viewportWidth);
}

void ScrollManager::recalculateContainerMetrics() {
  if (m_virtualContainer == nullptr) {
    return;
  }
  calculateVirtualMetrics();
  positionVirtualContainer();
  updateVirtualView();
}

void ScrollManager::forceVirtualViewUpdate() {
  calculateVirtualMetrics();
  positionVirtualContainer();
  updateVirtualView();
}

void ScrollManager::preCalculateLayout() {
  calculateVirtualMetrics();
  positionVirtualContainer();
}

// Create the virtual container without showing it immediately
void ScrollManager::createVirtualContainer() {
  cleanupVirtualContainer();
  m_virtualContainer = new QWidget(m_gridContainer);
  m_virtualContainer->setObjectName("virtualContainer");
  connectScrollEvents();
  positionVirtualContainer();
}

// Prime the container with target collection metrics before items are loaded
void ScrollManager::primeLayoutFor(const CollectionConfig &config) {
  if ((m_gridContainer == nullptr) || (m_mediaScrollArea == nullptr)) {
    return;
  }
  m_context.config = config;
  int savedTotal = m_totalItems;
  m_totalItems = 0;
  calculateVirtualMetrics();
  if (m_virtualContainer == nullptr) {
    createVirtualContainer();
  }
  positionVirtualContainer();
  m_totalItems = savedTotal;
}

// Positions virtual scrolling container with alignment and overflow handling
void ScrollManager::positionVirtualContainer() {
  if ((m_virtualContainer == nullptr) || (m_mediaScrollArea == nullptr)) {
    return;
  }

  int viewportWidth = getEffectiveViewportWidth();
  int scrollbarWidth = getScrollbarWidth();

  bool scrollbarVisible = (m_mediaScrollArea->verticalScrollBar() != nullptr) &&
                          m_mediaScrollArea->verticalScrollBar()->isVisible();
  int availableWidth = viewportWidth;
  if (!scrollbarVisible) {
    availableWidth -= scrollbarWidth;
  }

  if (availableWidth < 0) {
    availableWidth = viewportWidth;
  }

  int contentWidth = m_metrics.totalWidth;
  bool overflow = contentWidth > availableWidth;

  setupContainerSizes(availableWidth, contentWidth, overflow);

  HorizontalAlignment align = getEffectiveAlignment();

  bool verticalBarHidden =
      ((m_mediaScrollArea->verticalScrollBar() != nullptr) &&
       !m_mediaScrollArea->verticalScrollBar()->isVisible());
  int leftOffset;
  int rightOffset;
  int centerOffset;
  static constexpr int HIDDEN_SCROLLBAR_LEFT_OFFSET = -5;
  static constexpr int HIDDEN_SCROLLBAR_RIGHT_OFFSET = -20;
  static constexpr int HIDDEN_SCROLLBAR_CENTER_OFFSET = -10;

  leftOffset = verticalBarHidden ? HIDDEN_SCROLLBAR_LEFT_OFFSET : 0;
  rightOffset = verticalBarHidden ? HIDDEN_SCROLLBAR_RIGHT_OFFSET : 0;
  centerOffset = verticalBarHidden ? HIDDEN_SCROLLBAR_CENTER_OFFSET : 0;

  static constexpr int CONTAINER_EXTRA_SHIFT = 20;
  int extraShift = CONTAINER_EXTRA_SHIFT;

  int containerX;
  if (overflow) {
    int overflowAmount = contentWidth - availableWidth;
    containerX = -(overflowAmount / 2) +
                 centerOffset + extraShift;
    if (align == HorizontalAlignment::Center) {
      static constexpr int CENTER_ALIGNMENT_ADJUSTMENT = 10;
      containerX -= CENTER_ALIGNMENT_ADJUSTMENT;
    }
  } else if (contentWidth <= availableWidth) {
    switch (align) {
    case HorizontalAlignment::Left:
      containerX = -UIConstants::CONTAINER_OFFSET -
                   UIConstants::CONTAINER_LEFT_OFFSET + leftOffset;
      break;
    case HorizontalAlignment::Right:
      containerX = availableWidth - contentWidth +
                   UIConstants::CONTAINER_RIGHT_OFFSET + rightOffset + 10;
      break;
    case HorizontalAlignment::Center:
    default:
      containerX = ((availableWidth - contentWidth) / 2) +
                   centerOffset + extraShift;
      break;
    }
  } else {
    int overflowAmount = contentWidth - availableWidth;
    switch (align) {
    case HorizontalAlignment::Left:
      containerX = -UIConstants::CONTAINER_OFFSET - UIConstants::CONTAINER_LEFT_OFFSET +
                   leftOffset;
      break;
    case HorizontalAlignment::Right:
      containerX = -overflowAmount +
                   UIConstants::CONTAINER_RIGHT_OFFSET + rightOffset;
      break;
    case HorizontalAlignment::Center:
    default:
      containerX = -(overflowAmount / 2) +
                   centerOffset + extraShift;
      break;
    }
  }

  configureHorizontalScrollbar(overflow);
  m_virtualContainer->move(containerX, 0);
  m_virtualContainer->resize(m_metrics.totalWidth, m_metrics.totalHeight);
}

void ScrollManager::setupContainerSizes(int availableWidth, int contentWidth,
                                        bool overflow) {
  if (overflow) {
    m_gridContainer->setMinimumSize(availableWidth, m_metrics.totalHeight);
    m_gridContainer->setMaximumWidth(availableWidth);
  } else {
    m_gridContainer->setMinimumSize(qMax(availableWidth, contentWidth),
                                    m_metrics.totalHeight);
    m_gridContainer->setMaximumWidth(QWIDGETSIZE_MAX);
  }
  m_gridContainer->setMaximumHeight(m_metrics.totalHeight);
  m_virtualContainer->setFixedSize(contentWidth, m_metrics.totalHeight);
}

auto ScrollManager::getEffectiveAlignment() const -> HorizontalAlignment {
  HorizontalAlignment align = getCurrentAlignment();
  if (m_isFiltered && m_totalItems > 0 && m_metrics.itemsPerRow > 0) {
    if (m_totalItems < (m_metrics.itemsPerRow - 2)) {
      align = HorizontalAlignment::Center;
    }
  }
  return align;
}

void ScrollManager::configureHorizontalScrollbar(bool overflow) {
  if (overflow && m_mediaScrollArea != nullptr) {
    m_mediaScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if (auto *horizontalScrollbar = m_mediaScrollArea->horizontalScrollBar()) {
      horizontalScrollbar->setValue(0);
      horizontalScrollbar->hide();
    }
  }
}

// Cleans up the virtual container and persistent selection overlay resources
void ScrollManager::cleanupVirtualContainer() {
  debugLog("cleanupVirtualContainer called!");
  if (m_selectionOverlayAnim != nullptr) {
    if (m_selectionOverlayAnim->state() == QAbstractAnimation::Running) {
      m_selectionOverlayAnim->stop();
    }
    m_selectionOverlayAnim->deleteLater();
    m_selectionOverlayAnim = nullptr;
  }
  if (m_selectionOverlay != nullptr) {
    m_selectionOverlay->deleteLater();
    m_selectionOverlay = nullptr;
  }
  if (m_virtualContainer == nullptr) {
    return;
  }
  m_virtualContainer->deleteLater();
  m_virtualContainer = nullptr;
}

void ScrollManager::calculateVirtualMetrics() {
  m_metrics.itemWidth = m_context.config.itemWidth;
  m_metrics.itemHeight = m_context.config.itemHeight;
  m_metrics.itemsPerRow = qMax(1, m_context.config.gridWidth);
  m_metrics.horizontalSpacing = m_context.config.horizontalSpacing;
  m_metrics.verticalSpacing = m_context.config.verticalSpacing;
  m_metrics.margins = UIConstants::GRID_MARGINS;

  GridUtils::calculateGridMetrics(
      m_totalItems, m_metrics.itemsPerRow, m_metrics.itemWidth,
      m_metrics.itemHeight, m_metrics.horizontalSpacing,
      m_metrics.verticalSpacing, m_metrics.margins, m_metrics.totalWidth,
      m_metrics.totalHeight, m_metrics.actualGridWidth);

  m_metrics.totalRows =
      (m_totalItems + m_metrics.itemsPerRow - 1) / m_metrics.itemsPerRow;

  if (m_isFiltered && m_totalItems > 0 &&
      m_totalItems < m_metrics.itemsPerRow) {
    int used = m_totalItems;
    int horizontalSpacingContribution =
        (used > 0 ? (used - 1) * m_metrics.horizontalSpacing : 0);
    m_metrics.totalWidth = m_metrics.margins * 2 + used * m_metrics.itemWidth +
                           horizontalSpacingContribution;
    m_metrics.actualGridWidth = m_metrics.totalWidth;
  }
}

// Connects scrollbars to update logic and sets user scroll activity properties
void ScrollManager::connectScrollEvents() {
  if (m_mediaScrollArea == nullptr) {
    return;
  }

  QScrollBar *verticalScrollbar = m_mediaScrollArea->verticalScrollBar();
  QScrollBar *horizontalScrollbar = m_mediaScrollArea->horizontalScrollBar();

  if (verticalScrollbar != nullptr) {
    connectVerticalScrollEvents(verticalScrollbar);
  }

  if (horizontalScrollbar != nullptr) {
    connectHorizontalScrollEvents(horizontalScrollbar);
  }
}

void ScrollManager::connectVerticalScrollEvents(QScrollBar *verticalScrollbar) {
  m_vScrollConn = connect(verticalScrollbar, &QScrollBar::valueChanged, this,
                          &ScrollManager::onScrollChanged);

  connect(verticalScrollbar, &QScrollBar::sliderPressed, this,
          [this, verticalScrollbar]() {
            m_userScrollbarActive = true;
            if (m_userScrollIdleTimer) {
              m_userScrollIdleTimer->trigger();
            }
            if (m_mediaScrollArea) {
              m_mediaScrollArea->setProperty(PropertyKeys::UserScrollActive,
                                             true);
            }
            if (auto *arrowKeyAnimation =
                    verticalScrollbar->findChild<QPropertyAnimation *>(
                        "arrowKeyScrollAnim")) {
              if (arrowKeyAnimation->state() == QAbstractAnimation::Running) {
                arrowKeyAnimation->stop();
              }
            }
          });

  connect(verticalScrollbar, &QScrollBar::sliderReleased, this, [this]() {
    m_userScrollbarActive = false;
    if (m_userScrollIdleTimer) {
      m_userScrollIdleTimer->trigger();
    }
    QTimer::singleShot(UIConstants::USER_SCROLL_ACTIVE_CLEAR_DELAY_MS, this,
                       [this]() {
                         if (m_mediaScrollArea) {
                           m_mediaScrollArea->setProperty(
                               PropertyKeys::UserScrollActive, false);
                         }
                       });
    updateVirtualView();
  });

  connect(verticalScrollbar, &QAbstractSlider::actionTriggered, this,
          [this, verticalScrollbar](int) {
            m_userScrollbarActive = true;
            if (m_userScrollIdleTimer) {
              m_userScrollIdleTimer->trigger();
            }
            if (m_mediaScrollArea) {
              m_mediaScrollArea->setProperty(PropertyKeys::UserScrollActive,
                                             true);
            }
            if (auto *arrowKeyAnimation =
                    verticalScrollbar->findChild<QPropertyAnimation *>(
                        "arrowKeyScrollAnim")) {
              if (arrowKeyAnimation->state() == QAbstractAnimation::Running) {
                arrowKeyAnimation->stop();
              }
            }
          });
}

void ScrollManager::connectHorizontalScrollEvents(
    QScrollBar *horizontalScrollbar) {
  m_hScrollConn = connect(horizontalScrollbar, &QScrollBar::valueChanged, this,
                          &ScrollManager::onScrollChanged);

  connect(horizontalScrollbar, &QScrollBar::sliderPressed, this, [this]() {
    m_userScrollbarActive = true;
    if (m_userScrollIdleTimer) {
      m_userScrollIdleTimer->trigger();
    }
    if (m_mediaScrollArea) {
      m_mediaScrollArea->setProperty(PropertyKeys::UserScrollActive, true);
    }
  });

  connect(horizontalScrollbar, &QScrollBar::sliderReleased, this, [this]() {
    m_userScrollbarActive = false;
    if (m_userScrollIdleTimer) {
      m_userScrollIdleTimer->trigger();
    }
    QTimer::singleShot(UIConstants::USER_SCROLL_ACTIVE_CLEAR_DELAY_MS, this,
                       [this]() {
                         if (m_mediaScrollArea) {
                           m_mediaScrollArea->setProperty(
                               PropertyKeys::UserScrollActive, false);
                         }
                       });
    updateVirtualView();
  });

  connect(horizontalScrollbar, &QAbstractSlider::actionTriggered, this,
          [this](int) {
            m_userScrollbarActive = true;
            if (m_userScrollIdleTimer) {
              m_userScrollIdleTimer->trigger();
            }
            if (m_mediaScrollArea) {
              m_mediaScrollArea->setProperty(PropertyKeys::UserScrollActive,
                                             true);
            }
          });
}

void ScrollManager::disconnectScrollEvents() {
  if (m_vScrollConn != nullptr) {
    QObject::disconnect(m_vScrollConn);
    m_vScrollConn = QMetaObject::Connection();
  }
  if (m_hScrollConn != nullptr) {
    QObject::disconnect(m_hScrollConn);
    m_hScrollConn = QMetaObject::Connection();
  }
}

// Ensures a widget exists for the visual index; orders click handling to emit
// first so InteractionManager controls selection and scrolling
// Creates and positions widget for given visual index, handling both
// subcollections and media items
void ScrollManager::ensureWidgetForIndex(int visualIndex) {
  if (visualIndex < 0 || visualIndex >= m_totalItems) {
    return;
  }
  if (m_virtualContainer == nullptr) {
    return;
  }

  MediaItemWidget *existing = m_activeWidgets.value(visualIndex, nullptr);
  if (existing != nullptr) {
    if (!existing->isVisible()) {
      existing->show();
    }
    existing->setHideTitles(m_context.config.hideTitles);
    existing->setHideSubcollectionTitles(
        m_context.config.hideSubcollectionTitles);
    existing->setFontSize(m_context.config.fontSize);
    QPoint position = getItemPosition(visualIndex);
    existing->setGeometry(position.x(), position.y(), m_metrics.itemWidth,
                          m_metrics.itemHeight);
    return;
  }

  int actualIndex = getFilteredIndex(visualIndex);
  if (actualIndex < 0) {
    return;
  }

  int subCount = m_subcollections.size();
  MediaItemWidget *itemWidget = nullptr;

  if (actualIndex < subCount) {
    itemWidget = createSubcollectionWidget(visualIndex, actualIndex);
  } else {
    itemWidget = createMediaItemWidget(visualIndex, actualIndex);
  }

  if (itemWidget != nullptr) {
    QPoint position = getItemPosition(visualIndex);
    itemWidget->setGeometry(position.x(), position.y(), m_metrics.itemWidth,
                            m_metrics.itemHeight);
    itemWidget->show();

    // Restore selection state if this widget corresponds to the currently selected index
    if (visualIndex == m_committedSelectedIndex) {
      itemWidget->setSelected(true);
    }

    m_activeWidgets.insert(visualIndex, itemWidget);
  }
}

auto ScrollManager::createSubcollectionWidget(int visualIndex, int actualIndex)
    -> MediaItemWidget * {
  auto *itemWidget = acquireWidget();
  itemWidget->setParent(m_virtualContainer);
  itemWidget->setFocusPolicy(Qt::NoFocus);
  itemWidget->setHideTitles(m_context.config.hideTitles);
  itemWidget->setHideSubcollectionTitles(
      m_context.config.hideSubcollectionTitles);
  itemWidget->setFontSize(m_context.config.fontSize);
  itemWidget->setItemDimensions(m_metrics.itemWidth, m_metrics.itemHeight);

  int subcollectionIndex = m_subcollections[actualIndex];
  itemWidget->setAsSubcollection(subcollectionIndex,
                                 getSubcollectionName(subcollectionIndex));

  connect(itemWidget, &MediaItemWidget::clicked, this, [this, visualIndex]() {
    if (auto *clickedWidget = m_activeWidgets.value(visualIndex)) {
      emit widgetClicked(clickedWidget, QString());
    }
  });
  connect(itemWidget, &MediaItemWidget::subcollectionDoubleClicked, this,
          &ScrollManager::onSubcollectionDoubleClicked);

  return itemWidget;
}

auto ScrollManager::createMediaItemWidget(int visualIndex, int actualIndex)
    -> MediaItemWidget * {
  int subCount = m_subcollections.size();
  int mediaIndex = actualIndex - subCount;
  if (mediaIndex < 0 || mediaIndex >= m_filePaths.size()) {
    return nullptr;
  }

  const QString rawFileName = m_filePaths[mediaIndex];
  if (rawFileName.isEmpty()) {
      int chunkSize = 100;
      int chunkStart = (mediaIndex / chunkSize) * chunkSize;
      emit requestItemsRange(chunkStart, chunkSize);

      auto *itemWidget = acquireWidget();
      itemWidget->setParent(m_virtualContainer);
      itemWidget->setFocusPolicy(Qt::NoFocus);
      itemWidget->setItemDimensions(m_metrics.itemWidth, m_metrics.itemHeight);
      if (itemWidget->nameLabel) {
          itemWidget->nameLabel->setText("Loading...");
      }
      return itemWidget;
  }

  QString fullPath;
  QString displayName;
  int collectionIndex = m_context.currentIndex;

  resolveMediaItemPaths(rawFileName, fullPath, displayName, collectionIndex);
  if (fullPath.isEmpty()) {
    return nullptr;
  }

  auto *itemWidget = acquireWidget();
  itemWidget->setParent(m_virtualContainer);
  itemWidget->setFocusPolicy(Qt::NoFocus);
  itemWidget->setHideTitles(m_context.config.hideTitles);
  itemWidget->setHideSubcollectionTitles(
      m_context.config.hideSubcollectionTitles);
  itemWidget->setFontSize(m_context.config.fontSize);
  itemWidget->setItemDimensions(m_metrics.itemWidth, m_metrics.itemHeight);
  itemWidget->setFilePath(fullPath);
  itemWidget->setItemName(displayName);

  setupMediaItemConnections(itemWidget, visualIndex, fullPath, collectionIndex);
  configureArtworkForWidget(itemWidget, fullPath);

  return itemWidget;
}

void ScrollManager::resolveMediaItemPaths(const QString &rawFileName,
                                          QString &fullPath,
                                          QString &displayName,
                                          int &collectionIndex) {
  if (m_context.config.showAllSubcollectionItems) {
    if (QDir::isAbsolutePath(rawFileName)) {
      fullPath = rawFileName;
    } else {
      fullPath = resolveRelativeFilePath(rawFileName);
    }
    if (!fullPath.isEmpty()) {
      displayName = m_fileNames.value(fullPath, QFileInfo(fullPath)
                                                    .completeBaseName()
                                                    .replace('_', ' ')
                                                    .simplified());
    }
    updateCollectionIndexFromDatabase(fullPath, collectionIndex);
  } else {
    const QString mediaDir = m_context.config.mediaDirectory.trimmed();
    if (!mediaDir.isEmpty()) {
      fullPath = QDir(mediaDir).absoluteFilePath(rawFileName);
      displayName = m_fileNames.value(
          fullPath, QFileInfo(rawFileName).completeBaseName());
    }
  }
}

auto ScrollManager::resolveRelativeFilePath(const QString &rawFileName)
    -> QString {
  QString fullPath = m_rawToFullPath.value(rawFileName);
  if (fullPath.isEmpty()) {
    for (auto it = m_fileNames.constBegin(); it != m_fileNames.constEnd();
         ++it) {
      const QString &key = it.key();
      if (key.endsWith("/" + rawFileName) ||
          key.endsWith(QDir::separator() + rawFileName) || key == rawFileName) {
        fullPath = key;
        break;
      }
    }
    if (fullPath.isEmpty() && (m_databaseManager != nullptr) &&
        (m_collections != nullptr)) {
      int ownerIndex =
          m_databaseManager->getCollectionIndexForFile(rawFileName);
      if (ownerIndex >= 0 && ownerIndex < m_collections->size()) {
        const QString mediaDir = (*m_collections)[ownerIndex].mediaDirectory;
        if (!mediaDir.trimmed().isEmpty()) {
          fullPath = QDir(mediaDir).absoluteFilePath(rawFileName);
        }
      }
    }
  }
  return fullPath;
}

void ScrollManager::updateCollectionIndexFromDatabase(const QString &fullPath,
                                                      int &collectionIndex) {
  if (m_databaseManager != nullptr) {
    int detectedCollectionIndex =
        m_databaseManager->getCollectionIndexForFile(fullPath);
    if (detectedCollectionIndex >= 0) {
      collectionIndex = detectedCollectionIndex;
    }
  }
}

void ScrollManager::setupMediaItemConnections(MediaItemWidget *itemWidget,
                                              int visualIndex,
                                              const QString &fullPath,
                                              int collectionIndex) {
  connect(itemWidget, &MediaItemWidget::clicked, this,
          [this, visualIndex, fullPath]() {
            if (auto *clickedWidget = m_activeWidgets.value(visualIndex)) {
              emit widgetClicked(clickedWidget, fullPath);
            }
          });

  int capturedCollectionIndex = collectionIndex;
  connect(itemWidget, &MediaItemWidget::doubleClicked, this,
          [this, fullPath, capturedCollectionIndex]() {
            emit widgetDoubleClickedWithCollection(fullPath,
                                                   capturedCollectionIndex);
          });
}

void ScrollManager::configureArtworkForWidget(MediaItemWidget *itemWidget,
                                              const QString &fullPath) {
  QString artworkDir = m_context.config.artworkDirectory;
  if ((m_databaseManager != nullptr) &&
      m_context.config.showAllSubcollectionItems) {
    QString foundArtworkDir =
        m_databaseManager->findArtworkDirectoryForFile(fullPath);
    if (!foundArtworkDir.isEmpty()) {
      artworkDir = foundArtworkDir;
    }
  }
  QString artworkPath = ArtworkManager::findArtworkForFile(
      QFileInfo(fullPath).fileName(), artworkDir);
  if (!artworkPath.isEmpty() && m_artworkManager) {
    m_artworkManager->addPendingArtwork(itemWidget, artworkPath);
  }
}

auto ScrollManager::getItemPosition(int visualIndex) const -> QPoint {
  bool centerSingleRow =
      m_isFiltered && m_totalItems > 0 && m_totalItems < m_metrics.itemsPerRow;
  int itemsPerRowForLayout =
      centerSingleRow ? m_totalItems : m_metrics.itemsPerRow;

  int rowIndex =
      (itemsPerRowForLayout > 0) ? visualIndex / itemsPerRowForLayout : 0;
  int columnIndex =
      (itemsPerRowForLayout > 0) ? visualIndex % itemsPerRowForLayout : 0;

  int xPos =
      m_metrics.margins +
      (columnIndex * (m_metrics.itemWidth + m_metrics.horizontalSpacing));
  int yPos = (rowIndex * (m_metrics.itemHeight + m_metrics.verticalSpacing));

  return {xPos, yPos};
}

// Rebuild filtered indices based on names; aggregated views use full-path keyed
// display names
// Rebuilds filtered indices based on current search filter
void ScrollManager::rebuildFilteredIndices() {
  m_filteredIndices.clear();
  QString needle = m_currentFilter.toLower();
  if (needle.isEmpty()) {
    return;
  }

  int subCount = m_subcollections.size();
  int totalOriginal = subCount + m_filePaths.size();

  for (int originalIndex = 0; originalIndex < totalOriginal; ++originalIndex) {
    bool match = false;
    if (originalIndex < subCount) {
      match = matchesSubcollectionFilter(originalIndex, needle);
    } else {
      int mediaIndex = originalIndex - subCount;
      match = matchesMediaItemFilter(mediaIndex, needle);
    }
    if (match) {
      m_filteredIndices.append(originalIndex);
    }
  }
}

auto ScrollManager::matchesSubcollectionFilter(int subcollectionIndex,
                                               const QString &needle) const
    -> bool {
  int actualSubcollectionIndex = m_subcollections[subcollectionIndex];
  QString subcollectionName =
      getSubcollectionName(actualSubcollectionIndex).toLower();
  return subcollectionName.contains(needle);
}

auto ScrollManager::matchesMediaItemFilter(int mediaIndex,
                                           const QString &needle) const
    -> bool {
  QString rawEntry = m_filePaths.value(mediaIndex);
  QString display = getDisplayNameForMediaItem(rawEntry);
  return display.toLower().contains(needle);
}

auto ScrollManager::getDisplayNameForMediaItem(const QString &rawEntry) const
    -> QString {
  if (m_context.config.showAllSubcollectionItems) {
    if (QDir::isAbsolutePath(rawEntry)) {
      QString display = m_filePathToDisplayName.value(rawEntry);
      if (display.isEmpty()) {
        display = QFileInfo(rawEntry).completeBaseName();
      }
      return display;
    }
    QString display = m_filePathToDisplayName.value(rawEntry);
    if (display.isEmpty()) {
      display = QFileInfo(rawEntry).completeBaseName();
    }
    return display;
  }
  const QString mediaDir = m_context.config.mediaDirectory.trimmed();
  if (mediaDir.isEmpty()) {
    return QFileInfo(rawEntry).completeBaseName();
  }
  QString fullPath = QDir(mediaDir).absoluteFilePath(rawEntry);
  return m_fileNames.value(fullPath, QFileInfo(rawEntry).completeBaseName());
}

// Handles scroll changes throttling artwork updates and arrow-centering
// suppression
void ScrollManager::onScrollChanged() {
  if (m_destroying) {
    return;
  }
  if (m_scrollTimer == nullptr) {
    return;
  }

  if ((m_mediaScrollArea != nullptr) &&
      m_mediaScrollArea->property(PropertyKeys::ProgrammaticScroll).toBool()) {
    handleProgrammaticScroll();
    return;
  }

  handleUserScroll();
  setupScrollSuppression();
  finalizeScrollChanges();
}

void ScrollManager::handleProgrammaticScroll() {
  notifyUserActivity();
  if (!m_scrollTimer->isActive()) {
    m_scrollTimer->start();
  }
}

void ScrollManager::handleUserScroll() {
  m_userScrollbarActive = true;
  if (m_userScrollIdleTimer != nullptr) {
    m_userScrollIdleTimer->trigger();
  }

  if (m_mediaScrollArea != nullptr) {
    m_mediaScrollArea->setProperty(PropertyKeys::UserScrollActive, true);

    if (QScrollBar *verticalScrollbar =
            m_mediaScrollArea->verticalScrollBar()) {
      if (auto *arrowKeyAnimation =
              verticalScrollbar->findChild<QPropertyAnimation *>(
                  "arrowKeyScrollAnim")) {
        if (arrowKeyAnimation->state() == QAbstractAnimation::Running) {
          arrowKeyAnimation->stop();
        }
      }
    }
  }
}

void ScrollManager::setupScrollSuppression() {
  if (m_mediaScrollArea == nullptr) {
    return;
  }

  m_mediaScrollArea->setProperty(PropertyKeys::SuppressArrowCenter, true);
  qint64 until = QDateTime::currentMSecsSinceEpoch() +
                 UIConstants::WHEEL_SUPPRESS_ARROW_CENTER_MS;
  m_mediaScrollArea->setProperty(PropertyKeys::SuppressArrowCenterUntilMs,
                                 until);

  QPointer<QScrollArea> scrollAreaPtr = m_mediaScrollArea;
  QTimer::singleShot(
      UIConstants::ARROW_CENTER_CLEAR_CHECK_DELAY_MS, this, [scrollAreaPtr]() {
        if (!scrollAreaPtr) {
          return;
        }
        qint64 suppressUntilMs =
            scrollAreaPtr->property(PropertyKeys::SuppressArrowCenterUntilMs)
                .toLongLong();
        if (suppressUntilMs > 0 &&
            QDateTime::currentMSecsSinceEpoch() < suppressUntilMs) {
          return;
        }
        scrollAreaPtr->setProperty(PropertyKeys::SuppressArrowCenter, false);
      });
}

void ScrollManager::finalizeScrollChanges() {
  QTimer::singleShot(
      UIConstants::USER_SCROLL_ACTIVE_CLEAR_DELAY_MS, this, [this]() {
        if (m_mediaScrollArea) {
          m_mediaScrollArea->setProperty(PropertyKeys::UserScrollActive, false);
        }
      });

  notifyUserActivity();
  if (!m_scrollTimer->isActive()) {
    m_scrollTimer->start();
  }
}

void ScrollManager::onThrottledUpdate() { updateVirtualView(); }

void ScrollManager::onSubcollectionDoubleClicked(int subcollectionIndex) {
  emit subcollectionEntered(subcollectionIndex);
}

// Returns the underlying path for a visual index; prefers precomputed mapping
// for aggregated views
auto ScrollManager::filePathForVisualIndex(int visualIndex) const -> QString {
  int actualIndex = getFilteredIndex(visualIndex);
  int subCount = m_subcollections.size();
  if (actualIndex < subCount) {
    return {};
  }
  int mediaIndex = actualIndex - subCount;
  if (mediaIndex < 0 || mediaIndex >= m_filePaths.size()) {
    return {};
  }

  const QString rawEntry = m_filePaths[mediaIndex];

  return resolveToFullPath(rawEntry);
}

void ScrollManager::calculateMovementDirection(int selectedIndex, int prevIndex,
                                               int itemsPerRow,
                                               bool &isHorizontalMove) {
  if (prevIndex < 0) {
    isHorizontalMove = false;
    return;
  }

  // Allow jumps > 1 if on the same row (for rapid click-hold advancing)
  int prevRow = GridUtils::computeItemRow(prevIndex, itemsPerRow);
  int currRow = GridUtils::computeItemRow(selectedIndex, itemsPerRow);
  
  if (prevRow == currRow) {
    isHorizontalMove = true;
    return;
  }

  int diff = std::abs(selectedIndex - prevIndex);
  if (diff != 1) {
    isHorizontalMove = false;
    return;
  }

  if (itemsPerRow <= 1) {
    isHorizontalMove = false;
    return;
  }

  int prevCol = prevIndex % itemsPerRow;
  if (prevCol < 0) {
    prevCol += itemsPerRow;
  }
  int currCol = selectedIndex % itemsPerRow;
  if (currCol < 0) {
    currCol += itemsPerRow;
  }

  bool wrappedForward = (prevCol == itemsPerRow - 1) && (currCol == 0);
  bool wrappedBackward = (prevCol == 0) && (currCol == itemsPerRow - 1);
  isHorizontalMove = wrappedForward || wrappedBackward;
}
