// Sibling translation unit for ArtworkManager.
// Extracted from artworkmanager.cpp during LOC-reduction refactor.
// These remain ArtworkManager members; this is a translation-unit split.
#include "applicationcontext.h"
#include "artworkmanager.h"
#include "artworkutils.h"
#include "cachemanager.h"
#include "collectionutils.h"
#include "extensionutils.h"
#include "interactionstateholder.h"
#include "propertyutils.h"
#include "setuputils.h"
#include "timerutils.h"
#include "ui/widgets/itemwidget.h"
#include "uiconstants.h"

#include <algorithm>
#include <functional>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QtConcurrent>
#include <QTextStream>
#include <QThread>
#include <QTimer>

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcArtworkManager)
#define debugLog(msg) qCDebug(lcArtworkManager) << msg

// Scales an image to fit within a square box.
// Accounts for device pixel ratio for crisp HiDPI rendering.
// Does NOT center on a square canvas - the caller handles centering.
static auto scaleCenterToBox(const QImage &img, int targetSize, qreal dpr = 1.0) -> QImage {
  if (img.isNull()) {
    return {};
  }
  // Scale to fit within actual pixel size (targetSize * dpr) for HiDPI
  // crispness
  const int actualSize = qRound(targetSize * dpr);
  QImage scaled = img.scaled(actualSize, actualSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  scaled.setDevicePixelRatio(dpr);
  return scaled;
}

void ArtworkManager::addPendingArtwork(ItemWidget *widget, const QString &artworkPath) {
  if (!widget || artworkPath.isEmpty()) {
    return;
  }
  if (!stackedWidget || stackedWidget->currentWidget() != itemsPage) {
    return;
  }

  trackWidget(widget);

  // Clear any stale state from previous widget use (e.g., after pool recycling)
  // This ensures the widget can receive new artwork even if it was previously
  // marked as loaded with different artwork
  {
    QMutexLocker locker(&m_dataMutex);
    const QString existingPath = widgetToArtworkPath.value(widget);

    // Skip if widget already has this exact artwork loaded or pending
    if (existingPath == artworkPath) {
      // Already loaded with same path - nothing to do
      if (loadedArtwork.contains(QPointer<ItemWidget>(widget))) {
        return;
      }
      // Already pending with same path - check if already in queue
      for (const auto &info : pendingArtwork) {
        if (info.mediaItem == widget && info.artworkPath == artworkPath) {
          return; // Already queued
        }
      }
    }

    if (!existingPath.isEmpty() && existingPath != artworkPath) {
      loadedArtwork.removeAll(widget);
      widgetToArtworkPath.remove(widget);
      // Remove any stale pending entries for this widget
      for (int i = pendingArtwork.size() - 1; i >= 0; --i) {
        if (pendingArtwork[i].mediaItem == widget) {
          pendingArtwork.removeAt(i);
        }
      }
    }
  }

  bool shouldDefer = false;
  if (m_state) {
    const bool deferAll = m_state->artwork().deferAllArtwork;
    const bool gliding = m_state->glideAnimating();
    const bool arrowScrolling = m_state->arrow().arrowKeyScrolling;
    const bool userScrolling = m_state->scroll().userScrollActive;
    const bool allowDuringSelection = m_state->artwork().allowDuringSelection;
    shouldDefer = (deferAll || gliding || arrowScrolling || userScrolling) && !allowDuringSelection;
  }

  // Always check cache first - even when deferring, cached artwork should be
  // applied immediately for responsive scrolling
  QPixmap cached = ArtworkManager::getCachedPixmap(artworkPath);
  if (!cached.isNull()) {
    widget->setArtworkPixmap(cached);
    {
      QMutexLocker locker(&m_dataMutex);
      widgetToArtworkPath[widget] = artworkPath;
      if (!loadedArtwork.contains(widget)) {
        loadedArtwork.append(widget);
      }
    }
    return;
  }

  if (shouldDefer) {
    QMutexLocker locker(&m_dataMutex);
    ArtworkInfo info = {.mediaItem = QPointer<ItemWidget>(widget), .artworkPath = artworkPath};
    pendingArtwork.append(info);
    return;
  }

  {
    QMutexLocker locker(&m_dataMutex);
    ArtworkInfo info = {.mediaItem = QPointer<ItemWidget>(widget), .artworkPath = artworkPath};
    pendingArtwork.append(info);
  }

  scheduleViewportUpdate();

  // Background precaching disabled - only load visible viewport items
  // to minimize CPU usage when idle
}

// Clears all pending artwork entries and loaded state for a widget being
// recycled back to the pool - prevents stale entries from blocking new artwork
void ArtworkManager::clearPendingArtworkForWidget(ItemWidget *widget) {
  if (!widget) {
    return;
  }
  QMutexLocker locker(&m_dataMutex);
  // Remove from loaded tracking so new artwork can be loaded
  loadedArtwork.removeAll(widget);
  widgetToArtworkPath.remove(widget);
  // Remove any pending entries for this widget
  for (int i = pendingArtwork.size() - 1; i >= 0; --i) {
    if (pendingArtwork[i].mediaItem == widget) {
      pendingArtwork.removeAt(i);
    }
  }
}

// Creates a centered, scaled artwork pixmap from an input pixmap
// Uses system DPR for HiDPI support
auto ArtworkManager::createProcessedArtwork(const QPixmap &originalPixmap) -> QPixmap {
  if (originalPixmap.isNull()) {
    return {};
  }
  // Get device pixel ratio from primary screen for HiDPI scaling
  qreal dpr = 1.0;
  if (QGuiApplication::primaryScreen()) {
    dpr = QGuiApplication::primaryScreen()->devicePixelRatio();
  }
  QImage centered = scaleCenterToBox(originalPixmap.toImage(), UIConstants::Artwork::BOX_SIZE, dpr);
  if (centered.isNull()) {
    return {};
  }
  QPixmap result = QPixmap::fromImage(centered);
  result.setDevicePixelRatio(dpr);
  return result;
}

auto ArtworkManager::getCachedPixmap(const QString &artworkPath) -> QPixmap {
  if (artworkPath.isEmpty() || !m_cacheManager) {
    return {};
  }
  // Avoid UI-thread disk I/O; disk cache is consulted from worker threads.
  return m_cacheManager->getArtworkFromMemoryOnly(artworkPath);
}

// Schedules a viewport artwork update via coordinator
