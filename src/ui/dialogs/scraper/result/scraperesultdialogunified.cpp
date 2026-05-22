// Implementation for ScrapeResultDialogUnified — the unified-flow half of
// ScrapeResultDialog. Methods here read / write host state and host UI
// widgets via the friend-class privilege declared on ScrapeResultDialog
// (Kartend-izpz, the 3fkz step-3 extraction). The pattern matches
// DetailsPaneArtwork / DetailsPaneMetadataView (Kartend-5nxz / cd2u): state
// stays where the existing access sites already reference it; this class
// is a method-organising extraction that pulls the ~1100 LOC of unified
// flow out of the 2566-LOC scraperesultdialog.cpp while keeping the
// SingleItem / Batch flows anchored on ScrapeResultDialog.
#include "scraperesultdialogunified.h"

#include "scraperesultdialog.h"

#include "applicationcontext.h"
#include "batchprogressview.h"
#include "flowlayout.h"
#include "mediatypecheckboxbuilder.h"
#include "singleitemview.h"

#include <limits>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHideEvent>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QPair>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSet>
#include <QShowEvent>
#include <QSplitter>
#include <QStackedWidget>
#include <QtConcurrent/QtConcurrentRun>
#include <QTextBrowser>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrlQuery>
#include <QVBoxLayout>

#include "extensionutils.h"
#include "idatabasemanager.h"
#include "imagedecodeutils.h"
#include "metadatalookupprovider.h"
#include "pathutils.h"
#include "scrapejobgrouping.h"

// Shares the "kartend.scrape.timings" category with the host TU; each TU
// keeps its own static instance (Qt's logging registry dedupes by name).
Q_LOGGING_CATEGORY(lcDialogTimingsUnified, "kartend.scrape.timings", QtWarningMsg)
#define lcDialogTimings lcDialogTimingsUnified

ScrapeResultDialogUnified::ScrapeResultDialogUnified(ScrapeResultDialog *dlg)
    : QObject(dlg), m_dlg(dlg) {}

ScrapeResultDialogUnified::~ScrapeResultDialogUnified() = default;

namespace {
/// Shorten provider-specific keys for the label cell so every label
/// fits in the fixed label-column width. Common SS prefixes
/// (classification_/screenscraper_/rom_) get stripped because they
/// add no info once the keys are grouped together. Underscores
/// collapse to spaces. Full original key still shows in the cell's
/// tooltip via the caller.
QString prettifyCustomKey(const QString &key) {
  QString r = key;
  if (r.startsWith(QLatin1String("classification_"))) {
    r = r.mid(QStringLiteral("classification_").length()).toUpper();
  } else if (r.startsWith(QLatin1String("screenscraper_"))) {
    r = r.mid(QStringLiteral("screenscraper_").length());
  } else if (r.startsWith(QLatin1String("rom_"))) {
    r = r.mid(QStringLiteral("rom_").length());
  }
  r.replace(QLatin1Char('_'), QLatin1Char(' '));
  return r;
}
} // namespace

QList<Scraper::MediaAsset> ScrapeResultDialogUnified::selectInteractiveMediaForApply(
    const Scraper::ScrapedItem &detail) const {
  // Derive the asset list from the setup-view media-type checkboxes
  // (same filter auto-mode uses). Each matching MediaAsset from the
  // candidate detail is queued for the host's download pass.
  QSet<QString> filter;
  for (auto it = m_dlg->m_mediaTypeChecks.constBegin(); it != m_dlg->m_mediaTypeChecks.constEnd();
       ++it) {
    if (it.key() == QLatin1String("_metadata")) continue;
    if (it.value()->isChecked()) filter.insert(it.key().toLower());
  }
  QList<Scraper::MediaAsset> selected;
  for (const auto &asset : detail.media) {
    if (!asset.url.isValid()) continue;
    if (filter.isEmpty()) {
      // Empty filter falls back to "front cover only" — same legacy
      // behaviour BatchScrapeRunner uses.
      if (asset.type.compare(QStringLiteral("front"), Qt::CaseInsensitive) == 0) {
        selected.append(asset);
        break;
      }
    } else if (filter.contains(asset.type.toLower())) {
      selected.append(asset);
    }
  }
  return selected;
}

