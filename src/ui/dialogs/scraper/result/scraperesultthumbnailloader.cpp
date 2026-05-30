#include "scraperesultthumbnailloader.h"

#include "scraperesultdialog.h"

#include "extensionutils.h"
#include "imagedecodeutils.h"

#include <QAbstractItemView>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QIcon>
#include <QImage>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPair>
#include <QPixmap>
#include <QtConcurrent/QtConcurrentRun>

ScrapeResultThumbnailLoader::ScrapeResultThumbnailLoader(ScrapeResultDialog *dlg)
    : QObject(dlg), m_dlg(dlg) {}

void ScrapeResultThumbnailLoader::appendThumbAsync(const QString &path) {
  // QImage decode + smooth-scale run on the global QThreadPool;
  // QPixmap conversion stays on the main thread (QPixmap is GUI-
  // thread-only). The watcher is parented to `this` so a dialog
  // close before the worker finishes lets it be cleaned up — the
  // queued `finished` lambda never fires and the decoded bytes
  // are dropped harmlessly. Each completion appends one row and
  // auto-scrolls; out-of-order completion across concurrent items
  // is fine since the strip just shows "most recently completed".
  //
  // ScraperService::itemScraped reports every media path it just
  // wrote — covers, screenshots, AND the manual `.pdf`. Feeding a
  // PDF to QImage routes through Qt's libqpdf imageformats plugin,
  // which is PDFium-backed and calls abort() on some inputs; the
  // SIGTRAP took down kartend at the 1681/1878 mark of a 3 h scrape
  // (Kartend-wquq). Gate on the same helper artworkloaddispatcher
  // already uses so non-image media silently skip the thumb strip.
  if (!ExtensionUtils::isDecodableImagePath(path)) {
    return;
  }
  auto *watcher = new QFutureWatcher<QPair<QString, QImage>>(this);
  connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher]() {
    watcher->deleteLater();
    if (!m_dlg->isVisible()) return;
    const auto pair = watcher->result();
    if (pair.second.isNull()) return;
    auto *row = new QListWidgetItem(QIcon(QPixmap::fromImage(pair.second)), QString(),
                                    m_dlg->m_liveThumbsStrip);
    row->setToolTip(QFileInfo(pair.first).fileName());
    while (m_dlg->m_liveThumbsStrip->count() > 12) {
      delete m_dlg->m_liveThumbsStrip->takeItem(0);
    }
    m_dlg->m_liveThumbsStrip->scrollToItem(row, QAbstractItemView::PositionAtBottom);
  });
  watcher->setFuture(QtConcurrent::run([path]() {
    QImage img = ImageDecodeUtils::loadCapped(path);
    if (img.isNull()) return qMakePair(path, QImage());
    return qMakePair(path, img.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  }));
}
