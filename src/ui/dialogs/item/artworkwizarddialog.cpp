#include "artworkwizarddialog.h"

#include "extensionutils.h"
#include "imagedecodeutils.h"
#include "uiconstants/color.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QtConcurrent/QtConcurrentRun>
#include <QVBoxLayout>

namespace {

constexpr int kThumbSize = 96;

} // namespace

ArtworkWizardDialog::ArtworkWizardDialog(QWidget *parent) : QDialog(parent) {
  setupUi();
}

void ArtworkWizardDialog::setupUi() {
  setWindowTitle(tr("Artwork assignment wizard"));
  resize(720, 540);

  auto *outer = new QVBoxLayout(this);
  m_progressLabel = new QLabel(this);
  m_progressLabel->setStyleSheet(UIConstants::Color::MUTED_TEXT);
  outer->addWidget(m_progressLabel);

  m_headerLabel = new QLabel(this);
  m_headerLabel->setWordWrap(true);
  m_headerLabel->setStyleSheet("font-weight: bold;");
  outer->addWidget(m_headerLabel);

  m_pathLabel = new QLabel(this);
  m_pathLabel->setWordWrap(true);
  m_pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_pathLabel->setStyleSheet(UIConstants::Color::MUTED_ITALIC_TEXT);
  outer->addWidget(m_pathLabel);

  // Two-column body: candidate list left, larger preview right.
  auto *body = new QHBoxLayout();
  m_candidateList = new QListWidget(this);
  m_candidateList->setIconSize(QSize(kThumbSize, kThumbSize));
  m_candidateList->setMinimumWidth(280);
  m_candidateList->setSelectionMode(QAbstractItemView::SingleSelection);
  body->addWidget(m_candidateList, /*stretch=*/2);

  m_previewLabel = new QLabel(this);
  m_previewLabel->setAlignment(Qt::AlignCenter);
  m_previewLabel->setMinimumSize(320, 320);
  m_previewLabel->setStyleSheet("border: 1px solid palette(mid);");
  body->addWidget(m_previewLabel, /*stretch=*/3);

  outer->addLayout(body, /*stretch=*/1);

  m_emptyLabel = new QLabel(
      tr("No fuzzy-name matches in the artwork directory. Use Browse… to pick a file manually."),
      this);
  m_emptyLabel->setStyleSheet(UIConstants::Color::MUTED_ITALIC_TEXT);
  m_emptyLabel->setWordWrap(true);
  m_emptyLabel->setVisible(false);
  outer->addWidget(m_emptyLabel);

  auto *row = new QHBoxLayout();
  m_pickButton = new QPushButton(tr("Pick selected"), this);
  m_pickButton->setDefault(true);
  m_browseButton = new QPushButton(tr("Browse…"), this);
  m_skipButton = new QPushButton(tr("Skip"), this);
  m_closeButton = new QPushButton(tr("Close"), this);
  row->addWidget(m_pickButton);
  row->addWidget(m_browseButton);
  row->addWidget(m_skipButton);
  row->addStretch(1);
  row->addWidget(m_closeButton);
  outer->addLayout(row);

  connect(m_pickButton, &QPushButton::clicked, this, &ArtworkWizardDialog::onPickClicked);
  connect(m_browseButton, &QPushButton::clicked, this, &ArtworkWizardDialog::onBrowseClicked);
  connect(m_skipButton, &QPushButton::clicked, this, &ArtworkWizardDialog::onSkipClicked);
  connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
  connect(m_candidateList, &QListWidget::itemSelectionChanged, this,
          &ArtworkWizardDialog::onCandidateChanged);
}

void ArtworkWizardDialog::setQueue(QList<Entry> entries, CandidatesProvider candidatesFor,
                                   PickHandler onPick) {
  m_entries = std::move(entries);
  m_candidatesFor = std::move(candidatesFor);
  m_onPick = std::move(onPick);
  m_cursor = 0;
  renderCurrent();
}

