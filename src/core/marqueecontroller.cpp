// MarqueeController — drives the secondary-monitor marquee / topper window.
// Extracted from MainWindow (mainwindow_wiring.cpp / mainwindow_setup.cpp seam).
#include "marqueecontroller.h"
#include "applicationcontext.h"
#include "artworkmanager.h"
#include "collection/collectionconfig.h"
#include "collection/typehelpers.h"
#include "collection/validationhelpers.h"
#include "extensionutils.h"
#include "interactionmanager.h"
#include "marqueewindow.h"
#include "pathutils.h"
#include "timerutils.h"
#include "videoutils.h"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QImage>
#include <QPixmap>
#include <QScreen>
#include <QString>
#include <QtConcurrent>

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

MarqueeController::~MarqueeController() {
  // Drop any in-flight off-thread decode first so a late result can't touch
  // the MarqueeWindow we're about to delete.
  cancelMarqueeLoad();
  // m_marqueeWindow is a parentless top-level widget this controller owns. At
  // teardown the event loop is already winding down, so the deleteLater() used
  // on the live disable/rescreen path would never run — delete it directly to
  // avoid an ASan/LSan leak on the full-teardown smoke path (Kartend-e3dg).
  // Safe: the disable path nulls m_marqueeWindow after its deleteLater(), so a
  // non-null pointer here never has a pending deletion (no double-free).
  delete m_marqueeWindow;
}

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
  if (!m_generalSettings || !m_generalSettings->marquee.marqueeEnabled) {
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
    // No window to push to once disabled — drop any in-flight decode and the
    // cached path so a later re-enable re-pushes from scratch.
    cancelMarqueeLoad();
    m_lastMarqueePath.clear();
    if (m_marqueeWindow) {
      m_marqueeWindow->close();
      m_marqueeWindow->deleteLater();
      m_marqueeWindow = nullptr;
    }
    return;
  }
  QScreen *target = resolveMarqueeScreen(m_generalSettings->marquee.marqueeScreenName);
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
  // Force a re-push: the window may have just been (re)created or re-pinned,
  // so the previously cached path no longer matches live window content.
  m_lastMarqueePath.clear();
  updateMarqueeArtwork();
}

void MarqueeController::updateMarqueeArtwork() {
  if (!m_marqueeWindow || !m_generalSettings || !m_generalSettings->marquee.marqueeEnabled) return;
  if (!m_currentCollectionIndex || !m_collections) return;

  const int collectionIndex = *m_currentCollectionIndex;

  // Mode 2 — video / attract loop. Source priority: per-item preview
  // video (resolved like the details-pane preview) → collection's
  // backgroundVideo. Empty path stops playback and clears the window.
  if (m_generalSettings->marquee.marqueeMode == 2) {
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
      const CollectionConfig &cfg = (*m_collections)[collectionIndex];
      videoPath = CollectionUtils::resolvedAssetPath(cfg.background.backgroundVideo, cfg.name);
    }
    // Switching to video clears any image we were showing; drop the cached
    // path + in-flight decode so a later return to image mode re-pushes even
    // if the selection is unchanged.
    cancelMarqueeLoad();
    m_lastMarqueePath.clear();
    m_marqueeWindow->setVideo(videoPath);
    return;
  }

  QString artworkPath;
  if (m_generalSettings->marquee.marqueeMode == 0) {
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
    artworkPath = CollectionUtils::resolvedCollectionIcon((*m_collections)[collectionIndex]);
  }

  if (artworkPath.isEmpty()) {
    cancelMarqueeLoad();
    m_lastMarqueePath.clear();
    m_marqueeWindow->setPixmap(
        QPixmap()); // Clear to background; user sees the topper still exists.
    return;
  }
  // Skip redundant work when the selection storm keeps resolving to the same
  // path — the marquee is already showing (or already decoding) it. Without
  // this every wheel tick re-decoded the same cover on the UI thread
  // (Kartend-cq8yh).
  if (artworkPath == m_lastMarqueePath) {
    return;
  }
  m_lastMarqueePath = artworkPath;
  startMarqueeLoad(artworkPath);
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

void MarqueeController::startMarqueeLoad(const QString &path) {
  // Decode off the UI thread (the point of the marquee-jank fix): a QImage is
  // built on a worker, then converted to QPixmap and pushed on the GUI thread
  // via the watcher's finished signal. A newer request supersedes any in-flight
  // decode so a fast scroll can't paint a stale cover.
  cancelMarqueeLoad();
  auto *watcher = new QFutureWatcher<QImage>(this);
  m_marqueeLoadWatcher = watcher;
  connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher]() {
    if (m_marqueeLoadWatcher == watcher) {
      m_marqueeLoadWatcher = nullptr;
    }
    const QImage img = watcher->result();
    watcher->deleteLater();
    // The window may have been torn down (marquee disabled) while the decode
    // ran — the disable path nulls it and cancels this watcher, but guard
    // anyway. A null QImage (decode failed / non-image) clears the window,
    // matching the previous synchronous behaviour.
    if (m_marqueeWindow) {
      m_marqueeWindow->setPixmap(QPixmap::fromImage(img));
    }
  });
  watcher->setFuture(QtConcurrent::run([path]() -> QImage {
    // Extension guard: never hand a non-image (e.g. a misconfigured .pdf icon)
    // to the decoder — Qt's PDF image plugin abort()s the process.
    if (!ExtensionUtils::isDecodableImagePath(path)) {
      return {};
    }
    return QImage(path);
  }));
}

void MarqueeController::cancelMarqueeLoad() {
  // Drop any in-flight decode so its late result can't overwrite a newer push
  // or touch a torn-down window. The QtConcurrent worker can't be cancelled,
  // but disconnecting + deleting the watcher discards its result.
  if (m_marqueeLoadWatcher) {
    m_marqueeLoadWatcher->disconnect(this);
    m_marqueeLoadWatcher->deleteLater();
    m_marqueeLoadWatcher = nullptr;
  }
}
