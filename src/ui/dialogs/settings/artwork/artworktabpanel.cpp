#include "artworktabpanel.h"

#include "collection/collectionconfig.h"
#include "itemwidget.h"
#include "pathstatusglyph.h"
#include "pathutils.h"
#include "placeholderwarmer.h"
#include "settingsformbinding.h"
#include "settingsmodel.h"
#include "ui_artworktabpanel.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QCursor>
#include <QFileDialog>
#include <QFrame>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPalette>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QStringList>
#include <QtConcurrent>
#include <QVBoxLayout>

ArtworkTabPanel::ArtworkTabPanel(QWidget *parent) : QWidget(parent), ui(new Ui::ArtworkTabPanel) {
  ui->setupUi(this);

  connect(ui->browseArtworkDirButton, &QPushButton::clicked, this,
          &ArtworkTabPanel::onBrowseArtworkDir);
  connect(ui->browsePlaceholderArtworkButton, &QPushButton::clicked, this,
          &ArtworkTabPanel::onBrowsePlaceholderArtwork);

  for (auto *edit :
       {ui->artworkDirLineEdit, ui->placeholderArtworkLineEdit, ui->customArtworkTypesLineEdit}) {
    connect(edit, &QLineEdit::textChanged, this, [this](const QString &) { emit changed(); });
  }
  // hideMissingArtwork was a Layout-tab toggle; moved here because the
  // setting drives which media items actually surface in the items
  // page (an artwork-completeness filter, not a layout knob).
  connect(ui->hideMissingArtworkCheckBox, &QCheckBox::toggled, this,
          [this](bool) { emit changed(); });

  // Insert the warmer button just above the bottom spacer so it sits below
  // the Customization group without disturbing Designer-managed widgets.
  // Uses a separator + right-aligned row to read as a deliberate action,
  // not another form field.
  if (auto *root = qobject_cast<QVBoxLayout *>(layout())) {
    auto *separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);

    auto *row = new QHBoxLayout();
    row->addStretch();
    m_exportPlaceholdersButton =
        new QPushButton(tr("Export placeholder PNGs for missing covers…"), this);
    m_exportPlaceholdersButton->setToolTip(
        tr("For every item in this collection that has no cover image, write a "
           "procedural placeholder PNG into the Artwork folder. The files are "
           "harmless to delete or replace later. Settings on this tab are read "
           "from the live form (no Apply needed); other tabs use their saved "
           "values."));
    connect(m_exportPlaceholdersButton, &QPushButton::clicked, this,
            &ArtworkTabPanel::onExportPlaceholderPngs);
    row->addWidget(m_exportPlaceholdersButton);

    // The bottom spacer sits at index count()-1; insert before it so the
    // spacer continues to absorb leftover vertical space.
    const int insertAt = std::max(0, root->count() - 1);
    root->insertWidget(insertAt, separator);
    root->insertLayout(insertAt + 1, row);
  }
}

ArtworkTabPanel::~ArtworkTabPanel() {
  delete ui;
}

void ArtworkTabPanel::setModel(SettingsModel *model) {
  m_model = model;
}

void ArtworkTabPanel::load() {
  if (!m_model || !m_model->workingCollections || !m_model->currentIndex ||
      *m_model->currentIndex < 0 || *m_model->currentIndex >= m_model->workingCollections->size()) {
    return;
  }
  const CollectionConfig &config = (*m_model->workingCollections)[*m_model->currentIndex];
  SettingsFormBinding::loadInto(ui->artworkDirLineEdit, config.artworkDirectory);
  SettingsFormBinding::loadInto(ui->placeholderArtworkLineEdit, config.placeholderArtwork);
  SettingsFormBinding::loadInto(ui->customArtworkTypesLineEdit,
                                config.customArtworkTypes.join(", "));
  SettingsFormBinding::loadInto(ui->hideMissingArtworkCheckBox, config.hideMissingArtwork);
  // Kartend-liye: warning glyph for paths that don't resolve on this host.
  PathStatusGlyph::install(ui->artworkDirLineEdit, &PathUtils::checkDirectoryPath);
  PathStatusGlyph::install(ui->placeholderArtworkLineEdit, &PathUtils::checkFilePath);
}

void ArtworkTabPanel::clear() {
  ui->artworkDirLineEdit->clear();
  ui->placeholderArtworkLineEdit->clear();
  ui->customArtworkTypesLineEdit->clear();
  ui->hideMissingArtworkCheckBox->setChecked(false);
}