void ArtworkWizardDialog::renderCurrent() {
  if (m_cursor < 0 || m_cursor >= m_entries.size()) {
    m_progressLabel->setText(tr("Queue complete — no more items."));
    m_headerLabel->clear();
    m_pathLabel->clear();
    m_candidateList->clear();
    m_previewLabel->clear();
    m_emptyLabel->setVisible(false);
    m_pickButton->setEnabled(false);
    m_browseButton->setEnabled(false);
    m_skipButton->setEnabled(false);
    return;
  }
  const auto &entry = m_entries.at(m_cursor);
  m_progressLabel->setText(tr("Assigning item %1 of %2").arg(m_cursor + 1).arg(m_entries.size()));
  m_headerLabel->setText(entry.itemName.toHtmlEscaped());
  m_pathLabel->setText(entry.filePath);
  m_candidateList->clear();
  // Invalidate any candidate-thumb decodes still in flight for the previous
  // item: their rows were just deleted by clear(), so a late result must not
  // touch them.
  const int listGen = ++m_listGeneration;

  QList<ArtworkCandidates::Candidate> candidates;
  if (m_candidatesFor) {
    candidates = m_candidatesFor(entry);
  }
  for (const auto &candidate : candidates) {
    auto *row = new QListWidgetItem(m_candidateList);
    const QString display = QFileInfo(candidate.path).fileName();
    row->setText(display);
    row->setData(Qt::UserRole, candidate.path);
    row->setToolTip(candidate.path);
    // Decode + scale the thumbnail off the GUI thread; the generation guard
    // drops a result that lands after the list was rebuilt (its row is gone).
    const QString thumbPath = candidate.path;
    QPointer<ArtworkWizardDialog> self = this;
    auto *watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [self, watcher, row, listGen]() {
      watcher->deleteLater();
      if (!self || self->m_listGeneration != listGen) return;
      const QImage img = watcher->result();
      if (img.isNull()) return;
      row->setIcon(QIcon(QPixmap::fromImage(img)));
    });
    watcher->setFuture(QtConcurrent::run([thumbPath]() -> QImage {
      if (!ExtensionUtils::isDecodableImagePath(thumbPath)) return {};
      const QImage img = ImageDecodeUtils::loadCapped(thumbPath);
      if (img.isNull()) return {};
      // Square thumbnail keeps the candidate list compact regardless of the
      // source aspect ratio; KeepAspectRatio so portraits aren't stretched.
      return img.scaled(kThumbSize, kThumbSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }));
  }
  m_emptyLabel->setVisible(m_candidateList->count() == 0);
  if (m_candidateList->count() > 0) {
    m_candidateList->setCurrentRow(0);
  } else {
    m_previewLabel->clear();
  }
  m_pickButton->setEnabled(m_candidateList->count() > 0);
  m_browseButton->setEnabled(true);
  m_skipButton->setEnabled(true);
}

void ArtworkWizardDialog::onCandidateChanged() {
  // Any selection change supersedes a pending preview decode so a slow load for
  // a row the user already navigated away from can't overwrite a newer preview.
  const int previewGen = ++m_previewGeneration;
  if (m_candidateList->count() == 0) {
    m_previewLabel->clear();
    return;
  }
  QListWidgetItem *item = m_candidateList->currentItem();
  if (!item) {
    m_previewLabel->clear();
    return;
  }
  const QString path = item->data(Qt::UserRole).toString();
  // Decode the larger preview off the GUI thread. No caching since selection
  // changes are user-driven and infrequent.
  if (!ExtensionUtils::isDecodableImagePath(path)) {
    m_previewLabel->setText(tr("(failed to load preview)"));
    return;
  }
  QPointer<ArtworkWizardDialog> self = this;
  auto *watcher = new QFutureWatcher<QImage>(this);
  connect(watcher, &QFutureWatcherBase::finished, this, [self, watcher, previewGen]() {
    watcher->deleteLater();
    if (!self || self->m_previewGeneration != previewGen) return;
    const QImage img = watcher->result();
    if (img.isNull()) {
      self->m_previewLabel->setText(ArtworkWizardDialog::tr("(failed to load preview)"));
      return;
    }
    self->m_previewLabel->setPixmap(QPixmap::fromImage(img).scaled(
        self->m_previewLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
  });
  watcher->setFuture(QtConcurrent::run([path]() { return ImageDecodeUtils::loadCapped(path); }));
}

void ArtworkWizardDialog::onPickClicked() {
  if (!m_onPick || m_cursor < 0 || m_cursor >= m_entries.size()) {
    return;
  }
  QListWidgetItem *item = m_candidateList->currentItem();
  if (!item) {
    return;
  }
  const QString chosenPath = item->data(Qt::UserRole).toString();
  if (chosenPath.isEmpty()) {
    return;
  }
  if (m_onPick(m_entries.at(m_cursor), chosenPath)) {
    advance();
  }
}

void ArtworkWizardDialog::onBrowseClicked() {
  if (!m_onPick || m_cursor < 0 || m_cursor >= m_entries.size()) {
    return;
  }
  const QString filter = tr("Images (*.png *.jpg *.jpeg *.webp *.gif *.bmp);;All files (*)");
  const QString chosen =
      QFileDialog::getOpenFileName(this, tr("Choose artwork file"), QString(), filter);
  if (chosen.isEmpty()) {
    return;
  }
  if (m_onPick(m_entries.at(m_cursor), chosen)) {
    advance();
  }
}

void ArtworkWizardDialog::onSkipClicked() {
  advance();
}

void ArtworkWizardDialog::advance() {
  m_cursor += 1;
  renderCurrent();
}
