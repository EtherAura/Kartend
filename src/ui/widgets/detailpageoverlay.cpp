// Full-window detail page overlay for the selected item (Kartend-uve).
#include "detailpageoverlay.h"

#include <QApplication>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStringList>
#include <QVBoxLayout>

#include "itemmetadata.h"
#include "uiconstants.h"

namespace {
constexpr int kCardMargin = 48;
constexpr int kCardPadding = 32;
constexpr int kHeroSize = 360;
constexpr int kMetadataLabelColumn = 0;
constexpr int kMetadataValueColumn = 1;
constexpr QColor kOverlayColor = QColor(3, 5, 9, 230);
} // namespace

DetailPageOverlay::DetailPageOverlay(QWidget *parent) : QWidget(parent) {
  setupUI();
  setFocusPolicy(Qt::StrongFocus);
  if (parent) {
    parent->installEventFilter(this);
  }
  QWidget::hide();
}

DetailPageOverlay::~DetailPageOverlay() = default;

void DetailPageOverlay::setupUI() {
  setAutoFillBackground(false);

  // Card hosts everything inside a rounded frame so the overlay reads as a
  // distinct surface rather than just a tinted backdrop.
  m_card = new QWidget(this);
  m_card->setObjectName(QStringLiteral("detailPageCard"));
  m_card->setStyleSheet(QStringLiteral("#detailPageCard {"
                                       "  background-color: rgba(20, 23, 30, 245);"
                                       "  border: 1px solid rgba(255, 255, 255, 50);"
                                       "  border-radius: 16px;"
                                       "}"
                                       "QLabel { color: white; }"));

  auto *cardLayout = new QVBoxLayout(m_card);
  cardLayout->setContentsMargins(kCardPadding, kCardPadding, kCardPadding, kCardPadding);
  cardLayout->setSpacing(16);

  // ─── Top bar (close button) ────────────────────────────────────────────
  auto *topBar = new QHBoxLayout();
  topBar->setSpacing(8);
  topBar->addStretch(1);
  m_closeButton = new QPushButton(tr("✕"), m_card);
  m_closeButton->setFixedSize(32, 32);
  m_closeButton->setCursor(Qt::PointingHandCursor);
  m_closeButton->setFocusPolicy(Qt::NoFocus);
  m_closeButton->setStyleSheet(
      QStringLiteral("QPushButton { background-color: rgba(255, 255, 255, 25);"
                     " color: white; border: 1px solid rgba(255, 255, 255, 60);"
                     " border-radius: 16px; font-weight: bold; font-size: 14px; }"
                     "QPushButton:hover { background-color: rgba(255, 255, 255, 60); }"));
  connect(m_closeButton, &QPushButton::clicked, this, &DetailPageOverlay::hideOverlay);
  topBar->addWidget(m_closeButton);
  cardLayout->addLayout(topBar);

  // ─── Scroll area wrapping the body so very-long descriptions / many
  // custom fields don't get cropped on small windows. ─────────────────────
  m_scrollArea = new QScrollArea(m_card);
  m_scrollArea->setWidgetResizable(true);
  m_scrollArea->setFrameShape(QFrame::NoFrame);
  m_scrollArea->setStyleSheet(
      QStringLiteral("QScrollArea { background: transparent; }"
                     "QScrollArea > QWidget > QWidget { background: transparent; }"));
  m_scrollContent = new QWidget(m_scrollArea);
  auto *contentLayout = new QVBoxLayout(m_scrollContent);
  contentLayout->setContentsMargins(0, 0, 0, 0);
  contentLayout->setSpacing(20);
  m_scrollArea->setWidget(m_scrollContent);
  cardLayout->addWidget(m_scrollArea, 1);

  // ─── Hero row: artwork on the left, title + description on the right ──
  auto *hero = new QHBoxLayout();
  hero->setSpacing(24);
  hero->setAlignment(Qt::AlignTop);

  // Artwork stack with prev/next navigators and a type caption.
  auto *artworkColumn = new QVBoxLayout();
  artworkColumn->setSpacing(8);
  artworkColumn->setAlignment(Qt::AlignTop);

  auto *artworkRow = new QHBoxLayout();
  artworkRow->setSpacing(6);
  artworkRow->setAlignment(Qt::AlignCenter);

  m_heroPrevButton = new QPushButton(tr("‹"), m_scrollContent);
  m_heroPrevButton->setFixedSize(28, 56);
  m_heroPrevButton->setCursor(Qt::PointingHandCursor);
  m_heroPrevButton->setFocusPolicy(Qt::NoFocus);
  m_heroPrevButton->setStyleSheet(
      QStringLiteral("QPushButton { background-color: rgba(255, 255, 255, 20);"
                     " color: white; border: 1px solid rgba(255, 255, 255, 60);"
                     " border-radius: 4px; font-size: 22px; }"
                     "QPushButton:hover { background-color: rgba(255, 255, 255, 60); }"
                     "QPushButton:disabled { color: rgba(255, 255, 255, 60); }"));
  connect(m_heroPrevButton, &QPushButton::clicked, this, [this]() { cycleArtwork(-1); });
  artworkRow->addWidget(m_heroPrevButton);

  m_heroLabel = new QLabel(m_scrollContent);
  m_heroLabel->setFixedSize(kHeroSize, kHeroSize);
  m_heroLabel->setAlignment(Qt::AlignCenter);
  m_heroLabel->setStyleSheet(QStringLiteral("QLabel { background-color: rgba(0, 0, 0, 80);"
                                            " border: 1px solid rgba(255, 255, 255, 50);"
                                            " border-radius: 8px; }"));
  artworkRow->addWidget(m_heroLabel);

  m_heroNextButton = new QPushButton(tr("›"), m_scrollContent);
  m_heroNextButton->setFixedSize(28, 56);
  m_heroNextButton->setCursor(Qt::PointingHandCursor);
  m_heroNextButton->setFocusPolicy(Qt::NoFocus);
  m_heroNextButton->setStyleSheet(m_heroPrevButton->styleSheet());
  connect(m_heroNextButton, &QPushButton::clicked, this, [this]() { cycleArtwork(1); });
  artworkRow->addWidget(m_heroNextButton);

  artworkColumn->addLayout(artworkRow);

  m_heroTypeLabel = new QLabel(m_scrollContent);
  m_heroTypeLabel->setAlignment(Qt::AlignCenter);
  m_heroTypeLabel->setStyleSheet(
      QStringLiteral("QLabel { color: rgba(255, 255, 255, 180); font-size: 12px; }"));
  artworkColumn->addWidget(m_heroTypeLabel);

  hero->addLayout(artworkColumn, 0);

  // Right-hand text column: title, subtitle (file stem when title overrides),
  // description, manual button.
  auto *textColumn = new QVBoxLayout();
  textColumn->setSpacing(12);
  textColumn->setAlignment(Qt::AlignTop);

  m_titleLabel = new QLabel(m_scrollContent);
  m_titleLabel->setWordWrap(true);
  m_titleLabel->setStyleSheet(
      QStringLiteral("QLabel { font-size: 28px; font-weight: 700; letter-spacing: 0.3px; }"));
  m_titleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  textColumn->addWidget(m_titleLabel);

  m_subtitleLabel = new QLabel(m_scrollContent);
  m_subtitleLabel->setWordWrap(true);
  m_subtitleLabel->setStyleSheet(
      QStringLiteral("QLabel { color: rgba(255, 255, 255, 160); font-size: 14px; }"));
  m_subtitleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_subtitleLabel->hide();
  textColumn->addWidget(m_subtitleLabel);

  m_descriptionLabel = new QLabel(m_scrollContent);
  m_descriptionLabel->setWordWrap(true);
  m_descriptionLabel->setAlignment(Qt::AlignTop);
  m_descriptionLabel->setStyleSheet(
      QStringLiteral("QLabel { color: rgba(255, 255, 255, 220); font-size: 14px;"
                     " line-height: 1.4; }"));
  m_descriptionLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  textColumn->addWidget(m_descriptionLabel);

  m_manualButton = new QPushButton(tr("Open Manual"), m_scrollContent);
  m_manualButton->setCursor(Qt::PointingHandCursor);
  m_manualButton->setFocusPolicy(Qt::NoFocus);
  m_manualButton->setStyleSheet(
      QStringLiteral("QPushButton { background-color: rgba(255, 255, 255, 30);"
                     " color: white; border: 1px solid rgba(255, 255, 255, 60);"
                     " border-radius: 6px; padding: 6px 14px; font-size: 13px; }"
                     "QPushButton:hover { background-color: rgba(255, 255, 255, 70); }"));
  m_manualButton->hide();
  connect(m_manualButton, &QPushButton::clicked, this, [this]() {
    if (!m_payload.manualPath.isEmpty()) {
      emit manualRequested(m_payload.manualPath);
    }
  });
  auto *manualRow = new QHBoxLayout();
  manualRow->setContentsMargins(0, 0, 0, 0);
  manualRow->addWidget(m_manualButton);
  manualRow->addStretch(1);
  textColumn->addLayout(manualRow);

  textColumn->addStretch(1);

  hero->addLayout(textColumn, 1);
  contentLayout->addLayout(hero);

  // ─── Metadata grid (built lazily) ──────────────────────────────────────
  m_metadataContainer = new QWidget(m_scrollContent);
  m_metadataGrid = new QGridLayout(m_metadataContainer);
  m_metadataGrid->setContentsMargins(0, 0, 0, 0);
  m_metadataGrid->setHorizontalSpacing(16);
  m_metadataGrid->setVerticalSpacing(8);
  m_metadataGrid->setColumnStretch(kMetadataLabelColumn, 0);
  m_metadataGrid->setColumnStretch(kMetadataValueColumn, 1);
  contentLayout->addWidget(m_metadataContainer);

  contentLayout->addStretch(1);
}

