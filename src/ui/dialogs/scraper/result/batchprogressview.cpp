#include "batchprogressview.h"

#include <algorithm>

#include <QDateTime>
#include <QFont>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>

namespace {

// Local clone of ScrapeResultDialog::formatDuration. Kept inline here
// during the Kartend-xvci view extraction so the new view stays
// self-contained — the host dialog still uses its own copy for the
// single-item + unified progress lines until those modes peel out
// too, at which point this helper can land in a shared header.
QString formatDuration(qint64 ms) {
  if (ms <= 0) return QStringLiteral("—");
  const qint64 totalSec = ms / 1000;
  const qint64 h = totalSec / 3600;
  const qint64 m = (totalSec / 60) % 60;
  const qint64 s = totalSec % 60;
  if (h > 0) {
    return QStringLiteral("%1:%2:%3")
        .arg(h)
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'));
  }
  if (m > 0) {
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
  }
  return BatchScrapeProgressView::tr("%1s").arg(s);
}

} // namespace

BatchScrapeProgressView::BatchScrapeProgressView(QWidget *parent) : QWidget(parent) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(20, 20, 20, 20);
  layout->setSpacing(12);

  m_headerLabel = new QLabel(this);
  QFont headerFont = m_headerLabel->font();
  headerFont.setPointSizeF(headerFont.pointSizeF() * 1.2);
  headerFont.setBold(true);
  m_headerLabel->setFont(headerFont);
  m_headerLabel->setWordWrap(true);
  layout->addWidget(m_headerLabel);

  m_currentLabel = new QLabel(this);
  m_currentLabel->setWordWrap(true);
  layout->addWidget(m_currentLabel);

  m_progressBar = new QProgressBar(this);
  m_progressBar->setRange(0, 100);
  m_progressBar->setValue(0);
  m_progressBar->setTextVisible(true);
  layout->addWidget(m_progressBar);

  m_timingLabel = new QLabel(this);
  m_timingLabel->setWordWrap(true);
  layout->addWidget(m_timingLabel);

  m_countsLabel = new QLabel(this);
  layout->addWidget(m_countsLabel);

  layout->addStretch(1);
}

void BatchScrapeProgressView::setRunner(Scraper::BatchScrapeRunner *runner,
                                        const QString &collectionName, int totalItems) {
  m_runner = runner;
  m_totalItems = std::max(1, totalItems);
  m_startMs = QDateTime::currentMSecsSinceEpoch();

  m_headerLabel->setText(tr("Batch scraping '%1'…").arg(collectionName));
  m_currentLabel->setText(tr("Preparing…"));
  m_progressBar->setRange(0, m_totalItems);
  m_progressBar->setValue(0);
  m_timingLabel->setText(tr("Elapsed 0s · ETA —"));
  m_countsLabel->setText(tr("Scraped 0  ·  Skipped 0  ·  Errors 0"));

  if (!runner) return;
  connect(runner, &Scraper::BatchScrapeRunner::progress, this,
          &BatchScrapeProgressView::onProgress);
  connect(runner, &Scraper::BatchScrapeRunner::finished, this,
          [this](const Scraper::BatchScrapeRunner::Summary &s) {
            m_summary = s;
            // Snap the bar to full on completion so the user sees a
            // finished state for a moment before the host dialog
            // closes.
            m_progressBar->setValue(m_totalItems);
            m_currentLabel->setText(tr("Finished."));
            const qint64 elapsedMs =
                std::max<qint64>(1, QDateTime::currentMSecsSinceEpoch() - m_startMs);
            m_timingLabel->setText(tr("Elapsed %1 · ETA —").arg(formatDuration(elapsedMs)));
            m_countsLabel->setText(tr("Scraped %1  ·  Skipped %2  ·  Errors %3")
                                       .arg(s.scraped)
                                       .arg(s.skipped)
                                       .arg(s.errors));
            emit finished(s);
          });
}

void BatchScrapeProgressView::onProgress(int done, int total, const QString &currentName) {
  if (total > 0 && total != m_progressBar->maximum()) {
    m_progressBar->setRange(0, total);
    m_totalItems = total;
  }
  m_progressBar->setValue(done);
  m_currentLabel->setText(currentName.isEmpty() ? tr("Scraping…")
                                                : tr("Now scraping: %1").arg(currentName));

  const qint64 elapsedMs = std::max<qint64>(1, QDateTime::currentMSecsSinceEpoch() - m_startMs);
  QString etaStr = QStringLiteral("—");
  if (done > 0 && total > done) {
    const qint64 etaMs =
        static_cast<qint64>((double(elapsedMs) / double(done)) * double(total - done));
    etaStr = formatDuration(etaMs);
  }
  m_timingLabel->setText(tr("Elapsed %1 · ETA %2").arg(formatDuration(elapsedMs), etaStr));
}