void ScrapeResultDialogUnified::buildUnifiedPanel() {
  m_dlg->m_unifiedPage = new QWidget(m_dlg->m_modeStack);
  auto *root = new QVBoxLayout(m_dlg->m_unifiedPage);
  root->setContentsMargins(8, 8, 8, 8);
  root->setSpacing(8);

  // ── Top: collection tree (left) + items list (right) ────────────
  auto *splitter = new QSplitter(Qt::Horizontal, m_dlg->m_unifiedPage);
  m_dlg->m_unifiedSplitterContainer = splitter; // tracked so we can hide during a run

  m_dlg->m_collectionTree = new QTreeWidget(splitter);
  m_dlg->m_collectionTree->setHeaderLabel(tr("Collections"));
  m_dlg->m_collectionTree->setMinimumWidth(220);
  m_dlg->m_collectionTree->setRootIsDecorated(true);
  m_dlg->m_collectionTree->setAnimated(true);
  m_dlg->m_collectionTree->header()->setStretchLastSection(true);
  connect(m_dlg->m_collectionTree, &QTreeWidget::currentItemChanged, this,
          &ScrapeResultDialogUnified::onCollectionTreeCurrentChanged);
  connect(m_dlg->m_collectionTree, &QTreeWidget::itemChanged, this,
          &ScrapeResultDialogUnified::onCollectionCheckChanged);
  splitter->addWidget(m_dlg->m_collectionTree);

  auto *rightContainer = new QWidget(splitter);
  auto *rightLayout = new QVBoxLayout(rightContainer);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  auto *itemsHeaderRow = new QHBoxLayout;
  m_dlg->m_itemsHeaderLabel =
      new QLabel(tr("Select a collection to see its items."), rightContainer);
  itemsHeaderRow->addWidget(m_dlg->m_itemsHeaderLabel, 1);
  auto *itemsSelectAll = new QPushButton(tr("Select all"), rightContainer);
  auto *itemsSelectNone = new QPushButton(tr("Select none"), rightContainer);
  itemsHeaderRow->addWidget(itemsSelectAll);
  itemsHeaderRow->addWidget(itemsSelectNone);
  rightLayout->addLayout(itemsHeaderRow);
  // Bulk-toggle every visible row's checkbox + the underlying
  // inclusion map; mirrors what a user would do row-by-row. No-op
  // when no collection is currently displayed.
  auto setAllItemsChecked = [this](bool checked) {
    const auto *cur = m_dlg->m_collectionTree->currentItem();
    if (!cur) return;
    const int idx =
        m_dlg->m_treeItemToCollectionIndex.value(const_cast<QTreeWidgetItem *>(cur), -1);
    if (idx < 0) return;
    // The items list rows are only enabled when the collection itself
    // is checked; respect that gating here so disabled rows stay off.
    if (cur->checkState(0) != Qt::Checked && checked) return;
    QStringList included;
    QSignalBlocker b(m_dlg->m_unifiedItemsList);
    for (int i = 0; i < m_dlg->m_unifiedItemsList->count(); ++i) {
      auto *row = m_dlg->m_unifiedItemsList->item(i);
      if (!(row->flags() & Qt::ItemIsEnabled)) continue;
      row->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
      if (checked) {
        const QString path = row->data(Qt::UserRole).toString();
        if (!path.isEmpty()) included.append(path);
      }
    }
    m_dlg->m_itemSelectionByCollection[idx] = included;
  };
  connect(itemsSelectAll, &QPushButton::clicked, this,
          [setAllItemsChecked]() { setAllItemsChecked(true); });
  connect(itemsSelectNone, &QPushButton::clicked, this,
          [setAllItemsChecked]() { setAllItemsChecked(false); });
  m_dlg->m_unifiedItemsList = new QListWidget(rightContainer);
  m_dlg->m_unifiedItemsList->setSelectionMode(QAbstractItemView::NoSelection);
  connect(m_dlg->m_unifiedItemsList, &QListWidget::itemChanged, this,
          &ScrapeResultDialogUnified::onItemCheckChanged);
  rightLayout->addWidget(m_dlg->m_unifiedItemsList, 1);
  splitter->addWidget(rightContainer);
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  root->addWidget(splitter, 1);

  // ── Middle: media types ─────────────────────────────────────────
  // MediaTypeCheckboxBuilder owns the curated SS media-type table,
  // the 3-column grid layout, and the Select-all/none bulk-toggle
  // wiring. The dialog keeps `m_dlg->m_mediaTypeChecks` because the filter
  // derivation later in onApply / auto-mode reads back the per-type
  // checked state.
  m_dlg->m_mediaTypesGroup =
      MediaTypeCheckboxBuilder::build(m_dlg->m_unifiedPage, m_dlg->m_mediaTypeChecks);
  root->addWidget(m_dlg->m_mediaTypesGroup);

  // ── Mode toggle ─────────────────────────────────────────────────
  // Wrap the radio row in a container widget so we can hide the whole
  // row (label + both radios) while a scrape is running. QHBoxLayout
  // alone isn't a widget, so we'd otherwise have to toggle each child.
  m_dlg->m_modeRowContainer = new QWidget(m_dlg->m_unifiedPage);
  auto *modeRow = new QHBoxLayout(m_dlg->m_modeRowContainer);
  modeRow->setContentsMargins(0, 0, 0, 0);
  auto *modeLabel = new QLabel(tr("Mode:"), m_dlg->m_modeRowContainer);
  m_dlg->m_modeAutoRadio =
      new QRadioButton(tr("Auto-accept (first candidate)"), m_dlg->m_modeRowContainer);
  m_dlg->m_modeInteractiveRadio =
      new QRadioButton(tr("Interactive (pick candidate per item)"), m_dlg->m_modeRowContainer);
  m_dlg->m_modeAutoRadio->setChecked(true);
  modeRow->addWidget(modeLabel);
  modeRow->addWidget(m_dlg->m_modeAutoRadio);
  modeRow->addWidget(m_dlg->m_modeInteractiveRadio);
  modeRow->addStretch(1);
  root->addWidget(m_dlg->m_modeRowContainer);

  // ── Live view: currently-scraping metadata panel ────────────────
  // 10-column QGridLayout: FIVE (label, value) pairs per row.
  // Cross-row alignment is the whole point — every label sits in
  // one of cols {0, 2, 4, 6, 8}, every short value in one of cols
  // {1, 3, 5, 7, 9}, so labels line up vertically across all rows
  // AND every row has the same five-chip rhythm. Wide fields
  // (Description) span all value cols. Custom fields container
  // also spans the row.
  m_dlg->m_liveMetadataGroup = new QGroupBox(tr("Currently scraping"), m_dlg->m_unifiedPage);
  auto *metaOuter = new QVBoxLayout(m_dlg->m_liveMetadataGroup);
  metaOuter->setContentsMargins(8, 8, 8, 8);
  metaOuter->setSpacing(6);
  // ── Interactive candidate picker row ──────────────────────────────
  // Visible only while the service is waiting on the user to pick a
  // candidate (interactive mode). Selecting a row re-fetches detail
  // and refreshes the live metadata fields below. Stays hidden in
  // auto mode so the layout is identical to non-interactive scrapes
  // until the user explicitly turns interactive on.
  m_dlg->m_interactiveCandidateRow = new QWidget(m_dlg->m_liveMetadataGroup);
  auto *candRow = new QHBoxLayout(m_dlg->m_interactiveCandidateRow);
  candRow->setContentsMargins(0, 0, 0, 0);
  candRow->setSpacing(6);
  auto *candLbl = new QLabel(tr("Candidate:"), m_dlg->m_interactiveCandidateRow);
  candLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  candLbl->setMinimumWidth(78);
  candRow->addWidget(candLbl);
  m_dlg->m_interactiveCandidateCombo = new QComboBox(m_dlg->m_interactiveCandidateRow);
  m_dlg->m_interactiveCandidateCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  candRow->addWidget(m_dlg->m_interactiveCandidateCombo, /*stretch=*/1);
  // Combobox is the visible candidate picker in the unified live view;
  // the SingleItemScrapeView's QListWidget is the data owner. Forward
  // selection to the view's list so its onCandidateSelected drives the
  // detail fetch (Kartend-xvci step 4) — the host's detailLoaded
  // dispatch then calls applyScrapedItemToLive + enables Apply.
  connect(m_dlg->m_interactiveCandidateCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
          [this](int idx) {
            if (m_dlg->m_unifiedPhase != ScrapeResultDialog::UnifiedPhase::InteractivePicking) {
              return;
            }
            auto *list = m_dlg->m_singleItemView->candidateList();
            if (list && idx >= 0 && idx < list->count()) {
              list->setCurrentRow(idx);
            }
          });
  m_dlg->m_interactiveCandidateRow->hide();
  metaOuter->addWidget(m_dlg->m_interactiveCandidateRow);
  // Metadata host uses a plain vertical layout so the dialog can be
  // resized freely. Title + Description rows stretch horizontally to
  // fill the available width; everything else lives inside a backdrop
  // frame whose contents reflow via FlowLayout (typed fields above,
  // custom fields below). This keeps the panel readable on ultrawide
  // screens (more chips per row) AND low-res screens (chips wrap to
  // additional rows instead of overflowing the dialog width).
  auto *metaGridHost = new QWidget(m_dlg->m_liveMetadataGroup);
  auto *metaCol = new QVBoxLayout(metaGridHost);
  metaCol->setContentsMargins(0, 0, 0, 0);
  metaCol->setSpacing(6);

  // Uniform label width across every row so labels visually align.
  constexpr int kLabelW = 90;
  constexpr int kValueChipW = 90;

  auto makeFieldEdit = [this](bool bold = false) {
    auto *edit = new QLineEdit(m_dlg->m_liveMetadataGroup);
    edit->setReadOnly(true);
    edit->setFrame(true);
    if (bold) {
      QFont f = edit->font();
      f.setBold(true);
      edit->setFont(f);
    }
    return edit;
  };
  auto sizedLabel = [this](const QString &text) {
    auto *lbl = new QLabel(text, m_dlg->m_liveMetadataGroup);
    lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lbl->setFixedWidth(kLabelW);
    return lbl;
  };

  // ── Title row: label + stretching bold chip ───────────────────
  // Title is the most prominent field, so it gets its own full-width
  // row that grows with the dialog.
  m_dlg->m_liveMetadataTitle = makeFieldEdit(/*bold=*/true);
  m_dlg->m_liveMetadataTitle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  m_dlg->m_liveMetadataTitle->setMinimumWidth(kValueChipW * 2);
  {
    auto *row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);
    row->addWidget(sizedLabel(tr("Title:")));
    row->addWidget(m_dlg->m_liveMetadataTitle, /*stretch=*/1);
    metaCol->addLayout(row);
  }

  // ── Description row: top-aligned label + multi-line browser ───
  m_dlg->m_liveMetadataDescription = new QTextBrowser(m_dlg->m_liveMetadataGroup);
  m_dlg->m_liveMetadataDescription->setOpenExternalLinks(true);
  m_dlg->m_liveMetadataDescription->setMinimumHeight(80);
  m_dlg->m_liveMetadataDescription->setMaximumHeight(110);
  m_dlg->m_liveMetadataDescription->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
  {
    auto *row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);
    auto *lbl = sizedLabel(tr("Description:"));
    lbl->setAlignment(Qt::AlignRight | Qt::AlignTop);
    row->addWidget(lbl);
    row->addWidget(m_dlg->m_liveMetadataDescription, /*stretch=*/1);
    metaCol->addLayout(row);
  }

  // ── Backdrop frame for the post-description metadata ───────────
  // Wraps the typed short fields AND the custom-fields section in a
  // distinct visual container with a flat alternate-base tint + soft
  // rounded border. Inside, FlowLayouts let the chips wrap as the
  // dialog is resized.
  auto *postDescFrame = new QFrame(m_dlg->m_liveMetadataGroup);
  postDescFrame->setObjectName(QStringLiteral("scraperMetadataBackdrop"));
  postDescFrame->setFrameShape(QFrame::NoFrame);
  postDescFrame->setStyleSheet(QStringLiteral("QFrame#scraperMetadataBackdrop {"
                                              "  background: palette(alternate-base);"
                                              "  border: 1px solid palette(midlight);"
                                              "  border-radius: 10px;"
                                              "}"));
  postDescFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  auto *postOuter = new QVBoxLayout(postDescFrame);
  postOuter->setContentsMargins(12, 14, 12, 14);
  postOuter->setSpacing(8);

  // Helper that builds a fixed-size "chip" container holding a
  // right-aligned label and a read-only QLineEdit. Returns the
  // wrapper widget that the FlowLayout treats as a single item.
  auto makeChipPair = [&](const QString &label, QLineEdit *edit) -> QWidget * {
    auto *w = new QWidget(postDescFrame);
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(4);
    auto *lbl = new QLabel(label, w);
    lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lbl->setFixedWidth(kLabelW);
    edit->setParent(w);
    edit->setFixedWidth(kValueChipW);
    h->addWidget(lbl);
    h->addWidget(edit);
    w->setFixedSize(kLabelW + 4 + kValueChipW, edit->sizeHint().height() + 2);
    return w;
  };

  // ── Combined chip flow ────────────────────────────────────────
  // Single FlowLayout for ALL short metadata chips: typed fields
  // first, then custom fields appended after. Sharing one layout
  // means custom-field chips fill in directly after Tags on the same
  // row (instead of always starting a new row), so the bottom row
  // never has just one orphaned chip when there's horizontal room.
  m_dlg->m_liveMetadataPublisher = new QLineEdit(postDescFrame);
  m_dlg->m_liveMetadataPublisher->setReadOnly(true);
  m_dlg->m_liveMetadataDeveloper = new QLineEdit(postDescFrame);
  m_dlg->m_liveMetadataDeveloper->setReadOnly(true);
  m_dlg->m_liveMetadataReleased = new QLineEdit(postDescFrame);
  m_dlg->m_liveMetadataReleased->setReadOnly(true);
  m_dlg->m_liveMetadataSource = new QLineEdit(postDescFrame);
  m_dlg->m_liveMetadataSource->setReadOnly(true);
  m_dlg->m_liveMetadataGenre = new QLineEdit(postDescFrame);
  m_dlg->m_liveMetadataGenre->setReadOnly(true);
  m_dlg->m_liveMetadataPlayers = new QLineEdit(postDescFrame);
  m_dlg->m_liveMetadataPlayers->setReadOnly(true);
  m_dlg->m_liveMetadataContentRating = new QLineEdit(postDescFrame);
  m_dlg->m_liveMetadataContentRating->setReadOnly(true);
  m_dlg->m_liveMetadataRuntime = new QLineEdit(postDescFrame);
  m_dlg->m_liveMetadataRuntime->setReadOnly(true);
  m_dlg->m_liveMetadataTags = new QLineEdit(postDescFrame);
  m_dlg->m_liveMetadataTags->setReadOnly(true);

  m_dlg->m_liveExtrasContainer = new QWidget(postDescFrame);
  // FlowLayout computes height-for-width so the container grows /
  // shrinks naturally as chips wrap on resize. No min height — that
  // would create dead space on wide windows.
  m_dlg->m_liveExtrasContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  auto *extrasLayout = new FlowLayout(m_dlg->m_liveExtrasContainer, /*margin=*/0,
                                      /*hSp=*/8, /*vSp=*/6);
  m_dlg->m_liveExtrasContainer->setLayout(extrasLayout);
  extrasLayout->addWidget(makeChipPair(tr("Publisher:"), m_dlg->m_liveMetadataPublisher));
  extrasLayout->addWidget(makeChipPair(tr("Developer:"), m_dlg->m_liveMetadataDeveloper));
  extrasLayout->addWidget(makeChipPair(tr("Released:"), m_dlg->m_liveMetadataReleased));
  extrasLayout->addWidget(makeChipPair(tr("Source:"), m_dlg->m_liveMetadataSource));
  extrasLayout->addWidget(makeChipPair(tr("Genre:"), m_dlg->m_liveMetadataGenre));
  extrasLayout->addWidget(makeChipPair(tr("Players:"), m_dlg->m_liveMetadataPlayers));
  extrasLayout->addWidget(makeChipPair(tr("Rating:"), m_dlg->m_liveMetadataContentRating));
  extrasLayout->addWidget(makeChipPair(tr("Runtime:"), m_dlg->m_liveMetadataRuntime));
  extrasLayout->addWidget(makeChipPair(tr("Tags:"), m_dlg->m_liveMetadataTags));
  // populateCustomFields appends custom-key chips AFTER these typed
  // chips. m_dlg->m_typedChipCount marks the boundary so re-renders only
  // tear down the custom chips, leaving typed chips in place.
  m_dlg->m_typedChipCount = extrasLayout->count();
  postOuter->addWidget(m_dlg->m_liveExtrasContainer);

  // Add the backdrop frame to the outer vertical column.
  metaCol->addWidget(postDescFrame);

  // Pre-seed the custom-fields section with every key the SS parser
  // can emit (gathered from screenscraperparser.cpp). The user sees
  // the full placeholder grid the moment the panel opens — values
  // fill in as the scrape produces them. Other providers that emit
  // keys outside this list still get rendered (populateCustomFields
  // adds new keys dynamically), the seeded list just covers the
  // common case so the section isn't empty on first open.
  static const QStringList kKnownSSCustomKeys = {
      QStringLiteral("classification_esrb"),
      QStringLiteral("classification_pegi"),
      QStringLiteral("classification_usk"),
      QStringLiteral("classification_cero"),
      QStringLiteral("cloneof"),
      QStringLiteral("colors"),
      QStringLiteral("controls"),
      QStringLiteral("families"),
      QStringLiteral("languages"),
      QStringLiteral("modes"),
      QStringLiteral("notgame"),
      QStringLiteral("rating"),
      QStringLiteral("region"),
      QStringLiteral("resolution"),
      QStringLiteral("rom_crc"),
      QStringLiteral("rom_filename"),
      QStringLiteral("rom_md5"),
      QStringLiteral("rom_serial"),
      QStringLiteral("rom_sha1"),
      QStringLiteral("rom_size"),
      QStringLiteral("rom_type"),
      QStringLiteral("rotation"),
      QStringLiteral("screenscraper_companyid"),
      QStringLiteral("screenscraper_groupid"),
      QStringLiteral("screenscraper_id"),
      QStringLiteral("topstaff"),
  };
  for (const QString &k : kKnownSSCustomKeys) m_dlg->m_allSeenCustomKeys.insert(k);
  // Initial render with empty values — populateCustomFields handles
  // the persistent-cell creation against the seeded union.
  populateCustomFields({});

  // No fixed min height — title row + description row + backdrop
  // FlowLayouts each compute their own height-for-width, so the
  // panel naturally grows on narrow widths (chips wrap to more
  // rows) and stays compact on wide widths.

  // Attach the typed-fields grid below the candidate row in the
  // group's outer vertical layout.
  metaOuter->addWidget(metaGridHost);

  m_dlg->m_liveMetadataGroup->hide();
  root->addWidget(m_dlg->m_liveMetadataGroup);

  // ── Live view: recent media thumbnails ──────────────────────────
  // Compact horizontal filmstrip — auto-scrolls to keep the newest
  // thumbnail visible, no manual scrollbars. Items are tightly
  // packed (small spacing + no text label) so several covers fit
  // across the dialog width.
  m_dlg->m_liveThumbsGroup = new QGroupBox(tr("Recent media"), m_dlg->m_unifiedPage);
  auto *thumbsLayout = new QVBoxLayout(m_dlg->m_liveThumbsGroup);
  thumbsLayout->setContentsMargins(4, 4, 4, 4);
  m_dlg->m_liveThumbsStrip = new QListWidget(m_dlg->m_liveThumbsGroup);
  m_dlg->m_liveThumbsStrip->setViewMode(QListView::IconMode);
  m_dlg->m_liveThumbsStrip->setIconSize(QSize(96, 96));
  m_dlg->m_liveThumbsStrip->setFlow(QListView::LeftToRight);
  m_dlg->m_liveThumbsStrip->setWrapping(false);
  m_dlg->m_liveThumbsStrip->setMovement(QListView::Static);
  m_dlg->m_liveThumbsStrip->setSelectionMode(QAbstractItemView::NoSelection);
  m_dlg->m_liveThumbsStrip->setMaximumHeight(108);
  m_dlg->m_liveThumbsStrip->setUniformItemSizes(true);
  m_dlg->m_liveThumbsStrip->setSpacing(2);
  // Slightly larger than icon so items fit snugly with minimal gap.
  m_dlg->m_liveThumbsStrip->setGridSize(QSize(100, 100));
  m_dlg->m_liveThumbsStrip->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_dlg->m_liveThumbsStrip->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_dlg->m_liveThumbsStrip->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  m_dlg->m_liveThumbsStrip->setFrameShape(QFrame::NoFrame);
  thumbsLayout->addWidget(m_dlg->m_liveThumbsStrip);
  m_dlg->m_liveThumbsGroup->hide();
  root->addWidget(m_dlg->m_liveThumbsGroup);

  // ── Progress + status (visible during scrape) ───────────────────
  m_dlg->m_unifiedCurrentLabel = new QLabel(m_dlg->m_unifiedPage);
  m_dlg->m_unifiedCurrentLabel->setWordWrap(true);
  m_dlg->m_unifiedCurrentLabel->hide();
  root->addWidget(m_dlg->m_unifiedCurrentLabel);

  m_dlg->m_unifiedProgressBar = new QProgressBar(m_dlg->m_unifiedPage);
  m_dlg->m_unifiedProgressBar->setRange(0, 100);
  m_dlg->m_unifiedProgressBar->setValue(0);
  m_dlg->m_unifiedProgressBar->setTextVisible(true);
  m_dlg->m_unifiedProgressBar->hide();
  root->addWidget(m_dlg->m_unifiedProgressBar);

  m_dlg->m_unifiedTimingLabel = new QLabel(m_dlg->m_unifiedPage);
  m_dlg->m_unifiedTimingLabel->setWordWrap(true);
  m_dlg->m_unifiedTimingLabel->hide();
  root->addWidget(m_dlg->m_unifiedTimingLabel);

  m_dlg->m_unifiedCountsLabel = new QLabel(m_dlg->m_unifiedPage);
  m_dlg->m_unifiedCountsLabel->hide();
  // The error count is rendered as a link when non-zero; rich text
  // is needed for the anchor. Clicking it opens the failure list.
  m_dlg->m_unifiedCountsLabel->setTextFormat(Qt::RichText);
  m_dlg->m_unifiedCountsLabel->setTextInteractionFlags(Qt::LinksAccessibleByMouse |
                                                       Qt::LinksAccessibleByKeyboard);
  connect(m_dlg->m_unifiedCountsLabel, &QLabel::linkActivated, this,
          [this](const QString &) { showScrapeErrorDetails(); });
  root->addWidget(m_dlg->m_unifiedCountsLabel);

  // ScreenScraper request-quota readout. Hidden until a live scrape
  // delivers a valid quota via the service's quotaUpdated signal
  // (non-SS providers never do, so the row stays absent for them).
  m_dlg->m_unifiedQuotaLabel = new QLabel(m_dlg->m_unifiedPage);
  m_dlg->m_unifiedQuotaLabel->setWordWrap(true);
  m_dlg->m_unifiedQuotaLabel->hide();
  root->addWidget(m_dlg->m_unifiedQuotaLabel);
}