void ArtworkTabPanel::save() const {
  if (!m_model || !m_model->workingCollections || !m_model->currentIndex ||
      *m_model->currentIndex < 0 || *m_model->currentIndex >= m_model->workingCollections->size()) {
    return;
  }
  CollectionConfig &config = (*m_model->workingCollections)[*m_model->currentIndex];
  // Asset directory paths preserve the user's exact text (no trim) — matches
  // the prior extractUIFieldValues behavior for these fields. videoDirectory
  // and manualDirectory are no longer surfaced here (the scraper auto-routes
  // videos and manuals under {artworkDirectory}/video|manual/); pre-existing
  // INI values survive untouched because save() doesn't overwrite them.
  config.artworkDirectory = ui->artworkDirLineEdit->text();
  config.placeholderArtwork = ui->placeholderArtworkLineEdit->text();

  config.customArtworkTypes = parseCustomArtworkTypes();
  config.hideMissingArtwork = ui->hideMissingArtworkCheckBox->isChecked();
}

bool ArtworkTabPanel::hasChanges() const {
  if (!m_model || !m_model->originalCollection) return false;
  const CollectionConfig &o = *m_model->originalCollection;
  if (ui->artworkDirLineEdit->text() != o.artworkDirectory) return true;
  if (ui->placeholderArtworkLineEdit->text() != o.placeholderArtwork) return true;
  // Compare against the parsed-and-deduped form so a stylistic comma-spacing
  // tweak ("a, b" vs "a,b") doesn't register as dirty.
  if (parseCustomArtworkTypes() != o.customArtworkTypes) return true;
  if (ui->hideMissingArtworkCheckBox->isChecked() != o.hideMissingArtwork) return true;
  return false;
}

QStringList ArtworkTabPanel::parseCustomArtworkTypes() const {
  // Custom artwork types are comma-separated; parse, trim each entry, drop
  // empties + duplicates. Mirrors the legacy parser inline in
  // extractUIFieldValues.
  QStringList parsed = ui->customArtworkTypesLineEdit->text().split(',', Qt::SkipEmptyParts);
  QStringList cleaned;
  cleaned.reserve(parsed.size());
  for (QString &type : parsed) {
    type = type.trimmed();
    if (!type.isEmpty() && !cleaned.contains(type)) {
      cleaned.append(type);
    }
  }
  return cleaned;
}

void ArtworkTabPanel::onBrowseArtworkDir() {
  const QString dir = QFileDialog::getExistingDirectory(this, tr("Select Artwork Directory"));
  if (!dir.isEmpty()) {
    ui->artworkDirLineEdit->setText(dir);
  }
}