void DetailPageOverlay::showWith(const Payload &payload) {
  m_payload = payload;
  m_currentArtworkIndex = 0;
  m_active = true;

  // Title falls back to filename when no scraped title is set.
  const QString displayTitle = !payload.itemName.trimmed().isEmpty()
                                   ? payload.itemName
                                   : QFileInfo(payload.filePath).completeBaseName();
  m_titleLabel->setText(displayTitle);

  // Subtitle shows the file stem when it differs from the displayed title
  // (i.e. the metadata title overrode the filename) so the user can still
  // see the original ROM/file name.
  const QString fileStem = QFileInfo(payload.filePath).completeBaseName();
  if (!fileStem.isEmpty() && fileStem != displayTitle) {
    m_subtitleLabel->setText(fileStem);
    m_subtitleLabel->show();
  } else {
    m_subtitleLabel->clear();
    m_subtitleLabel->hide();
  }

  m_descriptionLabel->setText(payload.metadata.description);
  m_descriptionLabel->setVisible(!payload.metadata.description.trimmed().isEmpty());

  m_manualButton->setVisible(!payload.manualPath.isEmpty());
  m_manualButton->setToolTip(payload.manualPath);

  rebuildMetadataGrid(payload);
  renderHeroArtwork();

  if (parentWidget()) {
    updatePosition();
  }
  QWidget::show();
  raise();
  setFocus(Qt::OtherFocusReason);
  emit visibilityChanged(true);
}