void ScrapeResultDialogUnified::tickValueMarquees() {
  if (!m_dlg->m_liveMetadataGroup) return;
  // Defensive check — the hideEvent stops m_dlg->m_marqueeTimer, but if a
  // late-fired tick lands before that the visibility gate keeps us
  // from doing the findChildren-tree-walk on an invisible dialog.
  if (!m_dlg->isVisible()) return;
  const auto edits = m_dlg->m_liveMetadataGroup->findChildren<QLineEdit *>();
  for (auto *edit : edits) {
    if (!edit) continue;
    const QString text = edit->text();
    if (text.isEmpty()) continue;
    QFontMetrics fm(edit->font());
    const int textW = fm.horizontalAdvance(text);
    // -12 px accounts for the QLineEdit frame + horizontal padding;
    // close enough that "remainder fits" is true once the actual
    // tail of the string is visible.
    const int viewW = edit->width() - 12;
    if (textW <= viewW) {
      // No overflow; ensure we're at the start (in case the cell
      // previously held a longer value mid-marquee).
      if (edit->cursorPosition() != 0) edit->setCursorPosition(0);
      m_dlg->m_marqueePauseTicks.remove(edit);
      continue;
    }
    // Pause-mode: counting down at the rightmost-visible position.
    // When the counter hits zero, snap back to position 0 (no
    // reverse animation) so the marquee restarts L→R.
    if (m_dlg->m_marqueePauseTicks.contains(edit)) {
      int &pause = m_dlg->m_marqueePauseTicks[edit];
      if (--pause <= 0) {
        edit->setCursorPosition(0);
        m_dlg->m_marqueePauseTicks.remove(edit);
      }
      continue;
    }
    // Advancing phase: increment cursor (which scrolls the visible
    // region one character to the right). When the remainder of the
    // text fits in the visible viewport, hold for ~1.5 s before the
    // L→R wrap.
    const int curPos = edit->cursorPosition();
    const int remW = fm.horizontalAdvance(text.mid(curPos));
    if (remW <= viewW) {
      m_dlg->m_marqueePauseTicks.insert(edit, 10); // ~10 × 150 ms = 1.5 s hold
    } else {
      edit->setCursorPosition(qMin(curPos + 1, text.length()));
    }
  }
}

