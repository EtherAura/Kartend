#ifndef SCRAPERESULTTHUMBNAILLOADER_H
#define SCRAPERESULTTHUMBNAILLOADER_H

// Decodes + smooth-scales freshly-scraped media off the UI thread and
// appends one icon-only row per completion to the scraper dialog's
// "Recent media" filmstrip. Each watcher is parented to this loader so a
// dialog close before the worker finishes drops the decoded bytes
// harmlessly.
//
// Kartend-kggn8: de-friended from ScrapeResultDialog. It is handed the
// filmstrip QListWidget (via setLiveThumbsStrip, once the host builds it) and a
// QWidget host for the visibility gate, instead of reaching through a
// back-pointer.

#include <QObject>
#include <QPointer>
#include <QString>

QT_BEGIN_NAMESPACE
class QListWidget;
class QWidget;
QT_END_NAMESPACE

class ScrapeResultThumbnailLoader : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ScrapeResultThumbnailLoader)

public:
  /// @p host gates the append on visibility and is the QObject parent. The
  /// filmstrip is supplied later via setLiveThumbsStrip() because the host
  /// builds it after constructing this loader.
  explicit ScrapeResultThumbnailLoader(QWidget *host);

  /// Install the filmstrip the decoded rows are appended to. Called by the host
  /// once the strip exists; until then appendThumbAsync() completions are no-ops.
  void setLiveThumbsStrip(QListWidget *strip);

  /// Schedule an async decode + 96×96 smooth-scale of @p path; on
  /// completion appends one row to the filmstrip, trims the strip back
  /// to its 12-row cap, and auto-scrolls to the newest entry. Non-image
  /// media (e.g. a scraped `.pdf` manual) is silently skipped.
  void appendThumbAsync(const QString &path);

private:
  QPointer<QWidget> m_host;
  QPointer<QListWidget> m_liveThumbsStrip;
};

#endif // SCRAPERESULTTHUMBNAILLOADER_H