void DetailPageOverlay::hideOverlay() {
  if (!m_active && !isVisible()) {
    return;
  }
  m_active = false;
  QWidget::hide();
  emit visibilityChanged(false);
}

void DetailPageOverlay::renderHeroArtwork() {
  const int total = m_payload.artwork.size();
  if (total == 0) {
    m_heroLabel->clear();
    m_heroLabel->setText(tr("No artwork"));
    m_heroLabel->setStyleSheet(QStringLiteral("QLabel { background-color: rgba(0, 0, 0, 80);"
                                              " border: 1px solid rgba(255, 255, 255, 50);"
                                              " border-radius: 8px;"
                                              " color: rgba(255, 255, 255, 140);"
                                              " font-size: 14px; }"));
    m_heroTypeLabel->setText(QString());
    m_heroPrevButton->setEnabled(false);
    m_heroNextButton->setEnabled(false);
    return;
  }

  if (m_currentArtworkIndex < 0 || m_currentArtworkIndex >= total) {
    m_currentArtworkIndex = 0;
  }
  const auto &entry = m_payload.artwork[m_currentArtworkIndex];

  QPixmap pixmap(entry.path);
  if (pixmap.isNull()) {
    m_heroLabel->setText(tr("Unable to load %1").arg(entry.label));
  } else {
    QPixmap scaled =
        pixmap.scaled(kHeroSize, kHeroSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_heroLabel->setPixmap(scaled);
  }
  // Restore the styled-frame look every render in case the no-artwork branch
  // above replaced the stylesheet on a prior call.
  m_heroLabel->setStyleSheet(QStringLiteral("QLabel { background-color: rgba(0, 0, 0, 80);"
                                            " border: 1px solid rgba(255, 255, 255, 50);"
                                            " border-radius: 8px; }"));

  m_heroTypeLabel->setText(
      QStringLiteral("%1 (%2/%3)").arg(entry.label).arg(m_currentArtworkIndex + 1).arg(total));

  // Single-entry case: still allow the buttons but they're no-ops; disabling
  // them communicates "nothing to cycle" without removing the visual symmetry.
  const bool canCycle = total > 1;
  m_heroPrevButton->setEnabled(canCycle);
  m_heroNextButton->setEnabled(canCycle);
}

void DetailPageOverlay::cycleArtwork(int direction) {
  const int total = m_payload.artwork.size();
  if (total <= 1) {
    return;
  }
  m_currentArtworkIndex = (m_currentArtworkIndex + direction + total) % total;
  renderHeroArtwork();
}

void DetailPageOverlay::clearMetadataGrid() {
  if (!m_metadataGrid) {
    return;
  }
  while (QLayoutItem *child = m_metadataGrid->takeAt(0)) {
    if (QWidget *w = child->widget()) {
      w->deleteLater();
    }
    delete child;
  }
}

void DetailPageOverlay::appendMetadataRow(const QString &label, const QString &value, bool wrap) {
  if (!m_metadataGrid || value.trimmed().isEmpty()) {
    return;
  }
  const int row = m_metadataGrid->rowCount();
  auto *labelWidget = new QLabel(label + QStringLiteral(":"), m_metadataContainer);
  QFont labelFont = labelWidget->font();
  labelFont.setBold(true);
  labelWidget->setFont(labelFont);
  labelWidget->setStyleSheet(
      QStringLiteral("QLabel { color: rgba(255, 255, 255, 200); font-size: 13px; }"));
  labelWidget->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  m_metadataGrid->addWidget(labelWidget, row, kMetadataLabelColumn);

  auto *valueWidget = new QLabel(value, m_metadataContainer);
  valueWidget->setStyleSheet(
      QStringLiteral("QLabel { color: rgba(255, 255, 255, 230); font-size: 13px; }"));
  valueWidget->setWordWrap(wrap);
  valueWidget->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_metadataGrid->addWidget(valueWidget, row, kMetadataValueColumn);
}

void DetailPageOverlay::rebuildMetadataGrid(const Payload &payload) {
  clearMetadataGrid();

  const auto &md = payload.metadata;
  appendMetadataRow(tr("Genre"), md.genre);
  appendMetadataRow(tr("Developer"), md.developer);
  appendMetadataRow(tr("Publisher"), md.publisher);
  appendMetadataRow(tr("Release date"), md.releaseDate);
  appendMetadataRow(tr("Rating"), md.contentRating);
  appendMetadataRow(tr("Players"), md.players);
  appendMetadataRow(tr("Runtime"), formatRuntime(md.runtimeSeconds));
  appendMetadataRow(tr("Tags"), formatTags(md.tags), /*wrap=*/true);

  // Custom fields (Kartend-hpln). Alphabetically ordered by parseCustomFields.
  const auto customFields = ItemMetadataStore::parseCustomFields(md.customFields);
  for (const auto &pair : customFields) {
    appendMetadataRow(pair.first, pair.second, /*wrap=*/true);
  }

  // File information block — kept after the metadata so the user-visible
  // fields take priority. Always rendered (file path / size / extension are
  // always known once an item is selected).
  appendMetadataRow(tr("File"), payload.filePath, /*wrap=*/true);
  if (payload.fileSize >= 0) {
    appendMetadataRow(tr("Size"), formatFileSize(payload.fileSize));
  }
  appendMetadataRow(tr("Modified"), payload.fileModified);
  appendMetadataRow(tr("Extension"), payload.fileExtension);

  // Usage statistics (Kartend-7vi). Each row is skipped when empty so an
  // un-launched item shows nothing here.
  if (payload.usage.playCount > 0) {
    appendMetadataRow(tr("Play count"), QString::number(payload.usage.playCount));
  }
  if (!payload.usage.lastPlayed.isEmpty()) {
    appendMetadataRow(tr("Last played"),
                      UsageStatsStore::formatTimestamp(payload.usage.lastPlayed));
  }
  if (payload.usage.totalPlaySeconds > 0) {
    appendMetadataRow(tr("Time played"),
                      UsageStatsStore::formatDuration(payload.usage.totalPlaySeconds));
  }
}

QString DetailPageOverlay::formatFileSize(qint64 bytes) {
  const qint64 kb = UIConstants::Metadata::FILE_SIZE_KB;
  const qint64 mb = kb * UIConstants::Metadata::FILE_SIZE_KB;
  const qint64 gb = mb * UIConstants::Metadata::FILE_SIZE_KB;
  if (bytes >= gb) {
    return QString::number(bytes / static_cast<double>(gb), 'f', 2) + QStringLiteral(" GB");
  }
  if (bytes >= mb) {
    return QString::number(bytes / static_cast<double>(mb), 'f', 2) + QStringLiteral(" MB");
  }
  if (bytes >= kb) {
    return QString::number(bytes / static_cast<double>(kb), 'f', 2) + QStringLiteral(" KB");
  }
  return QString::number(bytes) + QStringLiteral(" bytes");
}

QString DetailPageOverlay::formatRuntime(int seconds) {
  if (seconds < 0) {
    return {};
  }
  const int hours = seconds / 3600;
  const int minutes = (seconds % 3600) / 60;
  const int secs = seconds % 60;
  if (hours > 0) {
    return QStringLiteral("%1h %2m").arg(hours).arg(minutes, 2, 10, QChar('0'));
  }
  if (minutes > 0) {
    return QStringLiteral("%1m %2s").arg(minutes).arg(secs, 2, 10, QChar('0'));
  }
  return QStringLiteral("%1s").arg(secs);
}

QString DetailPageOverlay::formatTags(const QString &raw) {
  // Mirrors MetadataSidebar::formatTags so the detail page renders the same
  // values: tolerate either a JSON array or a comma-separated string.
  QString trimmed = raw.trimmed();
  if (trimmed.startsWith('[') && trimmed.endsWith(']')) {
    trimmed.chop(1);
    trimmed.remove(0, 1);
    trimmed.replace('"', QString());
  }
  QStringList parts = trimmed.split(',', Qt::SkipEmptyParts);
  for (QString &p : parts) {
    p = p.trimmed();
  }
  parts.removeAll(QString());
  return parts.join(QStringLiteral(", "));
}

void DetailPageOverlay::paintEvent(QPaintEvent *event) {
  Q_UNUSED(event)
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.fillRect(rect(), kOverlayColor);
}

void DetailPageOverlay::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  updatePosition();
}