void ScrapeResultDialogUnified::appendThumbAsync(const QString &path) {
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

void ScrapeResultDialogUnified::applyScrapedItemToLive(const Scraper::ScrapedItem &item) {
  // New item → reset every marquee back to position 0 so each cell
  // restarts the L→R animation from the head of the new value.
  m_dlg->m_marqueePauseTicks.clear();
  auto setFromStart = [](QLineEdit *e, const QString &t) {
    e->setText(t);
    e->setCursorPosition(0);
  };
  setFromStart(m_dlg->m_liveMetadataTitle, item.title);
  setFromStart(m_dlg->m_liveMetadataPublisher, item.publisher);
  setFromStart(m_dlg->m_liveMetadataReleased, item.releaseDate);
  setFromStart(m_dlg->m_liveMetadataDeveloper, item.developer);
  setFromStart(m_dlg->m_liveMetadataGenre, item.genre);
  setFromStart(m_dlg->m_liveMetadataPlayers, item.players);
  setFromStart(m_dlg->m_liveMetadataContentRating, item.contentRating);
  setFromStart(m_dlg->m_liveMetadataRuntime,
               item.runtimeSeconds > 0
                   ? ScrapeResultDialog::formatDuration(qint64(item.runtimeSeconds) * 1000LL)
                   : QString());
  setFromStart(m_dlg->m_liveMetadataTags, item.tagsJson);
  setFromStart(m_dlg->m_liveMetadataSource, item.sourceProviderId);
  m_dlg->m_liveMetadataDescription->setText(item.description);
  populateCustomFields(item.customFields);
}

void ScrapeResultDialogUnified::populateCustomFields(const QHash<QString, QString> &fields) {
  if (!m_dlg->m_liveExtrasContainer || !m_dlg->m_liveExtrasContainer->layout()) return;
  auto *layout = m_dlg->m_liveExtrasContainer->layout();
  if (!layout) return;
  // Merge incoming keys into the session-wide union so new keys
  // emitted mid-scrape get a placeholder chip added to the flow.
  bool newKeyAdded = false;
  for (auto it = fields.constBegin(); it != fields.constEnd(); ++it) {
    if (!m_dlg->m_allSeenCustomKeys.contains(it.key())) {
      m_dlg->m_allSeenCustomKeys.insert(it.key());
      newKeyAdded = true;
    }
  }
  // First-pass build or new-key arrival → rebuild every chip-pair so
  // sorted-key order stays stable across the full union. Only the
  // chips from m_dlg->m_typedChipCount onward are torn down — the leading
  // typed-field chips (Publisher … Tags) stay in place.
  if (m_dlg->m_customFieldEdits.isEmpty() || newKeyAdded) {
    while (layout->count() > m_dlg->m_typedChipCount) {
      QLayoutItem *child = layout->takeAt(m_dlg->m_typedChipCount);
      if (!child) break;
      if (auto *w = child->widget()) w->deleteLater();
      delete child;
    }
    m_dlg->m_customFieldEdits.clear();
    QStringList keys = m_dlg->m_allSeenCustomKeys.values();
    std::sort(keys.begin(), keys.end());
    // Custom-field cells share the EXACT same label + chip widths as
    // the typed-fields chips above so columns align visually across
    // both flows.
    constexpr int kCustomLabelW = 90;
    constexpr int kCustomValueW = 90;
    for (const QString &key : keys) {
      auto *chip = new QWidget(m_dlg->m_liveExtrasContainer);
      auto *h = new QHBoxLayout(chip);
      h->setContentsMargins(0, 0, 0, 0);
      h->setSpacing(4);
      auto *lbl = new QLabel(prettifyCustomKey(key) + QLatin1Char(':'), chip);
      lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
      lbl->setFixedWidth(kCustomLabelW);
      lbl->setToolTip(key);
      auto *edit = new QLineEdit(chip);
      edit->setReadOnly(true);
      edit->setFrame(true);
      edit->setFixedWidth(kCustomValueW);
      h->addWidget(lbl);
      h->addWidget(edit);
      chip->setFixedSize(kCustomLabelW + 4 + kCustomValueW, edit->sizeHint().height() + 2);
      layout->addWidget(chip);
      m_dlg->m_customFieldEdits.insert(key, edit);
    }
  }
  // Update value text in every persistent cell — empty string for
  // keys absent from the current item so the slot stays visible but
  // blank rather than disappearing.
  for (auto it = m_dlg->m_customFieldEdits.constBegin(); it != m_dlg->m_customFieldEdits.constEnd();
       ++it) {
    const QString value = fields.value(it.key()).trimmed();
    it.value()->setText(value);
    it.value()->setCursorPosition(0);
    it.value()->setToolTip(value);
  }
}

void ScrapeResultDialogUnified::startUnifiedScrape(int preCollectionIndex,
                                                   const QString &preItemPath) {
  m_dlg->m_mode = ScrapeResultDialog::Mode::Unified;
  m_dlg->m_unifiedPhase = ScrapeResultDialog::UnifiedPhase::Setup;
  m_dlg->m_modeStack->setCurrentWidget(m_dlg->m_unifiedPage);
  m_dlg->m_applyButton->hide();
  // Repurpose the dialog button area: hide the legacy Apply, show a
  // dedicated Scrape button + a Close button. Close hides the dialog
  // (the ScraperService keeps running); Cancel stops the active
  // scrape entirely.
  if (!m_dlg->m_scrapeButton) {
    auto *box = qobject_cast<QDialogButtonBox *>(m_dlg->m_applyButton->parent());
    if (box) {
      m_dlg->m_scrapeButton = box->addButton(tr("Scrape"), QDialogButtonBox::ActionRole);
      connect(m_dlg->m_scrapeButton, &QPushButton::clicked, this,
              &ScrapeResultDialogUnified::onScrapeClicked);
      m_dlg->m_closeButton = box->addButton(tr("Close"), QDialogButtonBox::ActionRole);
      m_dlg->m_closeButton->setToolTip(
          tr("Hide this window. The scrape keeps running in the background; "
             "reopen Scraper from the File menu to see progress."));
      connect(m_dlg->m_closeButton, &QPushButton::clicked, this, [this]() {
        // Mid-interactive close → tell the service to pause so it
        // doesn't fire the next item's picker into a vanished UI.
        // Auto mode + idle: just hide.
        if (m_dlg->m_service &&
            m_dlg->m_service->state() == Scraper::ScraperService::State::RunningInteractive) {
          m_dlg->m_service->pauseInteractive();
        }
        m_dlg->hide();
      });
      m_dlg->m_closeButton->hide();
    }
  }
  if (m_dlg->m_scrapeButton) m_dlg->m_scrapeButton->show();
  // Re-attach to a running service if there is one — we should
  // come up directly in the Live view, not the Setup view. Don't
  // touch the tree / items state in that case: the user might have
  // configured a selection earlier and we shouldn't overwrite it
  // mid-run. Setup view rebuilds the tree as it always has.
  if (m_dlg->m_service && m_dlg->m_service->isActive()) {
    setUnifiedSetupEnabled(false);
    if (m_dlg->m_closeButton) m_dlg->m_closeButton->show();
    // Start the 1-second live tick (the scrapeStarted handler missed
    // this run because we connected after it already fired).
    m_dlg->m_rateSamples.clear();
    if (!m_dlg->m_liveTickTimer) {
      m_dlg->m_liveTickTimer = new QTimer(this);
      m_dlg->m_liveTickTimer->setInterval(1000);
      connect(m_dlg->m_liveTickTimer, &QTimer::timeout, this,
              &ScrapeResultDialogUnified::updateUnifiedProgressLabel);
    }
    m_dlg->m_liveTickTimer->start();
    if (!m_dlg->m_marqueeTimer) {
      m_dlg->m_marqueeTimer = new QTimer(this);
      m_dlg->m_marqueeTimer->setInterval(150);
      connect(m_dlg->m_marqueeTimer, &QTimer::timeout, this,
              &ScrapeResultDialogUnified::tickValueMarquees);
    }
    m_dlg->m_marqueePauseTicks.clear();
    m_dlg->m_marqueeTimer->start();
    // Sync the Live view from the service's current snapshot so the
    // user immediately sees where the scrape is sitting instead of
    // waiting for the next signal tick.
    updateUnifiedProgressLabel();
    applyScrapedItemToLive(m_dlg->m_service->lastScrapedItem());
    m_dlg->m_unifiedCurrentLabel->setText(
        tr("Collection: %1 — scraping: %2")
            .arg(m_dlg->m_service->currentCollectionName(),
                 QFileInfo(m_dlg->m_service->currentItemPath()).fileName()));
    m_dlg->m_shownCollectionName = m_dlg->m_service->currentCollectionName();
    // Restore the recent-media thumbnail strip from the service so
    // the user doesn't see an empty band when re-entering. Icon-only
    // rows + auto-scroll to the latest match the same shape as the
    // itemCompleted-driven append path.
    m_dlg->m_liveThumbsStrip->clear();
    QListWidgetItem *restoreLast = nullptr;
    for (const QString &p : m_dlg->m_service->recentMediaPaths()) {
      if (p.isEmpty()) continue;
      // Same PDFium-abort guard as appendThumbAsync — recentMediaPaths
      // mirrors what itemScraped emits, so it includes the manual
      // `.pdf` for every game. QPixmap here runs on the MAIN UI
      // thread; a SIGTRAP here would crash the dialog before the user
      // could even close it.
      if (!ExtensionUtils::isDecodableImagePath(p)) continue;
      const QImage img = ImageDecodeUtils::loadCapped(p);
      if (img.isNull()) continue;
      const QPixmap pm = QPixmap::fromImage(img);
      if (pm.isNull()) continue;
      auto *row = new QListWidgetItem(
          QIcon(pm.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation)), QString(),
          m_dlg->m_liveThumbsStrip);
      row->setToolTip(QFileInfo(p).fileName());
      restoreLast = row;
    }
    if (restoreLast) {
      m_dlg->m_liveThumbsStrip->scrollToItem(restoreLast, QAbstractItemView::PositionAtBottom);
    }
    // If paused mid-interactive, kick service back into action so the
    // user's resumed UI gets a picker.
    if (m_dlg->m_service->state() == Scraper::ScraperService::State::PausedInteractive) {
      m_dlg->m_service->resumePaused();
    }
    return; // skip populateCollectionTree — keep existing selection
  }
  setUnifiedSetupEnabled(true);
  populateCollectionTree();

  // Right-click flow: pre-check exactly the requested collection +
  // its single item, leaving every other collection in the unchecked
  // default state.
  if (preCollectionIndex >= 0 && m_dlg->m_scraperCtx.collections &&
      preCollectionIndex < m_dlg->m_scraperCtx.collections->size()) {
    for (auto it = m_dlg->m_treeItemToCollectionIndex.constBegin();
         it != m_dlg->m_treeItemToCollectionIndex.constEnd(); ++it) {
      if (it.value() == preCollectionIndex) {
        QSignalBlocker b(m_dlg->m_collectionTree);
        it.key()->setCheckState(0, Qt::Checked);
        m_dlg->m_collectionTree->setCurrentItem(it.key());
        if (!preItemPath.isEmpty()) {
          // Seed the inclusion list with the single requested path so
          // the items-list rebuild ticks only that row.
          m_dlg->m_itemSelectionByCollection[preCollectionIndex] = {preItemPath};
          // Pre-populate the items cache too so we don't need a DB
          // round-trip to display the row immediately.
          if (!m_dlg->m_itemsCacheByCollection.contains(preCollectionIndex)) {
            m_dlg->m_itemsCacheByCollection[preCollectionIndex] = {preItemPath};
          }
        }
        rebuildItemsList(preCollectionIndex);
        break;
      }
    }
  }
}

void ScrapeResultDialogUnified::populateCollectionTree() {
  m_dlg->m_collectionTree->clear();
  m_dlg->m_treeItemToCollectionIndex.clear();
  if (!m_dlg->m_scraperCtx.collections) return;
  const auto &cols = *m_dlg->m_scraperCtx.collections;

  // Build a parent-aware QTreeWidget mirroring the collection
  // hierarchy via CollectionConfig::parentCollectionIndex (root rows
  // are pi == -1). Multi-pass placement: keep iterating until every
  // collection has been parented, since the source list isn't sorted
  // topologically. Bounded by depth; orphans (out-of-range parent)
  // get re-rooted as a defensive last pass.
  QHash<int, QTreeWidgetItem *> itemByIndex;
  QSignalBlocker b(m_dlg->m_collectionTree);
  int remaining = cols.size();
  while (remaining > 0) {
    bool progress = false;
    for (int i = 0; i < cols.size(); ++i) {
      if (itemByIndex.contains(i)) continue;
      const int pi = cols[i].parentCollectionIndex;
      QTreeWidgetItem *item = nullptr;
      if (pi < 0) {
        item = new QTreeWidgetItem(m_dlg->m_collectionTree);
      } else if (itemByIndex.contains(pi)) {
        item = new QTreeWidgetItem(itemByIndex.value(pi));
      } else {
        continue; // parent not placed yet — try again next pass
      }
      item->setText(0, cols[i].name);
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
      item->setCheckState(0, Qt::Unchecked);
      itemByIndex.insert(i, item);
      m_dlg->m_treeItemToCollectionIndex.insert(item, i);
      --remaining;
      progress = true;
    }
    if (!progress) break; // safety: orphan / cycle — bail to the rescue loop.
  }
  for (int i = 0; i < cols.size(); ++i) {
    if (itemByIndex.contains(i)) continue;
    auto *item = new QTreeWidgetItem(m_dlg->m_collectionTree);
    item->setText(0, cols[i].name);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(0, Qt::Unchecked);
    m_dlg->m_treeItemToCollectionIndex.insert(item, i);
  }
  m_dlg->m_collectionTree->expandAll();
}

void ScrapeResultDialogUnified::onCollectionTreeCurrentChanged(QTreeWidgetItem *current,
                                                               QTreeWidgetItem *) {
  if (!current) {
    m_dlg->m_itemsHeaderLabel->setText(tr("Select a collection to see its items."));
    m_dlg->m_unifiedItemsList->clear();
    return;
  }
  const int idx = m_dlg->m_treeItemToCollectionIndex.value(current, -1);
  if (idx < 0) return;
  rebuildItemsList(idx);
}

void ScrapeResultDialogUnified::applyCollectionCheckState(int collectionIndex, bool checked) {
  if (collectionIndex < 0) return;
  if (checked) {
    // Newly-checked collection: default the inclusion set to "every
    // item we know about". The items list rebuild ticks each row.
    if (m_dlg->m_itemsCacheByCollection.contains(collectionIndex)) {
      m_dlg->m_itemSelectionByCollection[collectionIndex] =
          m_dlg->m_itemsCacheByCollection.value(collectionIndex);
    } else {
      // Empty entry signals "include all" until the DB lookup lands;
      // the rebuildItemsList call kicks the DB fetch which populates
      // both caches once paths arrive. Without that fetch
      // m_dlg->m_itemSelectionByCollection[idx] would stay empty and the
      // Scrape button would silently no-op for this collection.
      m_dlg->m_itemSelectionByCollection.insert(collectionIndex, QStringList());
      rebuildItemsList(collectionIndex);
    }
  } else {
    m_dlg->m_itemSelectionByCollection.remove(collectionIndex);
  }
}

void ScrapeResultDialogUnified::onCollectionCheckChanged(QTreeWidgetItem *item, int column) {
  if (column != 0) return;
  const int idx = m_dlg->m_treeItemToCollectionIndex.value(item, -1);
  if (idx < 0) return;
  const bool checked = item->checkState(0) == Qt::Checked;
  applyCollectionCheckState(idx, checked);

  // Cascade the new state through the whole subtree: checking a parent
  // collection selects its subcollections too, unchecking clears them.
  // Tree signals are blocked while the child check states are written
  // so this doesn't re-enter once per child — the per-collection
  // bookkeeping is applied directly instead.
  QSet<int> affected{idx};
  {
    QSignalBlocker blocker(m_dlg->m_collectionTree);
    std::function<void(QTreeWidgetItem *)> cascade = [&](QTreeWidgetItem *parent) {
      for (int i = 0; i < parent->childCount(); ++i) {
        QTreeWidgetItem *child = parent->child(i);
        child->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
        const int childIdx = m_dlg->m_treeItemToCollectionIndex.value(child, -1);
        if (childIdx >= 0) {
          applyCollectionCheckState(childIdx, checked);
          affected.insert(childIdx);
        }
        cascade(child);
      }
    };
    cascade(item);
  }

  // Refresh the items list if the collection currently on screen is
  // one the cascade just touched.
  const auto *cur = m_dlg->m_collectionTree->currentItem();
  const int curIdx =
      cur ? m_dlg->m_treeItemToCollectionIndex.value(const_cast<QTreeWidgetItem *>(cur), -1) : -1;
  if (curIdx >= 0 && affected.contains(curIdx)) {
    rebuildItemsList(curIdx);
  }
}

