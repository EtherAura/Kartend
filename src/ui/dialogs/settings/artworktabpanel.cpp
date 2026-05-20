#include "artworktabpanel.h"

#include "collectionutils.h"
#include "itemwidget.h"
#include "placeholderwarmer.h"
#include "settingsformbinding.h"
#include "settingsmodel.h"
#include "ui_artworktabpanel.h"

#include <QApplication>
#include <QCursor>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
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
}

void ArtworkTabPanel::clear() {
  ui->artworkDirLineEdit->clear();
  ui->placeholderArtworkLineEdit->clear();
  ui->customArtworkTypesLineEdit->clear();
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
}

bool ArtworkTabPanel::hasChanges() const {
  if (!m_model || !m_model->originalCollection) return false;
  const CollectionConfig &o = *m_model->originalCollection;
  if (ui->artworkDirLineEdit->text() != o.artworkDirectory) return true;
  if (ui->placeholderArtworkLineEdit->text() != o.placeholderArtwork) return true;
  // Compare against the parsed-and-deduped form so a stylistic comma-spacing
  // tweak ("a, b" vs "a,b") doesn't register as dirty.
  if (parseCustomArtworkTypes() != o.customArtworkTypes) return true;
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

  // Synchronous run — cover-art generation is cheap (a few hundred items
  // tile in well under a second) and a progress dialog would more than
  // double the surface area. Wait cursor signals "I'm working" for the
  // larger libraries.
  QApplication::setOverrideCursor(Qt::WaitCursor);
  const auto result = PlaceholderWarmer::exportMissingPlaceholders(
      cfg, liveArtworkDir, cfg.gridLayout.itemWidth, cfg.gridLayout.itemHeight, cfg.gridLayout.cornerRadius,
      [](int w, int h, int r) { return ItemWidget::buildPlaceholderTile(w, h, r); });
  QApplication::restoreOverrideCursor();

  QString summary =
      tr("Scanned %1 items.\nExported %2 new placeholder PNGs.\n"
         "Skipped %3 items that already had artwork.")
          .arg(QString::number(result.itemsScanned), QString::number(result.itemsExported),
               QString::number(result.itemsAlreadyHadArtwork));
  if (result.itemsFailed > 0) {
    summary += QStringLiteral("\n\n") + tr("Failed: %1\n%2")
                                            .arg(QString::number(result.itemsFailed),
                                                 result.firstFailures.join(QChar('\n')));
    QMessageBox::warning(this, tr("Placeholder export finished with errors"), summary);
  } else {
    QMessageBox::information(this, tr("Placeholder export complete"), summary);
  }
}

void ArtworkTabPanel::onBrowsePlaceholderArtwork() {
  const QString file = QFileDialog::getOpenFileName(
      this, tr("Select Placeholder Artwork"), {},
      tr("Image Files (*.png *.jpg *.jpeg *.bmp *.gif *.webp);;All Files (*)"));
  if (!file.isEmpty()) {
    ui->placeholderArtworkLineEdit->setText(file);
  }
}