void ArtworkTabPanel::onExportPlaceholderPngs() {
  if (!m_model || !m_model->workingCollections || !m_model->currentIndex ||
      *m_model->currentIndex < 0 || *m_model->currentIndex >= m_model->workingCollections->size()) {
    return;
  }
  // Read the working CollectionConfig for media dir + extensions + tile
  // dimensions (those live on tabs the user might not have opened). The
  // artwork dir is read from the live line edit so unsaved edits on THIS
  // tab still drive the warm — matches the tooltip's promise.
  const CollectionConfig &cfg = (*m_model->workingCollections)[*m_model->currentIndex];
  const QString liveArtworkDir = ui->artworkDirLineEdit->text();

  const auto pre = PlaceholderWarmer::preflight(cfg, liveArtworkDir);
  if (pre.error != PlaceholderWarmer::PreflightError::None) {
    QMessageBox::warning(this, tr("Cannot export placeholders"), pre.humanMessage);
    return;
  }

  const auto reply =
      QMessageBox::question(this, tr("Export placeholder PNGs?"),
                            tr("Kartend will scan:\n  %1\n\nfor items matching this collection's "
                               "extensions and write a placeholder PNG into:\n  %2\n\nfor every "
                               "item that has no existing cover. Existing artwork is left "
                               "untouched. Generated files can be deleted or replaced later.\n\n"
                               "Continue?")
                                .arg(pre.resolvedMediaDirectory, pre.resolvedArtworkDirectory),
                            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (reply != QMessageBox::Yes) {
    return;
  }

  // Kartend-qe9a: run the export on a worker behind a cancelable progress
  // dialog. Multi-thousand-item libraries used to freeze the GUI behind a wait
  // cursor while the walk rendered + saved every tile. The render theme + base
  // are snapshotted here on the GUI thread so the worker can render each tile
  // to a QImage via ItemPlaceholderRenderer::buildTileImage without touching
  // QApplication / QPixmap off the GUI thread.
  const ItemPlaceholderRenderer::TileTheme theme = ItemWidget::currentTileTheme();
  const QColor base = palette().color(QPalette::Mid);
  PlaceholderWarmer::TileFactory factory = [theme, base](int w, int h, int r) {
    const quint64 seed =
        (static_cast<quint64>(w) << 32) | (static_cast<quint64>(h) << 16) | static_cast<quint64>(r);
    return ItemPlaceholderRenderer::buildTileImage(w, h, r, /*applyGradient=*/true, base,
                                                   /*lineAlphaScale=*/1.0, theme, seed);
  };

  auto *progress =
      new QProgressDialog(tr("Exporting placeholder artwork…"), tr("Cancel"), 0, 0, this);
  progress->setWindowModality(Qt::WindowModal);
  progress->setMinimumDuration(0);
  progress->setAutoClose(false);
  progress->setAutoReset(false);

  auto cancelFlag = std::make_shared<std::atomic<bool>>(false);
  connect(progress, &QProgressDialog::canceled, this, [cancelFlag]() { cancelFlag->store(true); });

  QPointer<QProgressDialog> progressPtr = progress;
  const PlaceholderWarmer::ProgressFn onProgress = [progressPtr](qint64 scanned, qint64 exported) {
    // Invoked on the worker thread — marshal the label update to the GUI.
    QMetaObject::invokeMethod(qApp, [progressPtr, scanned, exported]() {
      if (progressPtr) {
        progressPtr->setLabelText(QObject::tr("Scanned %1 items, exported %2 placeholders…")
                                      .arg(QString::number(scanned), QString::number(exported)));
      }
    });
  };
  const PlaceholderWarmer::CancelFn isCancelled = [cancelFlag]() { return cancelFlag->load(); };

  // Capture inputs by value — the worker outlives this stack frame and the
  // model's CollectionConfig may change underneath it.
  const CollectionConfig cfgCopy = cfg;
  const QString artworkDirCopy = liveArtworkDir;
  const int tileW = cfg.gridLayout.itemWidth;
  const int tileH = cfg.gridLayout.itemHeight;
  const int tileR = cfg.gridLayout.cornerRadius;

  QPointer<ArtworkTabPanel> self = this;
  auto *watcher = new QFutureWatcher<PlaceholderWarmer::Result>(this);
  connect(watcher, &QFutureWatcher<PlaceholderWarmer::Result>::finished, this,
          [self, watcher, progress]() {
            watcher->deleteLater();
            progress->close();
            progress->deleteLater();
            if (!self) {
              return;
            }
            const PlaceholderWarmer::Result result = watcher->result();
            if (result.cancelled) {
              return; // user canceled — no summary
            }
            QString summary = tr("Scanned %1 items.\nExported %2 new placeholder PNGs.\n"
                                 "Skipped %3 items that already had artwork.")
                                  .arg(QString::number(result.itemsScanned),
                                       QString::number(result.itemsExported),
                                       QString::number(result.itemsAlreadyHadArtwork));
            if (result.itemsFailed > 0) {
              summary += QStringLiteral("\n\n") + tr("Failed: %1\n%2")
                                                      .arg(QString::number(result.itemsFailed),
                                                           result.firstFailures.join(QChar('\n')));
              QMessageBox::warning(self, tr("Placeholder export finished with errors"), summary);
            } else {
              QMessageBox::information(self, tr("Placeholder export complete"), summary);
            }
          });

  watcher->setFuture(QtConcurrent::run([cfgCopy, artworkDirCopy, tileW, tileH, tileR, factory,
                                        onProgress, isCancelled]() {
    return PlaceholderWarmer::exportMissingPlaceholders(cfgCopy, artworkDirCopy, tileW, tileH,
                                                        tileR, factory, onProgress, isCancelled);
  }));
}

void ArtworkTabPanel::onBrowsePlaceholderArtwork() {
  const QString file = QFileDialog::getOpenFileName(
      this, tr("Select Placeholder Artwork"), {},
      tr("Image Files (*.png *.jpg *.jpeg *.bmp *.gif *.webp);;All Files (*)"));
  if (!file.isEmpty()) {
    ui->placeholderArtworkLineEdit->setText(file);
  }
}