void ScrapeResultDialogUnified::rebuildItemsList(int collectionIndex) {
  if (!m_dlg->m_scraperCtx.collections || collectionIndex < 0 ||
      collectionIndex >= m_dlg->m_scraperCtx.collections->size()) {
    return;
  }
  const CollectionConfig &cfg = (*m_dlg->m_scraperCtx.collections)[collectionIndex];
  m_dlg->m_itemsHeaderLabel->setText(tr("Items in '%1'").arg(cfg.name));

  // Fetch from DB on first display per session; cache for subsequent
  // tree clicks. Fetch is async — populate cache from the response.
  if (!m_dlg->m_itemsCacheByCollection.contains(collectionIndex)) {
    m_dlg->m_unifiedItemsList->clear();
    auto *placeholder = new QListWidgetItem(tr("Loading items…"), m_dlg->m_unifiedItemsList);
    placeholder->setFlags(placeholder->flags() & ~Qt::ItemIsEnabled);
    // Kartend-m02z: read DB through ctx instead of cached pointer.
    auto *db = m_dlg->m_scraperCtx.ctx ? m_dlg->m_scraperCtx.ctx->databaseManager() : nullptr;
    if (!db || !m_dlg->m_scraperCtx.collections) return;
    CollectionContext context;
    context.config = cfg;
    context.currentIndex = collectionIndex;
    QPointer<ScrapeResultDialog> guard(m_dlg);
    auto *connHolder = new QObject(this);
    QObject::connect(
        db, &IDatabaseManager::itemsRangeLoaded, connHolder,
        [guard, connHolder, collectionIndex](
            int /*offset*/, const QStringList &filePaths, const QHash<QString, QString> &,
            const QHash<QString, QString> &, const QHash<QString, QString> &,
            const QHash<QString, int> &fileToCollectionIndex, int requestedCollectionIndex) {
          // itemsRangeLoaded is a shared signal: when a parent collection is
          // cascade-checked the dialog has one fetchItemsRange in flight per
          // collection, and every connected handler sees every emission.
          // Consume ONLY the result for the collection this fetch asked for —
          // otherwise this collection's cache gets populated from another
          // collection's items (the whole-parent-group over-count bug).
          if (requestedCollectionIndex != collectionIndex) return;
          connHolder->deleteLater();
          if (guard.isNull()) return;
          guard->m_itemsCacheByCollection[collectionIndex] = filePaths;
          // Retain each item's owning-collection index so a
          // scrape of a shell parent routes per item rather
          // than dumping everything on the parent.
          guard->m_itemOwnerByCollection[collectionIndex] = fileToCollectionIndex;
          // If the collection was checked before items landed,
          // populate the inclusion set with the full list now.
          if (guard->m_itemSelectionByCollection.contains(collectionIndex) &&
              guard->m_itemSelectionByCollection.value(collectionIndex).isEmpty()) {
            guard->m_itemSelectionByCollection[collectionIndex] = filePaths;
          }
          // Only re-render if the user is still viewing this collection.
          const auto *cur = guard->m_collectionTree->currentItem();
          const int curIdx =
              cur ? guard->m_treeItemToCollectionIndex.value(const_cast<QTreeWidgetItem *>(cur), -1)
                  : -1;
          if (curIdx == collectionIndex) {
            guard->m_unified->rebuildItemsList(collectionIndex);
          }
        });
    db->fetchItemsRange(context, *m_dlg->m_scraperCtx.collections, 0,
                        std::numeric_limits<int>::max(), QString());
    return;
  }

  // Cache hit — render synchronously.
  const QStringList &paths = m_dlg->m_itemsCacheByCollection.value(collectionIndex);
  const auto *treeRow = [&]() -> QTreeWidgetItem * {
    for (auto it = m_dlg->m_treeItemToCollectionIndex.constBegin();
         it != m_dlg->m_treeItemToCollectionIndex.constEnd(); ++it) {
      if (it.value() == collectionIndex) return it.key();
    }
    return nullptr;
  }();
  const bool collectionChecked = treeRow && treeRow->checkState(0) == Qt::Checked;
  const QStringList &included = m_dlg->m_itemSelectionByCollection.value(collectionIndex);
  const QSet<QString> includedSet(included.begin(), included.end());

  QSignalBlocker b(m_dlg->m_unifiedItemsList);
  m_dlg->m_unifiedItemsList->clear();
  for (const QString &path : paths) {
    auto *row = new QListWidgetItem(QFileInfo(path).fileName(), m_dlg->m_unifiedItemsList);
    row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
    row->setData(Qt::UserRole, path);
    if (!collectionChecked) {
      row->setCheckState(Qt::Unchecked);
      row->setFlags(row->flags() & ~Qt::ItemIsEnabled);
    } else {
      row->setCheckState(includedSet.contains(path) ? Qt::Checked : Qt::Unchecked);
    }
  }
}

void ScrapeResultDialogUnified::onItemCheckChanged(QListWidgetItem *item) {
  if (!item) return;
  const auto *cur = m_dlg->m_collectionTree->currentItem();
  if (!cur) return;
  const int idx = m_dlg->m_treeItemToCollectionIndex.value(const_cast<QTreeWidgetItem *>(cur), -1);
  if (idx < 0) return;
  const QString path = item->data(Qt::UserRole).toString();
  if (path.isEmpty()) return;
  QStringList included = m_dlg->m_itemSelectionByCollection.value(idx);
  if (item->checkState() == Qt::Checked) {
    if (!included.contains(path)) included.append(path);
  } else {
    included.removeAll(path);
  }
  m_dlg->m_itemSelectionByCollection[idx] = included;
}

void ScrapeResultDialogUnified::setUnifiedSetupEnabled(bool enabled) {
  // Setup widgets — collection tree, items list, media-types group,
  // mode toggle — are HIDDEN while a scrape is running. The Live view
  // (metadata + thumbnails + progress) takes their place and gets the
  // full vertical space. Setup widgets reappear when the scrape ends.
  if (m_dlg->m_scrapeButton) m_dlg->m_scrapeButton->setEnabled(enabled);
  if (m_dlg->m_unifiedSplitterContainer) m_dlg->m_unifiedSplitterContainer->setVisible(enabled);
  if (m_dlg->m_mediaTypesGroup) m_dlg->m_mediaTypesGroup->setVisible(enabled);
  if (m_dlg->m_modeRowContainer) m_dlg->m_modeRowContainer->setVisible(enabled);

  const bool showProgress = !enabled;
  m_dlg->m_unifiedCurrentLabel->setVisible(showProgress);
  m_dlg->m_unifiedProgressBar->setVisible(showProgress);
  m_dlg->m_unifiedTimingLabel->setVisible(showProgress);
  m_dlg->m_unifiedCountsLabel->setVisible(showProgress);
  // The quota label is shown only once a live scrape reports a valid
  // quota (the quotaUpdated handler reveals it). Returning to the
  // setup view always hides it; entering the live view leaves it
  // hidden until that first quota update arrives.
  if (m_dlg->m_unifiedQuotaLabel && !showProgress) m_dlg->m_unifiedQuotaLabel->hide();
  if (m_dlg->m_liveMetadataGroup) m_dlg->m_liveMetadataGroup->setVisible(showProgress);
  if (m_dlg->m_liveThumbsGroup) m_dlg->m_liveThumbsGroup->setVisible(showProgress);
  if (m_dlg->m_closeButton) m_dlg->m_closeButton->setVisible(showProgress);
  // No explicit layout invalidation — Qt handles re-flow when widgets
  // hide/show, and forcing it was contributing to the dialog auto-
  // resizing on every toggle. The minimum-size pin in the ctor keeps
  // the window stable.
}

void ScrapeResultDialogUnified::onServiceScrapeStarted(int total) {
  qCInfo(lcDialogTimings) << "DIALOG service.scrapeStarted total=" << total;
  setUnifiedSetupEnabled(false);
  m_dlg->m_unifiedProgressBar->setRange(0, std::max(1, total));
  m_dlg->m_unifiedProgressBar->setValue(m_dlg->m_service->itemsCompleted());
  // Reset rate-window samples. The seen-keys union is left intact so
  // the pre-seeded known SS keys (plus any keys accumulated during
  // prior runs in this session) stay visible — values clear naturally
  // as each item rewrites them.
  m_dlg->m_rateSamples.clear();
  if (!m_dlg->m_liveTickTimer) {
    m_dlg->m_liveTickTimer = new QTimer(this);
    m_dlg->m_liveTickTimer->setInterval(1000);
    connect(m_dlg->m_liveTickTimer, &QTimer::timeout, this,
            &ScrapeResultDialogUnified::updateUnifiedProgressLabel);
  }
  m_dlg->m_liveTickTimer->start();
  // Value-marquee timer: scrolls overflowing chip text L→R then wraps.
  // Lazy-init on first scrapeStart.
  if (!m_dlg->m_marqueeTimer) {
    m_dlg->m_marqueeTimer = new QTimer(this);
    m_dlg->m_marqueeTimer->setInterval(150);
    connect(m_dlg->m_marqueeTimer, &QTimer::timeout, this,
            &ScrapeResultDialogUnified::tickValueMarquees);
  }
  m_dlg->m_marqueePauseTicks.clear();
  m_dlg->m_marqueeTimer->start();
  updateUnifiedProgressLabel();
}

void ScrapeResultDialogUnified::onServiceItemBegan(int done, int total,
                                                   const QString &collectionName,
                                                   const QString &name) {
  Q_UNUSED(total);
  Q_UNUSED(done);
  // Hidden dialog → skip the label-update work. With high
  // batchItemConcurrency this fires several times per second; not
  // worth updating widgets nobody can see.
  if (!m_dlg->isVisible()) return;
  qCDebug(lcDialogTimings) << "DIALOG service.itemBegan name=" << name;
  // Refresh the collection label HERE, not only in the itemCompleted
  // handler below: itemCompleted fires only on a successful scrape, so
  // a collection whose items all error (or all skip) would otherwise
  // leave the label frozen on the last collection that produced a
  // success while the scrape churns on. itemBegan fires for every item
  // whatever the outcome. Gated on an actual collection change so
  // batchItemConcurrency > 1 doesn't re-set the label as each parallel
  // item in the same collection starts.
  if (collectionName != m_dlg->m_shownCollectionName) {
    m_dlg->m_shownCollectionName = collectionName;
    m_dlg->m_unifiedCurrentLabel->setText(tr("Collection: %1").arg(collectionName));
  }
  // The metadata panel and the richer "last scraped" label form are
  // still updated together by the itemCompleted handler so they always
  // describe the same completed item — deliberately not touched here,
  // where many parallel items mid-lookup would wipe the panel every
  // few hundred ms.
  updateUnifiedProgressLabel();
}

void ScrapeResultDialogUnified::onServiceItemCompleted(int done, int total,
                                                       const Scraper::ScrapedItem &scraped,
                                                       const QStringList &mediaPaths) {
  Q_UNUSED(done);
  Q_UNUSED(total);
  // Hidden dialog → skip every UI update. The service still tracks
  // recentMediaPaths + lastScrapedItem internally, and startUnifiedScrape
  // rebuilds the thumb strip + metadata from that snapshot when the
  // dialog is reopened. No visible work means no reason to decode +
  // smooth-scale thumbnails on the main thread per completed item.
  if (!m_dlg->isVisible()) return;
  // Don't poke the progress bar here — updateUnifiedProgressLabel
  // (called below) is the single source of truth and reads counters
  // straight from the service. Doing both used to race: this slot
  // would set the value, then the helper would reset it from the
  // legacy m_dlg->m_unifiedItemsCompletedAcross (always 0 in service mode),
  // so the bar stayed at zero. Shared field-population path so auto
  // and interactive modes render to identical widgets.
  applyScrapedItemToLive(scraped);
  // Sync the "currently scraping" label with whatever just landed in
  // the metadata panel — both update together so label and fields
  // always describe the same item even when concurrency has many
  // items in flight.
  QString displayName = scraped.title;
  if (displayName.isEmpty() && !mediaPaths.isEmpty()) {
    displayName = QFileInfo(mediaPaths.first()).completeBaseName();
  }
  if (displayName.isEmpty()) {
    displayName = QFileInfo(m_dlg->m_service->currentItemPath()).fileName();
  }
  m_dlg->m_unifiedCurrentLabel->setText(
      tr("Collection: %1 — last scraped: %2")
          .arg(m_dlg->m_service->currentCollectionName(), displayName));
  // Append new media paths to the thumb strip via async decode/scale
  // (off the UI thread). Each completed decode auto-scrolls the strip
  // to its own freshly-added row, so the newest cover is always
  // visible. The strip is icon-only — less crowded, fits more
  // thumbnails — and bounded inside the watcher's finished slot so a
  // long batch doesn't grow it unbounded.
  for (const QString &p : mediaPaths) {
    if (p.isEmpty()) continue;
    appendThumbAsync(p);
  }
  updateUnifiedProgressLabel();
}