void DetailPageOverlay::updatePosition() {
  if (!parentWidget()) {
    return;
  }
  setGeometry(parentWidget()->rect());
  if (!m_card) {
    return;
  }
  const int margin = kCardMargin;
  m_card->setGeometry(margin, margin, qMax(0, width() - margin * 2),
                      qMax(0, height() - margin * 2));
}

void DetailPageOverlay::keyPressEvent(QKeyEvent *event) {
  if (!event) {
    QWidget::keyPressEvent(event);
    return;
  }
  switch (event->key()) {
  case Qt::Key_Escape:
    hideOverlay();
    event->accept();
    return;
  case Qt::Key_Left:
    cycleArtwork(-1);
    event->accept();
    return;
  case Qt::Key_Right:
    cycleArtwork(1);
    event->accept();
    return;
  default:
    break;
  }
  QWidget::keyPressEvent(event);
}

void DetailPageOverlay::mousePressEvent(QMouseEvent *event) {
  // Click outside the card dismisses the overlay — same affordance as the
  // artwork preview overlay (Kartend-un3l).
  if (m_card && !m_card->geometry().contains(event->pos())) {
    hideOverlay();
    event->accept();
    return;
  }
  QWidget::mousePressEvent(event);
}

bool DetailPageOverlay::eventFilter(QObject *watched, QEvent *event) {
  if (watched == parentWidget() && event && event->type() == QEvent::Resize) {
    updatePosition();
  }
  return QWidget::eventFilter(watched, event);
}
