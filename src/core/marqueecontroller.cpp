// MarqueeController — drives the secondary-monitor marquee / topper window.
// Extracted from MainWindow (mainwindowwiring.cpp / mainwindowsetup.cpp seam).
#include "marqueecontroller.h"
#include "applicationcontext.h"
#include "artworkmanager.h"
#include "interactionmanager.h"
#include "marqueewindow.h"
#include "pathutils.h"
#include "timerutils.h"
#include "videoutils.h"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPixmap>
#include <QScreen>
#include <QString>

namespace {
// Trailing-edge debounce window for marquee-artwork refreshes triggered by
// selection storms (wheel / arrow-key holds). Matches the sidebar metadata
// debounce by intent, not by reference — same cost profile (path expand +
// FS video probe + QPixmap disk load), tuned to the same wheel-tick cadence.
// Kept local so a future tweak to either subsystem stays scoped.
constexpr int kMarqueeArtworkDebounceMs = 60;

/// Resolve a configured marquee screen name to a live QScreen pointer.
/// Empty / missing names return the primary screen so a never-configured
/// (or unplugged-since-config) target still works.
QScreen *resolveMarqueeScreen(const QString &screenName) {
  if (!screenName.isEmpty()) {
    for (QScreen *s : QGuiApplication::screens()) {
      if (s->name() == screenName) return s;
    }
    qWarning("Marquee: configured screen '%s' not found — falling back to primary",
             qPrintable(screenName));
  }
  return QGuiApplication::primaryScreen();
}
} // namespace

MarqueeController::MarqueeController(QObject *parent) : QObject(parent) {}

MarqueeController::~MarqueeController() = default;

void MarqueeController::setupReferences(const MarqueeControllerSetup &setup) {
  m_ctx = setup.ctx;
  m_generalSettings = setup.generalSettings;
  m_currentCollectionIndex = setup.currentCollectionIndex;
  m_collections = setup.collections;
  m_isShuttingDown = setup.isShuttingDown;

  // Coalesce marquee-artwork refreshes during selection storms. Trailing
  // edge — single clicks still feel instant. No-op for marquee-disabled users:
  // updateMarqueeArtwork's early return makes the debounced fire free.
  if (!m_marqueeDebouncer) {
    m_marqueeDebouncer = new TimerUtils::DebouncedTimer(kMarqueeArtworkDebounceMs, this);
    connect(m_marqueeDebouncer, &TimerUtils::DebouncedTimer::triggered, this, [this]() {
      if ((m_isShuttingDown && m_isShuttingDown()) || QApplication::closingDown()) {
        return;
      }
      updateMarqueeArtwork();
    });
  }
}

void MarqueeController::applyMarqueeSettings() {
  if (QApplication::closingDown()) return;
  if (!m_generalSettings || !m_generalSettings->marqueeEnabled) {
    // Disable path: tear the window down so the user reclaims the
    // secondary monitor. The pointer is set to nullptr (rather than
    // hide()) so the next enable starts from a clean state — handles
    // the case where the user changed the target screen as part of
    // the same Save action.
    if (m_marqueeDebouncer) {
      // Drop any in-flight selection-storm trigger so it can't fire after
      // teardown and run updateMarqueeArtwork's no-op early return for
      // nothing. Harmless either way; this just avoids the spurious wakeup.
      m_marqueeDebouncer->cancel();
    }
    if (m_marqueeWindow) {
      m_marqueeWindow->close();
      m_marqueeWindow->deleteLater();
      m_marqueeWindow = nullptr;
    }
    return;
  }
  QScreen *target = resolveMarqueeScreen(m_generalSettings->marqueeScreenName);
  if (!m_marqueeWindow) {
    m_marqueeWindow = new MarqueeWindow(target);
  } else {
    m_marqueeWindow->pinToScreen(target);
  }
  m_marqueeWindow->show();
  // Settings save / first enable — push immediately, not through the
  // debouncer, so the user sees the marquee populated as soon as they
  // hit Save (no 60ms blank flash).
  if (m_marqueeDebouncer) {
    m_marqueeDebouncer->cancel();
  }
  updateMarqueeArtwork();
}