void ScrapeResultDialogUnified::onServicePickerNeeded(
    const QString &itemPath, const QString &itemName,
    const QList<Scraper::ScrapeCandidate> &candidates,
    std::shared_ptr<MetadataLookupProvider> provider, const QString &artworkDir) {
  Q_UNUSED(artworkDir);
  Q_UNUSED(itemName);
  // Stay on the unified live view (don't flip to the legacy single-item
  // page). Surface a candidate combo at the top of the metadata panel;
  // the existing live fields show the selected candidate's data; Apply
  // button confirms.
  m_dlg->m_unifiedPhase = ScrapeResultDialog::UnifiedPhase::InteractivePicking;
  m_dlg->m_mode = ScrapeResultDialog::Mode::Unified;
  m_dlg->m_interactiveProvider = provider;
  m_dlg->m_interactiveItems = {itemPath};
  m_dlg->m_interactiveCursor = 0;
  m_dlg->m_singleItemView->clearMediaRows();
  // Populate the candidate combo from the lookup result. Block signals
  // during the refill so the first-row change doesn't trigger a stray
  // detail fetch before the view's setProviderAndCandidates below
  // installs the matching candidate-list state.
  {
    QSignalBlocker blocker(m_dlg->m_interactiveCandidateCombo);
    m_dlg->m_interactiveCandidateCombo->clear();
    for (const auto &c : candidates) {
      QString label = c.displayName;
      if (!c.subtitle.isEmpty()) label += QStringLiteral(" — ") + c.subtitle;
      if (c.matchScore >= 0) label += QStringLiteral("  (%1)").arg(c.matchScore);
      m_dlg->m_interactiveCandidateCombo->addItem(label);
    }
  }
  m_dlg->m_interactiveCandidateRow->setVisible(candidates.size() > 0);
  if (m_dlg->m_applyButton) {
    m_dlg->m_applyButton->show();
    m_dlg->m_applyButton->setEnabled(false);
  }
  if (m_dlg->m_scrapeButton) m_dlg->m_scrapeButton->hide();
  // Hand the candidate list + provider to the view so its candidate-
  // selection slot fetches detail for row 0 and emits detailLoaded —
  // the host's signal dispatch then calls applyScrapedItemToLive +
  // enables Apply (Kartend-xvci step 4/5).
  m_dlg->m_singleItemView->setProviderAndCandidates(provider.get(), candidates);
}

void ScrapeResultDialogUnified::onServiceScrapeFinished(const Scraper::ScraperService::Summary &s) {
  qCInfo(lcDialogTimings) << "DIALOG service.scrapeFinished scraped=" << s.scraped
                          << "skipped=" << s.skipped << "errors=" << s.errors;
  if (m_dlg->m_liveTickTimer) m_dlg->m_liveTickTimer->stop();
  if (m_dlg->m_marqueeTimer) m_dlg->m_marqueeTimer->stop();
  m_dlg->m_marqueePauseTicks.clear();
  if (m_dlg->m_interactiveCandidateRow) m_dlg->m_interactiveCandidateRow->hide();
  // Reset phase so a subsequent Scrape click isn't rejected by the
  // "if (m_dlg->m_unifiedPhase != Setup) return;" guard in onScrapeClicked.
  // Interactive runs leave the phase at InteractivePicking; auto runs
  // leave it at Setup. We unconditionally snap back here.
  m_dlg->m_unifiedPhase = ScrapeResultDialog::UnifiedPhase::Setup;
  setUnifiedSetupEnabled(true);
  if (m_dlg->m_scrapeButton) m_dlg->m_scrapeButton->show();
  if (m_dlg->m_applyButton) m_dlg->m_applyButton->hide();
  // Quota-exhausted stop: setUnifiedSetupEnabled(true) hid the progress
  // label, but the user needs to see WHY the scrape ended early — and
  // when they can resume. Re-show the current-status label with the
  // quota message. The reset time comes from the live quota readout
  // when we have one (the label still holds it); otherwise fall back
  // to the generic "midnight UTC" wording.
  if (s.quotaExhausted) {
    // m_dlg->m_lastQuotaResetText is the local-time HH:mm captured from the
    // last live quota update; fall back to the generic wording when no
    // quota update arrived (e.g. the very first item hit 430 before
    // any ssuser block was parsed).
    const QString resetText =
        m_dlg->m_lastQuotaResetText.isEmpty() ? tr("midnight UTC") : m_dlg->m_lastQuotaResetText;
    m_dlg->m_unifiedCurrentLabel->setText(
        tr("Scrape stopped — ScreenScraper's daily quota is exhausted. "
           "Resume after it resets (%1).")
            .arg(resetText));
    m_dlg->m_unifiedCurrentLabel->show();
  }
  emit m_dlg->unifiedScrapeFinished(s.scraped, s.skipped, s.errors, s.firstFailures);
}

void ScrapeResultDialogUnified::onServiceScrapePaused() {
  m_dlg->m_unifiedCurrentLabel->setText(tr("Scrape paused — close to keep paused, or "
                                           "reopen to continue."));
}

void ScrapeResultDialogUnified::onServiceQuotaUpdated(const Scraper::QuotaStatus &quota) {
  if (!m_dlg->m_unifiedQuotaLabel) return;
  // dailyMax 0 = quota unknown (SS didn't report a ceiling); keep the
  // row hidden rather than showing "N / 0".
  if (!quota.valid || quota.dailyMax <= 0) {
    m_dlg->m_unifiedQuotaLabel->hide();
    return;
  }
  m_dlg->m_lastQuotaResetText = quota.resetAtUtc.toLocalTime().toString(QStringLiteral("HH:mm"));
  m_dlg->m_unifiedQuotaLabel->setText(tr("ScreenScraper: %1 / %2 requests today · resets %3")
                                          .arg(quota.dailyUsed)
                                          .arg(quota.dailyMax)
                                          .arg(m_dlg->m_lastQuotaResetText));
  m_dlg->m_unifiedQuotaLabel->show();
}

int ScrapeResultDialogUnified::totalCheckedItemCount() const {
  int total = 0;
  for (auto it = m_dlg->m_itemSelectionByCollection.constBegin();
       it != m_dlg->m_itemSelectionByCollection.constEnd(); ++it) {
    total += it.value().size();
  }
  return total;
}

void ScrapeResultDialogUnified::updateUnifiedProgressLabel() {
  // Service-driven path: counters live on the ScraperService, not on
  // the legacy m_dlg->m_unified* fields. Read from whichever is the source
  // of truth for the active run.
  int total = 0;
  int done = 0;
  qint64 startMs = 0;
  int scraped = 0;
  int skipped = 0;
  int errors = 0;
  if (m_dlg->m_service && m_dlg->m_service->isActive()) {
    total = m_dlg->m_service->totalItems();
    done = m_dlg->m_service->itemsCompleted();
    startMs = m_dlg->m_service->startedAtUnixMs();
    const auto s = m_dlg->m_service->summary();
    scraped = s.scraped;
    skipped = s.skipped;
    errors = s.errors;
  } else {
    total = totalCheckedItemCount();
    done = m_dlg->m_unifiedItemsCompletedAcross;
    startMs = m_dlg->m_unifiedStartMs;
    scraped = m_dlg->m_unifiedScrapedTotal;
    skipped = m_dlg->m_unifiedSkippedTotal;
    errors = m_dlg->m_unifiedErrorsTotal;
  }
  if (total <= 0) return;
  m_dlg->m_unifiedProgressBar->setRange(0, total);
  m_dlg->m_unifiedProgressBar->setValue(done);
  const qint64 elapsedMs = std::max<qint64>(1, QDateTime::currentMSecsSinceEpoch() - startMs);
  QString etaStr = QStringLiteral("—");
  if (done > 0 && total > done) {
    const qint64 etaMs =
        static_cast<qint64>((double(elapsedMs) / double(done)) * double(total - done));
    etaStr = ScrapeResultDialog::formatDuration(etaMs);
  }
  QString rateStr = QStringLiteral("0 KiB/s");
  if (m_dlg->m_service) {
    // Sliding-window rate: keep samples from the last ~10 seconds
    // and compute (newest.bytes - oldest.bytes) / window-duration.
    // Total-bytes ÷ total-elapsed underreports because lookup-API
    // calls + provider throttling create long no-download stretches
    // that dilute the average.
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 bytes = m_dlg->m_service->totalBytesDownloaded();
    constexpr qint64 kWindowMs = 10000;
    m_dlg->m_rateSamples.append({nowMs, bytes});
    while (m_dlg->m_rateSamples.size() > 1 &&
           nowMs - m_dlg->m_rateSamples.first().first > kWindowMs) {
      m_dlg->m_rateSamples.removeFirst();
    }
    if (m_dlg->m_rateSamples.size() >= 2) {
      const auto &oldest = m_dlg->m_rateSamples.first();
      const auto &newest = m_dlg->m_rateSamples.last();
      const qint64 deltaMs = std::max<qint64>(1, newest.first - oldest.first);
      const qint64 deltaBytes = std::max<qint64>(0, newest.second - oldest.second);
      const double mibPerSec = (deltaBytes / (1024.0 * 1024.0)) / (deltaMs / 1000.0);
      if (mibPerSec >= 1.0) {
        rateStr = tr("%1 MiB/s").arg(mibPerSec, 0, 'f', 1);
      } else {
        rateStr = tr("%1 KiB/s").arg(mibPerSec * 1024.0, 0, 'f', 0);
      }
    }
  }
  m_dlg->m_unifiedTimingLabel->setText(
      tr("Items %1/%2 · Elapsed %3 · ETA %4 · %5")
          .arg(done)
          .arg(total)
          .arg(ScrapeResultDialog::formatDuration(elapsedMs), etaStr, rateStr));
  int mediaWritten = 0;
  if (m_dlg->m_service) mediaWritten = m_dlg->m_service->summary().mediaWritten;
  // Render the error count as a clickable link when there are errors,
  // so the user can open the recorded failure messages. Substituted
  // into the %4 slot rather than baked into the tr() string so the
  // translatable text stays markup-free.
  const QString errorsField =
      errors > 0 ? QStringLiteral("<a href=\"kartend:scrape-errors\">%1</a>").arg(errors)
                 : QString::number(errors);
  m_dlg->m_unifiedCountsLabel->setText(tr("Scraped %1 items, %2 media  ·  Skipped %3  ·  Errors %4")
                                           .arg(QString::number(scraped),
                                                QString::number(mediaWritten),
                                                QString::number(skipped), errorsField));
}

