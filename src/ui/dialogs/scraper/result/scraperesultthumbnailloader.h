#ifndef SCRAPERESULTTHUMBNAILLOADER_H
#define SCRAPERESULTTHUMBNAILLOADER_H

// Decodes + smooth-scales freshly-scraped media off the UI thread and
// appends one icon-only row per completion to the scraper dialog's
// "Recent media" filmstrip. Each watcher is parented to this loader so a
// dialog close before the worker finishes drops the decoded bytes
// harmlessly. Reaches the host dialog's m_liveThumbsStrip through friend
// access.

#include <QObject>
#include <QString>

class ScrapeResultDialog;

class ScrapeResultThumbnailLoader : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ScrapeResultThumbnailLoader)

public:
  explicit ScrapeResultThumbnailLoader(ScrapeResultDialog *dlg);

  /// Schedule an async decode + 96×96 smooth-scale of @p path; on
  /// completion appends one row to the filmstrip, trims the strip back
  /// to its 12-row cap, and auto-scrolls to the newest entry. Non-image
  /// media (e.g. a scraped `.pdf` manual) is silently skipped.
  void appendThumbAsync(const QString &path);

private:
  ScrapeResultDialog *m_dlg = nullptr;
};

#endif // SCRAPERESULTTHUMBNAILLOADER_H
