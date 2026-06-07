#include "batchprogressview.h"

#include "durationformat.h"

#include <algorithm>

#include <QDateTime>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

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

  // Sub-status under the current item, e.g. "Hashing ROM…",
  // "Extracting archive…". Hidden when empty (the common case for
  // small ROMs that hash in milliseconds) so the layout doesn't
  // reserve vertical space for it (Kartend-ou0a).
  m_stageLabel = new QLabel(this);
  m_stageLabel->setWordWrap(true);
  QFont stageFont = m_stageLabel->font();
  stageFont.setItalic(true);
  m_stageLabel->setFont(stageFont);
  m_stageLabel->hide();
  layout->addWidget(m_stageLabel);

  // "Skip this item" — shown only while a stage is active (a single
  // large item is hashing/extracting and can make the modal feel hung).
  // Skipping abandons just that item; the batch keeps going.
  m_skipButton = new QPushButton(tr("Skip this item"), this);
  m_skipButton->setToolTip(
      tr("Stop scraping the current item and move on. The rest of the batch keeps going."));
  m_skipButton->hide();
  connect(m_skipButton, &QPushButton::clicked, this, [this]() {
    if (m_runner) m_runner->skipCurrentItem();
    // Disable until the next item's stage starts so a lingering click can't
    // read as "skip the next one too"; onStageChanged re-enables on show.
    m_skipButton->setEnabled(false);
  });
  auto *skipRow = new QHBoxLayout();
  skipRow->setContentsMargins(0, 0, 0, 0);
  skipRow->addWidget(m_skipButton);
  skipRow->addStretch(1);
  layout->addLayout(skipRow);

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
  connect(runner, &Scraper::BatchScrapeRunner::itemStageChanged, this,
          &BatchScrapeProgressView::onStageChanged);
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
            m_timingLabel->setText(
                tr("Elapsed %1 · ETA —").arg(DurationFormat::formatDurationMs(elapsedMs)));
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
  m_currentItemName = currentName;
  m_currentLabel->setText(currentName.isEmpty() ? tr("Scraping…")
                                                : tr("Now scraping: %1").arg(currentName));
  // Each new item starts with a fresh (empty) stage; clear the label
  // so a previous item's "Extracting archive…" doesn't linger.
  m_stageLabel->clear();
  m_stageLabel->hide();
  m_skipButton->hide();

  const qint64 elapsedMs = std::max<qint64>(1, QDateTime::currentMSecsSinceEpoch() - m_startMs);
  QString etaStr = QStringLiteral("—");
  if (done > 0 && total > done) {
    const qint64 etaMs =
        static_cast<qint64>((double(elapsedMs) / double(done)) * double(total - done));
    etaStr = DurationFormat::formatDurationMs(etaMs);
  }
  m_timingLabel->setText(
      tr("Elapsed %1 · ETA %2").arg(DurationFormat::formatDurationMs(elapsedMs), etaStr));
}

void BatchScrapeProgressView::onStageChanged(const QString &stage) {
  if (stage.isEmpty()) {
    m_stageLabel->clear();
    m_stageLabel->hide();
    m_skipButton->hide();
    return;
  }
  m_stageLabel->setText(stage);
  m_stageLabel->show();
  // A stage is running (hash/extraction) — offer the per-item skip, and
  // re-enable it in case a previous skip click left it disabled.
  m_skipButton->setEnabled(true);
  m_skipButton->show();
}