void ScrapeResultDialogUnified::showScrapeErrorDetails() {
  // Gather failure messages from both the service summary (live /
  // service-driven runs) and the in-dialog accumulator (the fallback
  // orchestration path); dedupe so a message recorded by both isn't
  // listed twice.
  QStringList failures = m_dlg->m_unifiedFailures;
  if (m_dlg->m_service) {
    for (const QString &failure : m_dlg->m_service->summary().firstFailures) {
      if (!failures.contains(failure)) {
        failures.append(failure);
      }
    }
  }

  // The total error count comes straight off the summary; failures is
  // what was actually retained (capped — see kMaxReportedFailures).
  const int totalErrors = m_dlg->m_service ? m_dlg->m_service->summary().errors : failures.size();

  // A resizable dialog with a scrollable list — a misconfigured
  // collection can report hundreds of failures, far past what a
  // QMessageBox can show without clipping.
  QDialog dlg(m_dlg);
  dlg.setWindowTitle(tr("Scrape errors"));
  auto *layout = new QVBoxLayout(&dlg);
  if (failures.isEmpty()) {
    // The error counter advanced but no message was captured.
    layout->addWidget(
        new QLabel(tr("No further error detail was recorded for this scrape."), &dlg));
  } else {
    QString header = tr("The scrape reported the following errors:");
    if (failures.size() < totalErrors) {
      // More items errored than messages were retained — say so rather
      // than letting the user assume the list is complete.
      header = tr("Showing %1 of %2 errors:").arg(failures.size()).arg(totalErrors);
    }
    layout->addWidget(new QLabel(header, &dlg));
    auto *list = new QListWidget(&dlg);
    list->addItems(failures);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list->setWordWrap(true);
    layout->addWidget(list);
  }
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
  connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  layout->addWidget(buttons);
  dlg.resize(640, 420);
  dlg.exec();
}

void ScrapeResultDialogUnified::onScrapeClicked() {
  if (m_dlg->m_unifiedPhase != ScrapeResultDialog::UnifiedPhase::Setup) return;
  if (!m_dlg->m_scraperCtx.collections) return;
  // Walk the tree in display order (including subcollections under
  // their parents) so the user sees a predictable collection
  // sequence in the progress label. Recursive walk because the tree
  // is now hierarchical (top-level + subcollection rows).
  QList<QTreeWidgetItem *> rowsInOrder;
  std::function<void(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *item) {
    if (!item) return;
    rowsInOrder.append(item);
    for (int i = 0; i < item->childCount(); ++i) walk(item->child(i));
  };
  for (int i = 0; i < m_dlg->m_collectionTree->topLevelItemCount(); ++i) {
    walk(m_dlg->m_collectionTree->topLevelItem(i));
  }
  // Translate checked rows into ScraperService::CollectionJob entries.
  // Each job carries the collection uuid + artwork dir resolved here
  // (the service's persistence layer keys jobs by these so resume
  // can survive a config reorder).
  QList<Scraper::ScraperService::CollectionJob> serviceQueue;
  // Legacy/test fallback queue uses the dialog's own CollectionJob
  // shape; populated in parallel so the in-dialog orchestration still
  // runs when no service is wired.
  m_dlg->m_unifiedQueue.clear();
  m_dlg->m_unifiedQueueCursor = 0;
  m_dlg->m_unifiedScrapedTotal = 0;
  m_dlg->m_unifiedSkippedTotal = 0;
  m_dlg->m_unifiedErrorsTotal = 0;
  m_dlg->m_unifiedFailures.clear();
  m_dlg->m_unifiedItemsCompletedAcross = 0;
  m_dlg->m_unifiedCancelled = false;
  m_dlg->m_unifiedStartMs = QDateTime::currentMSecsSinceEpoch();
  // Resolve every checked item to its owning collection, then emit one
  // job per owner. A "shell" parent collection displays the items of
  // its subcollections; checking the parent row pulls those items in,
  // but each item's scraped artwork + metadata must land on the
  // subcollection that owns it — not the parent. ScrapeJobGrouping does
  // the (pure, unit-tested) grouping; owners keep tree display order so
  // the progress label stays predictable.
  QList<int> checkedOrder;
  for (QTreeWidgetItem *row : rowsInOrder) {
    if (row->checkState(0) == Qt::Checked) {
      checkedOrder.append(m_dlg->m_treeItemToCollectionIndex.value(row, -1));
    }
  }
  const auto ownerGroups = ScrapeJobGrouping::byOwningCollection(
      checkedOrder, m_dlg->m_itemSelectionByCollection, m_dlg->m_itemOwnerByCollection,
      static_cast<int>(m_dlg->m_scraperCtx.collections->size()));
  for (const auto &group : ownerGroups) {
    const int owner = group.first;
    const QStringList &items = group.second;
    if (items.isEmpty()) continue;
    const CollectionConfig &cfg = (*m_dlg->m_scraperCtx.collections)[owner];
    const QString expandedMediaDir = PathUtils::validateAndExpandPath(cfg.mediaDirectory, cfg.name);
    const QString uuid = CollectionUtils::computeCollectionUuid(cfg.name, expandedMediaDir);
    const QString artworkDir = PathUtils::validateAndExpandPath(cfg.artworkDirectory, cfg.name);

    Scraper::ScraperService::CollectionJob sJob;
    sJob.collectionIndex = owner;
    sJob.collectionUuid = uuid;
    sJob.collectionName = cfg.name;
    sJob.artworkDir = artworkDir;
    sJob.items = items;
    serviceQueue.append(sJob);

    ScrapeResultDialog::CollectionJob job;
    job.collectionIndex = owner;
    job.collectionName = cfg.name;
    job.items = items;
    m_dlg->m_unifiedQueue.append(job);
  }
  if (serviceQueue.isEmpty()) {
    QMessageBox::information(m_dlg, tr("Scraper"),
                             tr("Pick at least one collection (and one item) before scraping."));
    return;
  }
  // Translate user picks into runner config. Filter keys are
  // lowercased: the runner matches each asset's `type.toLower()`
  // against this set, so a mixed-case key (e.g. "support-2D") would
  // otherwise never match and that media type would silently never
  // download.
  QSet<QString> mediaFilter;
  bool writeMetadata = true;
  for (auto it = m_dlg->m_mediaTypeChecks.constBegin(); it != m_dlg->m_mediaTypeChecks.constEnd();
       ++it) {
    if (it.key() == QLatin1String("_metadata")) {
      writeMetadata = it.value()->isChecked();
      continue;
    }
    if (it.value()->isChecked()) mediaFilter.insert(it.key().toLower());
  }
  const auto mode = m_dlg->m_modeAutoRadio->isChecked()
                        ? Scraper::ScraperService::Mode::Auto
                        : Scraper::ScraperService::Mode::Interactive;

  if (m_dlg->m_service) {
    // Production path: hand the queue off to the long-lived service
    // and let it drive. The dialog's signal handlers (wired in
    // setScraperService) flip the UI into Live view + advance progress
    // from there. Close button now relevant.
    qCInfo(lcDialogTimings) << "DIALOG onScrapeClicked: service path, queue size="
                            << serviceQueue.size() << "mode="
                            << (mode == Scraper::ScraperService::Mode::Auto ? "auto"
                                                                            : "interactive")
                            << "writeMetadata=" << writeMetadata << "mediaFilter=" << mediaFilter;
    setUnifiedSetupEnabled(false);
    if (m_dlg->m_closeButton) m_dlg->m_closeButton->show();
    m_dlg->m_service->startScrape(serviceQueue, mode, mediaFilter, writeMetadata);
    return;
  }

  // Fallback path: no service wired — run the orchestration in the
  // dialog itself (matches the v1 behaviour for tests / unit harnesses
  // that haven't been migrated to the service yet). This path keeps
  // working but does NOT survive dialog close / app exit.
  setUnifiedSetupEnabled(false);
  m_dlg->m_unifiedProgressBar->setRange(0, totalCheckedItemCount());
  m_dlg->m_unifiedProgressBar->setValue(0);
  updateUnifiedProgressLabel();
  startNextCollectionInQueue();
}

void ScrapeResultDialogUnified::startNextCollectionInQueue() {
  if (m_dlg->m_unifiedCancelled || m_dlg->m_unifiedQueueCursor >= m_dlg->m_unifiedQueue.size()) {
    // All done — fire summary signal, leave the dialog open with the
    // final state visible (caller can dismiss).
    m_dlg->m_unifiedPhase = ScrapeResultDialog::UnifiedPhase::Done;
    m_dlg->m_unifiedCurrentLabel->setText(tr("Finished."));
    emit m_dlg->unifiedScrapeFinished(m_dlg->m_unifiedScrapedTotal, m_dlg->m_unifiedSkippedTotal,
                                      m_dlg->m_unifiedErrorsTotal, m_dlg->m_unifiedFailures);
    m_dlg->accept();
    return;
  }
  const ScrapeResultDialog::CollectionJob &job = m_dlg->m_unifiedQueue[m_dlg->m_unifiedQueueCursor];
  m_dlg->m_unifiedCurrentLabel->setText(tr("Collection: %1  (%2 of %3)")
                                            .arg(job.collectionName)
                                            .arg(m_dlg->m_unifiedQueueCursor + 1)
                                            .arg(m_dlg->m_unifiedQueue.size()));
  updateUnifiedProgressLabel();
  if (m_dlg->m_modeAutoRadio->isChecked()) {
    m_dlg->m_unifiedPhase = ScrapeResultDialog::UnifiedPhase::AutoRunning;
    runAutoCollection(job.collectionIndex, job.items);
  } else {
    m_dlg->m_unifiedPhase = ScrapeResultDialog::UnifiedPhase::InteractiveLookingUp;
    runInteractiveCollection(job.collectionIndex, job.items);
  }
}