void MarqueeController::updateMarqueeArtwork() {
  if (!m_marqueeWindow || !m_generalSettings || !m_generalSettings->marqueeEnabled) return;
  if (!m_currentCollectionIndex || !m_collections) return;

  const int collectionIndex = *m_currentCollectionIndex;

  // Mode 2 — video / attract loop. Source priority: per-item preview
  // video (resolved like the details-pane preview) → collection's
  // backgroundVideo. Empty path stops playback and clears the window.
  if (m_generalSettings->marqueeMode == 2) {
    if (!CollectionUtils::isValidIndex(collectionIndex, m_collections)) return;
    QString videoPath;
    if (auto *im = m_ctx ? m_ctx->interactionManager() : nullptr) {
      const QString filePath = im->selectedFilePath();
      if (!filePath.isEmpty()) {
        const CollectionConfig &cfg = (*m_collections)[collectionIndex];
        QString videoDir = PathUtils::validateAndExpandPath(cfg.videoDirectory, cfg.name);
        if (!videoDir.isEmpty()) {
          videoPath = VideoUtils::findVideoForFile(filePath, videoDir);
        }
        // Fall back to the artwork-tree's `video/` subdir, mirroring
        // the details-pane preview lookup so a single-root collection
        // still finds its videos.
        if (videoPath.isEmpty()) {
          const QString artDir = PathUtils::validateAndExpandPath(cfg.artworkDirectory, cfg.name);
          if (!artDir.isEmpty()) {
            videoPath = VideoUtils::findVideoForFile(filePath, QDir(artDir).filePath("video"));
          }
        }
      }
    }
    if (videoPath.isEmpty()) {
      videoPath = (*m_collections)[collectionIndex].backgroundVideo;
    }
    m_marqueeWindow->setVideo(videoPath);
    return;
  }

  QString artworkPath;
  if (m_generalSettings->marqueeMode == 0) {
    // Item Artwork mode — the cover of the currently-selected item.
    if (!CollectionUtils::isValidIndex(collectionIndex, m_collections)) return;
    auto *im = m_ctx ? m_ctx->interactionManager() : nullptr;
    if (!im) return;
    const QString filePath = im->selectedFilePath();
    if (filePath.isEmpty()) return;
    const CollectionConfig &cfg = (*m_collections)[collectionIndex];
    const QString artworkDir = PathUtils::validateAndExpandPath(cfg.artworkDirectory, cfg.name);
    artworkPath = ArtworkManager::findArtworkForFile(QFileInfo(filePath).fileName(), artworkDir);
  } else {
    // Collection Icon mode — the active collection's icon. Stable
    // even as the user scrolls so the marquee acts as a banner.
    if (!CollectionUtils::isValidIndex(collectionIndex, m_collections)) return;
    artworkPath = (*m_collections)[collectionIndex].collectionIcon;
  }

  if (artworkPath.isEmpty()) {
    m_marqueeWindow->setPixmap(
        QPixmap()); // Clear to background; user sees the topper still exists.
    return;
  }
  QPixmap pix(artworkPath);
  m_marqueeWindow->setPixmap(pix);
}

void MarqueeController::requestArtworkRefresh() {
  // Push the new selection's artwork to the marquee through the debouncer
  // so a wheel/arrow storm coalesces into a single trailing-edge refresh
  // — the per-tick cost (path expand + FS video probe + image-mode disk
  // load) is the same the sidebar used to pay before its own debounce.
  // Fallback to direct call if setup hasn't wired the debouncer yet
  // (rare: very early teardown / tests).
  if (m_marqueeDebouncer) {
    m_marqueeDebouncer->trigger();
  } else {
    updateMarqueeArtwork();
  }
}