void ScrapeResultDialogUnified::runAutoCollection(int collectionIndex, const QStringList &items) {
  // Kartend-m02z: ScraperContext now carries the full ApplicationContext;
  // a missing/null ctx is treated like the legacy missing databaseManager.
  auto *runAutoDb = m_dlg->m_scraperCtx.ctx ? m_dlg->m_scraperCtx.ctx->databaseManager() : nullptr;
  if (!m_dlg->m_scraperCtx.providerBuilder || !runAutoDb || !m_dlg->m_scraperCtx.generalSettings ||
      !m_dlg->m_scraperCtx.collections) {
    ++m_dlg->m_unifiedErrorsTotal;
    ++m_dlg->m_unifiedQueueCursor;
    startNextCollectionInQueue();
    return;
  }
  std::shared_ptr<MetadataLookupProvider> provider =
      m_dlg->m_scraperCtx.providerBuilder(collectionIndex);
  if (!provider) {
    m_dlg->m_unifiedFailures.append(
        tr("%1: no provider applies")
            .arg(m_dlg->m_unifiedQueue[m_dlg->m_unifiedQueueCursor].collectionName));
    m_dlg->m_unifiedErrorsTotal += items.size();
    m_dlg->m_unifiedItemsCompletedAcross += items.size();
    ++m_dlg->m_unifiedQueueCursor;
    updateUnifiedProgressLabel();
    startNextCollectionInQueue();
    return;
  }
  const CollectionConfig &cfg = (*m_dlg->m_scraperCtx.collections)[collectionIndex];
  const QString expandedMediaDir = PathUtils::validateAndExpandPath(cfg.mediaDirectory, cfg.name);
  const QString uuid = CollectionUtils::computeCollectionUuid(cfg.name, expandedMediaDir);
  const QString artworkDir = PathUtils::validateAndExpandPath(cfg.artworkDirectory, cfg.name);
  const Scraper::RescrapeMode rescrapeMode = static_cast<Scraper::RescrapeMode>(
      m_dlg->m_scraperCtx.generalSettings->scraperOptions.rescrapeMode);
  const int itemConcurrency =
      m_dlg->m_scraperCtx.generalSettings->scraperOptions.batchItemConcurrency;
  const int skipRecentDays =
      m_dlg->m_scraperCtx.generalSettings->scraperOptions.skipRecentScrapeDays;

  // Translate the user's media-type checkboxes into the runner's
  // filter set. The synthetic `_metadata` key gates text-field
  // persistence and is consumed here (stripped from the filter set,
  // routed to setWriteMetadata instead). Empty media filter → runner
  // falls back to legacy "front only" behaviour; non-empty → runner
  // fetches every matching type per item in parallel.
  QSet<QString> mediaFilter;
  bool writeMetadata = true;
  for (auto it = m_dlg->m_mediaTypeChecks.constBegin(); it != m_dlg->m_mediaTypeChecks.constEnd();
       ++it) {
    if (it.key() == QLatin1String("_metadata")) {
      writeMetadata = it.value()->isChecked();
      continue;
    }
    if (it.value()->isChecked()) mediaFilter.insert(it.key().toLower());
  }

  auto *runner = new Scraper::BatchScrapeRunner(
      m_dlg->m_scraperCtx.ctx, std::move(provider), uuid, items, artworkDir,
      /*fetchPrimaryCover=*/true, rescrapeMode, itemConcurrency, skipRecentDays, this);
  runner->setMediaTypeFilter(mediaFilter);
  runner->setWriteMetadata(writeMetadata);
  m_dlg->m_batchRunner = runner;

  connect(
      runner, &Scraper::BatchScrapeRunner::progress, this,
      [this](int done, int total, const QString &name) {
        // `done` is per-collection; aggregate across queue items
        // for the dialog's outer progress. m_dlg->m_unifiedItemsCompletedAcross
        // accumulates the prior queue items' completions before this
        // collection started — the +done below is per-collection
        // progress on top of that running total.
        const int totalAcross = totalCheckedItemCount();
        m_dlg->m_unifiedProgressBar->setRange(0, totalAcross);
        m_dlg->m_unifiedProgressBar->setValue(m_dlg->m_unifiedItemsCompletedAcross + done);
        m_dlg->m_unifiedCurrentLabel->setText(
            tr("Scraping: %1  (item %2 of %3 in this collection)").arg(name).arg(done).arg(total));
        updateUnifiedProgressLabel();
      });
  connect(runner, &Scraper::BatchScrapeRunner::finished, this,
          [this, runner, items](const Scraper::BatchScrapeRunner::Summary &s) {
            m_dlg->m_unifiedScrapedTotal += s.scraped;
            m_dlg->m_unifiedSkippedTotal += s.skipped;
            m_dlg->m_unifiedErrorsTotal += s.errors;
            m_dlg->m_unifiedFailures.append(s.firstFailures);
            m_dlg->m_unifiedItemsCompletedAcross += items.size();
            m_dlg->m_batchRunner = nullptr;
            runner->deleteLater();
            ++m_dlg->m_unifiedQueueCursor;
            startNextCollectionInQueue();
          });
  runner->start();
}

void ScrapeResultDialogUnified::runInteractiveCollection(int collectionIndex,
                                                         const QStringList &items) {
  if (!m_dlg->m_scraperCtx.providerBuilder) {
    ++m_dlg->m_unifiedErrorsTotal;
    ++m_dlg->m_unifiedQueueCursor;
    startNextCollectionInQueue();
    return;
  }
  m_dlg->m_interactiveProvider = m_dlg->m_scraperCtx.providerBuilder(collectionIndex);
  m_dlg->m_interactiveItems = items;
  m_dlg->m_interactiveCursor = 0;
  m_dlg->m_interactiveCollectionIndex = collectionIndex;
  if (!m_dlg->m_interactiveProvider) {
    m_dlg->m_unifiedFailures.append(
        tr("%1: no provider applies")
            .arg(m_dlg->m_unifiedQueue[m_dlg->m_unifiedQueueCursor].collectionName));
    m_dlg->m_unifiedErrorsTotal += items.size();
    m_dlg->m_unifiedItemsCompletedAcross += items.size();
    ++m_dlg->m_unifiedQueueCursor;
    updateUnifiedProgressLabel();
    startNextCollectionInQueue();
    return;
  }
  interactiveNextItem();
}

void ScrapeResultDialogUnified::interactiveNextItem() {
  if (m_dlg->m_unifiedCancelled || m_dlg->m_interactiveCursor >= m_dlg->m_interactiveItems.size()) {
    // End of items for this collection — advance the outer queue.
    ++m_dlg->m_unifiedQueueCursor;
    startNextCollectionInQueue();
    return;
  }
  const QString filePath = m_dlg->m_interactiveItems[m_dlg->m_interactiveCursor];
  m_dlg->m_unifiedCurrentLabel->setText(tr("Looking up: %1").arg(QFileInfo(filePath).fileName()));
  // Issue the lookup; once candidates land we flip to the single-item
  // page and let the user pick.
  const QString queryText = QFileInfo(filePath).completeBaseName();
  MetadataLookupProvider::LookupContext ctx{queryText, filePath};
  QPointer<ScrapeResultDialog> guard(m_dlg);
  m_dlg->m_interactiveProvider->lookup(
      ctx, [guard](ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> r) {
        if (guard.isNull()) return;
        guard->m_unified->interactiveOnLookupResult(r);
      });
}

void ScrapeResultDialogUnified::interactiveOnLookupResult(
    ErrorUtils::Result<QList<Scraper::ScrapeCandidate>> result) {
  if (m_dlg->m_unifiedCancelled) {
    ++m_dlg->m_unifiedQueueCursor;
    startNextCollectionInQueue();
    return;
  }
  if (result.isError() || result.value().isEmpty()) {
    if (result.isError()) {
      ++m_dlg->m_unifiedErrorsTotal;
      m_dlg->m_unifiedFailures.append(QStringLiteral("%1: %2").arg(
          QFileInfo(m_dlg->m_interactiveItems[m_dlg->m_interactiveCursor]).fileName(),
          result.error().message));
    } else {
      ++m_dlg->m_unifiedSkippedTotal;
    }
    ++m_dlg->m_unifiedItemsCompletedAcross;
    ++m_dlg->m_interactiveCursor;
    updateUnifiedProgressLabel();
    interactiveNextItem();
    return;
  }
  // Flip to the single-item picker page for this item. Hand the
  // candidate list to the view (Kartend-xvci step 4) — its
  // setProviderAndCandidates populates the list widget, hides the
  // panel on single-result responses, pre-selects row 0, and the
  // resulting onCandidateSelected fetches detail. After the user
  // clicks Apply or Cancel we catch the result via interactiveOnApplied
  // / interactiveOnSkipped.
  m_dlg->m_unifiedPhase = ScrapeResultDialog::UnifiedPhase::InteractivePicking;
  m_dlg->m_singleItemView->clearMediaRows();
  m_dlg->m_modeStack->setCurrentWidget(m_dlg->m_singleItemView);
  m_dlg->m_applyButton->show();
  m_dlg->m_applyButton->setEnabled(false);
  if (m_dlg->m_scrapeButton) m_dlg->m_scrapeButton->hide();
  m_dlg->m_singleItemView->setProviderAndCandidates(m_dlg->m_interactiveProvider.get(),
                                                    result.value());
}

void ScrapeResultDialogUnified::interactiveOnApplied() {
  // Called when the user's Apply finishes (m_dlg->m_result populated).
  // Honour the `_metadata` checkbox by stripping textual fields when
  // unchecked — applyResult downstream will then preserve whatever's
  // in the DB instead of overwriting with the scrape's text.
  ScrapeResultDialog::Result delivered = m_dlg->m_result;
  auto *metaCheck = m_dlg->m_mediaTypeChecks.value(QStringLiteral("_metadata"));
  if (metaCheck && !metaCheck->isChecked()) {
    delivered.item.title.clear();
    delivered.item.description.clear();
    delivered.item.genre.clear();
    delivered.item.developer.clear();
    delivered.item.publisher.clear();
    delivered.item.releaseDate.clear();
    delivered.item.contentRating.clear();
    delivered.item.players.clear();
    delivered.item.tagsJson.clear();
    delivered.item.customFields.clear();
    delivered.item.sourceProviderId.clear();
    delivered.item.runtimeSeconds = -1;
  }
  if (m_dlg->m_scraperCtx.applyResult) {
    m_dlg->m_scraperCtx.applyResult(m_dlg->m_interactiveCollectionIndex,
                                    m_dlg->m_interactiveItems[m_dlg->m_interactiveCursor],
                                    delivered);
  }
  ++m_dlg->m_unifiedScrapedTotal;
  ++m_dlg->m_unifiedItemsCompletedAcross;
  ++m_dlg->m_interactiveCursor;
  updateUnifiedProgressLabel();
  // Switch back to the unified page for the next item's lookup phase.
  m_dlg->m_modeStack->setCurrentWidget(m_dlg->m_unifiedPage);
  if (m_dlg->m_scrapeButton) m_dlg->m_scrapeButton->show();
  m_dlg->m_applyButton->hide();
  m_dlg->m_unifiedPhase = ScrapeResultDialog::UnifiedPhase::InteractiveLookingUp;
  interactiveNextItem();
}

void ScrapeResultDialogUnified::finishCurrentApply() {
  // Unified interactive picker: don't close the dialog — advance to
  // the next item instead.
  if (m_dlg->m_mode == ScrapeResultDialog::Mode::Unified &&
      m_dlg->m_unifiedPhase == ScrapeResultDialog::UnifiedPhase::InteractivePicking) {
    // Service-driven path: persist via the caller's applyResult hook,
    // then tell the service to advance. The service emits
    // `pickerNeeded` for the next item, which the dialog's signal
    // handler flips us into. Stays in the same window throughout.
    if (m_dlg->m_service) {
      ScrapeResultDialog::Result delivered = m_dlg->m_result;
      auto *metaCheck = m_dlg->m_mediaTypeChecks.value(QStringLiteral("_metadata"));
      if (metaCheck && !metaCheck->isChecked()) {
        delivered.item.title.clear();
        delivered.item.description.clear();
        delivered.item.genre.clear();
        delivered.item.developer.clear();
        delivered.item.publisher.clear();
        delivered.item.releaseDate.clear();
        delivered.item.contentRating.clear();
        delivered.item.players.clear();
        delivered.item.tagsJson.clear();
        delivered.item.customFields.clear();
        delivered.item.sourceProviderId.clear();
        delivered.item.runtimeSeconds = -1;
      }
      if (m_dlg->m_scraperCtx.applyResult && !m_dlg->m_interactiveItems.isEmpty()) {
        // The service is the source of truth for which collection
        // is being processed (the dialog may be reattached to a
        // resumed run where m_dlg->m_interactiveCollectionIndex is stale).
        const int idx = m_dlg->m_service->currentCollectionIndex();
        m_dlg->m_scraperCtx.applyResult(idx, m_dlg->m_interactiveItems.first(), delivered);
      }
      // Stay on the unified page. Apply button hides until the next
      // pickerNeeded signal arrives (which re-enables it with the
      // next item's candidates). The interactive candidate row also
      // hides momentarily — the next pickerNeeded re-shows it.
      if (m_dlg->m_applyButton) m_dlg->m_applyButton->hide();
      if (m_dlg->m_interactiveCandidateRow) m_dlg->m_interactiveCandidateRow->hide();
      m_dlg->m_service->applyPick(delivered.item);
      return;
    }
    interactiveOnApplied();
    return;
  }
  m_dlg->accept();
}

void ScrapeResultDialogUnified::interactiveOnSkipped() {
  ++m_dlg->m_unifiedSkippedTotal;
  ++m_dlg->m_unifiedItemsCompletedAcross;
  ++m_dlg->m_interactiveCursor;
  updateUnifiedProgressLabel();
  m_dlg->m_modeStack->setCurrentWidget(m_dlg->m_unifiedPage);
  if (m_dlg->m_scrapeButton) m_dlg->m_scrapeButton->show();
  m_dlg->m_applyButton->hide();
  m_dlg->m_unifiedPhase = ScrapeResultDialog::UnifiedPhase::InteractiveLookingUp;
  interactiveNextItem();
}
