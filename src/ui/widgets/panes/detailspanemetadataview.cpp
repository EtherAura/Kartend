// Implementation for DetailsPaneMetadataView — the dynamically-built
// "Details" section of DetailsPane. Friend-of-host pattern: state stays
// on DetailsPane so sibling .cpp files keep compiling untouched; the
// methods here reach into the host's m_detailsContainer / m_detailsLayout
// / m_descriptionScroll / m_metadataScroll / m_manualButton / ui-> via
// the friend privilege declared on DetailsPane (Kartend-cd2u).
#include "detailspanemetadataview.h"

#include "detailsformat.h"
#include "detailspane.h"
#include "ui_detailspane.h"
#include "usagestatsstore.h"

#include <cmath>
#include <QColor>
#include <QDesktopServices>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QTimer>
#include <QUrl>

#include <QVBoxLayout>

DetailsPaneMetadataView::DetailsPaneMetadataView(QObject *parent) : QObject(parent) {}

void DetailsPaneMetadataView::setHost(DetailsPane *host) {
  m_host = host;
}

void DetailsPaneMetadataView::ensureDetailsSection() {
  if (!m_host) return;
  if (m_host->m_detailsContainer) {
    return;
  }
  auto *contentLayout = qobject_cast<QVBoxLayout *>(m_host->ui->contentWidget->layout());
  if (!contentLayout) {
    return;
  }

  m_host->m_detailsContainer = new QWidget(m_host->ui->contentWidget);
  auto *outer = new QVBoxLayout(m_host->m_detailsContainer);
  outer->setContentsMargins(0, 0, 0, 0);
  // Slightly looser stacking: description tile and metadata card read
  // as distinct blocks with a clear breath between them, rather than
  // butting up against each other.
  outer->setSpacing(5);

  // No section heading: the metadata rows speak for themselves below
  // the description, and the previous "Details" label only widened
  // the gap between the gallery scrollbar and the first row.

  // Manual button is constructed up front so it can be referenced from
  // setManualFile / clearDetailsSection without lifecycle juggling, but
  // it's only laid out at the *bottom* of the details section (below
  // the metadata scroll). That placement is intentional: when the
  // button was above the description, scraped items with a manual
  // pushed the description ~28px below the gallery, making the gap
  // between gallery and description read as much larger than it
  // actually was.
  m_host->m_manualButton = new QPushButton(QObject::tr("Open Manual"), m_host->m_detailsContainer);
  m_host->m_manualButton->setCursor(Qt::PointingHandCursor);
  m_host->m_manualButton->hide();
  connect(m_host->m_manualButton, &QPushButton::clicked, this,
          &DetailsPaneMetadataView::openCurrentManual);

  // Metadata grid lives inside a QFrame (the styled backdrop) which is
  // itself nested in a QScrollArea. The QScrollArea claims the leftover
  // vertical space below the description and clips anything that
  // overflows — an auto-scroll timer marquees through the rows so the
  // outer details pane never needs to be scrolled.
  m_host->m_metadataBackdrop = new QFrame;
  m_host->m_metadataBackdrop->setObjectName(QStringLiteral("metadataBackdrop"));
  m_host->m_metadataBackdrop->setFrameShape(QFrame::NoFrame);
  auto *gridHost = new QVBoxLayout(m_host->m_metadataBackdrop);
  gridHost->setContentsMargins(6, 4, 6, 4);
  gridHost->setSpacing(0);
  m_host->m_detailsLayout = new QGridLayout();
  m_host->m_detailsLayout->setContentsMargins(0, 0, 0, 0);
  m_host->m_detailsLayout->setHorizontalSpacing(10);
  m_host->m_detailsLayout->setVerticalSpacing(6);
  // Equal stretch on both columns so short pairs (Genre / Players, etc.)
  // get balanced halves instead of one taking the slack.
  m_host->m_detailsLayout->setColumnStretch(0, 1);
  m_host->m_detailsLayout->setColumnStretch(1, 1);
  gridHost->addLayout(m_host->m_detailsLayout);
  // Trailing stretch keeps the rows top-aligned when the card has more
  // vertical room than the rows need.
  gridHost->addStretch(1);

  m_host->m_metadataScroll = new QScrollArea(m_host->m_detailsContainer);
  m_host->m_metadataScroll->setObjectName(QStringLiteral("metadataScroll"));
  m_host->m_metadataScroll->setFrameShape(QFrame::NoFrame);
  m_host->m_metadataScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_host->m_metadataScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_host->m_metadataScroll->setWidgetResizable(true);
  m_host->m_metadataScroll->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
  m_host->m_metadataScroll->viewport()->setAutoFillBackground(false);
  m_host->m_metadataScroll->setStyleSheet("QScrollArea { background: transparent; border: none; }");
  m_host->m_metadataScroll->setWidget(m_host->m_metadataBackdrop);
  outer->addWidget(m_host->m_metadataScroll, /*stretch=*/1);
  // No alignment on purpose (Kartend-6i10t): summary cards fill the whole
  // slot now (the hug-to-content cap is gone per user decision), and an
  // ALIGNED layout item stops stretching — the old AlignTop pin, needed
  // when the cap left slack in the cell, would itself shrink the card to
  // its size hint. Rows stay top-aligned via the backdrop's trailing
  // stretch instead.
  // Manual button anchored below the scrolling metadata so it doesn't
  // disturb the gallery→description hand-off at the top of the section.
  outer->addWidget(m_host->m_manualButton);

  // Insert ABOVE the static file-info rows, not at the end of the content
  // column: the unscraped skeleton (Kartend-um69l) shows Description +
  // placeholder rows WITH the file-info fallback, and the agreed order is
  // skeleton first, File Information at the bottom. Scraped items hide the
  // file rows, so their layout is unchanged by the insert position.
  const int fileInfoIndex = contentLayout->indexOf(m_host->ui->fileInfoTitle);
  if (fileInfoIndex >= 0) {
    contentLayout->insertWidget(fileInfoIndex, m_host->m_detailsContainer, /*stretch=*/1);
  } else {
    contentLayout->addWidget(m_host->m_detailsContainer, /*stretch=*/1);
  }
  m_host->m_detailsContainer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
  m_host->m_detailsContainer->hide();

  // Auto-scroll driver for the metadata block. One QTimer runs for the
  // pane's lifetime; it's a no-op when the inner card fits the
  // viewport. clearDetailsSection snaps the scroll position back to 0
  // so each new selection starts at the top.
  m_host->m_metadataAutoScrollTimer.setInterval(120);
  m_host->m_metadataAutoScrollPause = 16;
  connect(&m_host->m_metadataAutoScrollTimer, &QTimer::timeout, this, [this]() {
    if (!m_host || !m_host->m_metadataScroll) return;
    auto *bar = m_host->m_metadataScroll->verticalScrollBar();
    if (!bar || bar->maximum() <= 0) {
      m_host->m_metadataAutoScrollPause = 16; // re-arm pause for when content grows
      return;
    }
    if (m_host->m_metadataAutoScrollPause > 0) {
      --m_host->m_metadataAutoScrollPause;
      return;
    }
    if (bar->value() >= bar->maximum()) {
      bar->setValue(0);
      m_host->m_metadataAutoScrollPause = 25;
      return;
    }
    bar->setValue(bar->value() + 1);
  });
  m_host->m_metadataAutoScrollTimer.start();
}

void DetailsPaneMetadataView::clearDetailsSection() {
  if (!m_host || !m_host->m_detailsLayout) {
    return;
  }
  while (QLayoutItem *child = m_host->m_detailsLayout->takeAt(0)) {
    if (QWidget *w = child->widget()) {
      w->deleteLater();
    }
    delete child;
  }
  // Reset the grid cursor so the next rebuild starts in the top-left
  // cell rather than continuing wherever the previous selection left off.
  m_host->m_detailsRow = 0;
  m_host->m_detailsCol = 0;
  // Description widgets live outside m_metadataScroll so they aren't
  // touched by the grid clear above; nuke them here so the next
  // appendScrollingDescription rebuilds with the new selection's text.
  if (m_host->m_descriptionScroll) {
    m_host->m_descriptionScroll->deleteLater();
    m_host->m_descriptionScroll = nullptr;
  }
  if (m_host->m_descriptionLabel) {
    m_host->m_descriptionLabel->deleteLater();
    m_host->m_descriptionLabel = nullptr;
  }
  // Snap the metadata auto-scroller back to the top + pause so the
  // freshly-built rows get the same "rest then start scrolling" cycle
  // a fresh selection would.
  if (m_host->m_metadataScroll) {
    if (auto *bar = m_host->m_metadataScroll->verticalScrollBar()) {
      bar->setValue(0);
    }
    // Drop any height cap a previous Collection-tab render applied. The
    // Item tab's metadata card is meant to fill the leftover space
    // below the description (the auto-scroller marquees any overflow);
    // renderCollectionSummary re-applies the cap when it needs it.
    m_host->m_metadataScroll->setMaximumHeight(QWIDGETSIZE_MAX);
  }
  m_host->m_metadataAutoScrollPause = 16;
}

void DetailsPaneMetadataView::appendDetailRow(const QString &label, const QString &value, bool wrap,
                                              bool placeholder, bool linkify) {
  if (!m_host || !m_host->m_detailsLayout || value.trimmed().isEmpty()) {
    return;
  }
  // Render label + value as one inline QLabel ("Genre: Action"). The label
  // prefix uses the per-collection sidebar accent (palette(Highlight) is
  // plumbed in applyAppearance); the value text uses the default text
  // color. Single line per pair keeps the metadata block compact and lets
  // two short pairs share a row in the two-column grid.
  auto *rowLabel = new QLabel(m_host->m_detailsContainer);
  rowLabel->setTextFormat(Qt::RichText);
  // QTextDocument (rich text) won't resolve `palette(highlight)` — we
  // need a concrete hex color for the span style. Pull it from the
  // detail container's palette so the per-collection accent override
  // wins. (applyBubbleStyles rebuilds the value rows on appearance
  // change, so this snapshot stays fresh.)
  QColor accent = m_host->m_detailsContainer->palette().color(QPalette::Highlight);
  // Contrast guard (Kartend-6i10t): a per-collection accent close in luma to
  // the value-pill background made the bold key prefix nearly invisible
  // (dark-red accent on a dark-red theme's pill). Fall back to the plain
  // text colour when the pair can't carry a bold label.
  if (m_host->m_sectionBubbleColor.isValid()) {
    const auto luma = [](const QColor &c) {
      return 0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue();
    };
    if (std::abs(luma(accent) - luma(m_host->m_sectionBubbleColor)) < 60.0) {
      accent = m_host->m_detailsContainer->palette().color(QPalette::WindowText);
    }
  }
  if (placeholder) {
    // Same concrete-hex reasoning as the accent above: PlaceholderText is
    // the palette's own dimmed role, so the stand-in row tracks light and
    // dark schemes without a hardcoded grey.
    const QColor dim = m_host->m_detailsContainer->palette().color(QPalette::PlaceholderText);
    rowLabel->setText(
        QStringLiteral("<span style=\"color:%1;\"><b>%2:</b></span> "
                       "<span style=\"color:%3;font-style:italic;\">%4</span>")
            .arg(accent.name(), label.toHtmlEscaped(), dim.name(), value.toHtmlEscaped()));
  } else if (linkify) {
    // Website row: the value is an http(s) URL (the parser refuses other
    // schemes). Anchor colour follows the accent so the link reads as part
    // of the card, not browser-default blue on every theme.
    rowLabel->setText(QStringLiteral("<span style=\"color:%1;\"><b>%2:</b></span> "
                                     "<a href=\"%3\" style=\"color:%1;\">%3</a>")
                          .arg(accent.name(), label.toHtmlEscaped(), value.toHtmlEscaped()));
    rowLabel->setOpenExternalLinks(true);
  } else {
    rowLabel->setText(QStringLiteral("<span style=\"color:%1;\"><b>%2:</b></span> %3")
                          .arg(accent.name(), label.toHtmlEscaped(), value.toHtmlEscaped()));
  }
  // Color only — DO NOT set `padding: 0px` here. An inline padding
  // declaration on a QLabel that also matches the global
  // `QLabel[sidebarRole="value"]` rule overrides the bubble's padding
  // entirely, which means the text would render flush against the
  // bubble edge. Leaving padding unset lets the global 5px 12px (plus
  // any explicit left-only padding added in applyBubbleStyles) win.
  rowLabel->setStyleSheet("color: palette(windowtext);");
  rowLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  rowLabel->setWordWrap(wrap);
  rowLabel->setProperty("sidebarRole", "value");
  rowLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

  // Decide whether this row fits as a half-width cell or needs the full
  // sidebar width. wrap=true is the explicit "I will be long" caller
  // signal; the heuristic kicks in otherwise. Measure the bold label
  // prefix and the regular-weight value separately because the bubble
  // renders the prefix bold (so a plain fontMetrics call underestimates
  // the rendered width). Subtract the per-cell bubble padding from the
  // half-column budget so we don't over-pack rows the bubble would clip.
  // MEASURE THE WIDTH THE ROW ACTUALLY GETS, which is not always the
  // viewport's. DetailsPane::constrainContentToViewport() caps contentWidget's
  // maximumWidth at the viewport width, and that cap can lag the viewport --
  // the pane is constructed at DetailsPane::FIXED_WIDTH and only later resized
  // to the collection's sidebarWidth. While the two disagree, the viewport
  // reports the NEW width and the row is laid out inside the OLD one.
  //
  // That mismatch is what made a wider pane clip its own content: at a 420px
  // viewport halfW is ~176, wide enough for "Developer: Wildfire Games", so
  // the row was packed into a half cell -- then rendered inside a contentWidget
  // still capped at 300, where half a cell is ~116, and the value was cut with
  // no horizontal scrollbar to reach it (horizontalScrollBarPolicy is
  // AlwaysOff). Taking the smaller of the two makes the heuristic pessimistic
  // exactly when they disagree, so a row that cannot fit becomes a full row
  // instead of a clipped half one.
  const int viewportOnly = (m_host->ui->scrollArea && m_host->ui->scrollArea->viewport())
                               ? m_host->ui->scrollArea->viewport()->width()
                               : 0;
  const int contentCap = m_host->ui->contentWidget ? m_host->ui->contentWidget->maximumWidth() : 0;
  const int viewportW = (viewportOnly > 0 && contentCap > 0 && contentCap < QWIDGETSIZE_MAX)
                            ? qMin(viewportOnly, contentCap)
                            : viewportOnly;
  // 28 = contentLayout l+r margin, 8 = column gap, 32 = 2*16 bubble padding.
  const int halfW = qMax(0, (viewportW - 28 - 8 - 32) / 2);
  QFont boldFont = rowLabel->font();
  boldFont.setBold(true);
  const QFontMetrics fmBold(boldFont);
  const QFontMetrics fmReg(rowLabel->font());
  const int textW =
      fmBold.horizontalAdvance(label + QStringLiteral(": ")) + fmReg.horizontalAdvance(value);
  const bool fullRow = wrap || (halfW > 0 && textW > halfW);

  if (fullRow) {
    if (m_host->m_detailsCol != 0) {
      ++m_host->m_detailsRow;
      m_host->m_detailsCol = 0;
    }
    m_host->m_detailsLayout->addWidget(rowLabel, m_host->m_detailsRow, 0, 1, 2);
    ++m_host->m_detailsRow;
    m_host->m_detailsCol = 0;
    return;
  }
  m_host->m_detailsLayout->addWidget(rowLabel, m_host->m_detailsRow, m_host->m_detailsCol);
  if (m_host->m_detailsCol == 0) {
    m_host->m_detailsCol = 1;
  } else {
    m_host->m_detailsCol = 0;
    ++m_host->m_detailsRow;
  }
}

void DetailsPaneMetadataView::appendScrollingDescription(const QString &label, const QString &value,
                                                         int maxLines, bool placeholder) {
  if (!m_host || !m_host->m_detailsLayout || value.trimmed().isEmpty()) {
    return;
  }
  // The description label mirrors the gallery section's "Media gallery"
  // header — same font weight, same padding, same color — so the two
  // section titles read as a matched pair.
  auto *labelWidget = new QLabel(label, m_host->m_detailsContainer);
  QFont labelFont = labelWidget->font();
  labelFont.setBold(true);
  labelWidget->setFont(labelFont);
  labelWidget->setStyleSheet("color: palette(windowtext); padding: 0px;");

  // The QScrollArea is the public widget — it owns the bubble background
  // (so the visual chrome matches the other "value" rows) and clips the
  // inner label as it animates. Disable user scrollbars; the auto-scroll
  // timer is the only thing that moves the viewport. The viewport is
  // forced transparent here so the bubble's background-color (applied
  // via the global stylesheet for sidebarRole="value") shows through.
  auto *scroll = new QScrollArea(m_host->m_detailsContainer);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setWidgetResizable(true);
  scroll->setProperty("sidebarRole", "value");
  scroll->viewport()->setAutoFillBackground(false);

  auto *valueWidget = new QLabel(value, scroll);
  valueWidget->setWordWrap(true);
  valueWidget->setTextInteractionFlags(Qt::TextSelectableByMouse);
  valueWidget->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  if (placeholder) {
    // Dimmed + italic stand-in text (Kartend-um69l). Concrete hex, not a
    // QSS palette() name: the stylesheet grammar has no placeholder-text
    // role, so snapshot the palette's dimmed colour the same way
    // appendDetailRow snapshots the accent.
    const QColor dim = valueWidget->palette().color(QPalette::PlaceholderText);
    valueWidget->setStyleSheet(QStringLiteral("color: %1; font-style: italic;"
                                              " background: transparent;")
                                   .arg(dim.name()));
  } else {
    valueWidget->setStyleSheet("color: palette(windowtext); background: transparent;");
  }
  scroll->setWidget(valueWidget);

  // Cap height to `maxLines` of wrapped text at the current font.
  // fontMetrics().lineSpacing() includes the leading; +4px slack covers
  // the inner padding the QScrollArea adds around its viewport.
  const int lineH = valueWidget->fontMetrics().lineSpacing();
  scroll->setFixedHeight(lineH * maxLines + 4);

  // The description block lives in m_detailsContainer's *outer* layout,
  // not in the metadata grid — that's what keeps it pinned in place
  // while the metadata card below marquees. Insert just before
  // m_metadataScroll so the visual order is preserved (description on
  // top, scrolling rows below).
  m_host->m_descriptionLabel = labelWidget;
  m_host->m_descriptionScroll = scroll;
  auto *containerLayout = qobject_cast<QVBoxLayout *>(m_host->m_detailsContainer->layout());
  if (containerLayout) {
    int insertIndex = containerLayout->indexOf(m_host->m_metadataScroll);
    if (insertIndex < 0) insertIndex = containerLayout->count();
    containerLayout->insertWidget(insertIndex, labelWidget);
    containerLayout->insertWidget(insertIndex + 1, scroll);
  }
  // Auto-scroll driver. Defer the start by one event-loop tick so the
  // QScrollArea has finished its first layout pass and `verticalScrollBar()
  // ->maximum()` reflects the wrapped text height (otherwise it's 0 and
  // the animator decides there's nothing to scroll).
  QTimer::singleShot(0, scroll, [scroll]() {
    auto *bar = scroll->verticalScrollBar();
    if (!bar || bar->maximum() <= 0) return; // content fits — no scroll needed
    auto *tick = new QTimer(scroll);
    // State for the marquee animation. One-way (top → bottom) scroll
    // at reading speed: every tick advances one pixel, the bottom
    // holds for a beat, then the position resets to 0 and the cycle
    // repeats. Reversing was confusing because the eye loses the line
    // it was reading mid-snap.
    struct State {
      int pauseTicks = 12; // initial pause at the top so the user has
                           // a beat to start reading before motion begins
    };
    auto *state = new State;
    QObject::connect(tick, &QTimer::destroyed, tick, [state]() { delete state; });
    QObject::connect(tick, &QTimer::timeout, scroll, [bar, state]() {
      if (state->pauseTicks > 0) {
        --state->pauseTicks;
        return;
      }
      if (bar->value() >= bar->maximum()) {
        // Reached the bottom — hold long enough to read the last
        // line, then snap back to the top and hold again before the
        // next scroll cycle starts.
        bar->setValue(0);
        state->pauseTicks = 25; // ~3s hold after the reset
        return;
      }
      bar->setValue(bar->value() + 1);
    });
    // 120ms/tick = ~1px every 0.12s. With a ~5-line viewport on a 10-line
    // synopsis this works out to roughly one screen of text every 8s,
    // comfortable reading pace without feeling sluggish.
    tick->start(120);
  });
}

void DetailsPaneMetadataView::setExtendedMetadata(const ItemMetadataStore::ItemMetadata &metadata) {
  if (!m_host) return;
  // Cache the canonical title so a tab switch back to Item can pick it up
  // without re-running the manager's selection pipeline. We do this even
  // on non-Item tabs because the cache is what lets applyTabVisibility()
  // restore the title later.
  m_host->m_currentMetadataTitle = metadata.title;

  // The Details container is shared with renderCollectionSummary on the
  // Collection tab — clearing/populating it from the per-item path would
  // wipe the just-built summary rows. Only the Item tab needs this body
  // to run; the File tab skips Details entirely (its content is the
  // file-info rows in the .ui).
  if (m_host->m_activeTab != DetailsPaneTab::Item) {
    return;
  }

  if (metadata.isEmpty()) {
    // Skeleton with placeholders (Kartend-um69l, decided with the user
    // 2026-08-23): an unscraped item keeps the same section shape as a
    // scraped one — Description plus the most common field rows, rendered
    // dimmed and italic — so it reads as the same pane with data pending
    // rather than a different, poorer view. The file-info rows stay below
    // the skeleton, same as the old fallback.
    ensureDetailsSection();
    if (m_host->m_detailsContainer) {
      clearDetailsSection();
      const QString dash = QStringLiteral("—");
      appendScrollingDescription(QObject::tr("Description"),
                                 QObject::tr("No description available"), /*maxLines=*/5,
                                 /*placeholder=*/true);
      appendDetailRow(QObject::tr("Genre"), dash, /*wrap=*/false, /*placeholder=*/true);
      appendDetailRow(QObject::tr("Developer"), dash, /*wrap=*/false, /*placeholder=*/true);
      appendDetailRow(QObject::tr("Release date"), dash, /*wrap=*/false, /*placeholder=*/true);
      m_host->applySidebarFont(m_host->m_activeSidebarFontFamily,
                               m_host->m_activeSidebarFontPointSize);
      m_host->m_detailsContainer->show();
    }
    m_host->setFileInfoRowsVisible(true);
    return;
  }
  // Scraped metadata wins the slot — hide the placeholder file-info rows.
  m_host->setFileInfoRowsVisible(false);

  ensureDetailsSection();
  if (!m_host->m_detailsContainer) {
    return;
  }
  clearDetailsSection();

  if (!metadata.title.isEmpty()) {
    m_host->ui->itemNameValue->setText(metadata.title);
  }

  appendScrollingDescription(QObject::tr("Description"), metadata.description, /*maxLines=*/5);
  appendDetailRow(QObject::tr("Genre"), metadata.genre);
  appendDetailRow(QObject::tr("Developer"), metadata.developer);
  appendDetailRow(QObject::tr("Publisher"), metadata.publisher);
  appendDetailRow(QObject::tr("Release date"), metadata.releaseDate);
  appendDetailRow(QObject::tr("Rating"), metadata.contentRating);
  appendDetailRow(QObject::tr("Players"), metadata.players);
  appendDetailRow(QObject::tr("Runtime"), DetailsFormat::formatRuntime(metadata.runtimeSeconds));
  appendDetailRow(QObject::tr("Tags"), DetailsFormat::formatTags(metadata.tags));
  // Personal curation fields. Labeled "Your rating" so it's distinct from
  // the scraper-provided content rating row above. Empty values are
  // suppressed inside appendDetailRow so unset fields don't clutter the
  // sidebar with blank rows.
  appendDetailRow(QObject::tr("Your rating"), DetailsFormat::formatPersonalRating(metadata.rating));
  // Per-item state flags — only render the row when set so unflagged items
  // don't get three blank lines pushed onto the sidebar. The chip text
  // matches the context-menu wording (Pin / Hide / Continue later) so the
  // visual + textual affordances stay consistent.
  QStringList stateChips;
  if (metadata.isPinned) {
    stateChips << QObject::tr("Pinned");
  }
  if (metadata.continueLater) {
    stateChips << QObject::tr("Continue later");
  }
  if (metadata.isHidden) {
    stateChips << QObject::tr("Hidden");
  }
  if (!stateChips.isEmpty()) {
    appendDetailRow(QObject::tr("State"), stateChips.join(QStringLiteral("  ·  ")));
  }
  appendDetailRow(QObject::tr("Source"), metadata.sourceUrl, /*wrap=*/true);
  appendDetailRow(QObject::tr("Notes"), metadata.notes, /*wrap=*/true);

  // User-defined custom fields. Rendered after the structured
  // fields so they appear as a contiguous block at the bottom of Details.
  // parseCustomFields() returns rows in alphabetical key order for stable
  // display regardless of edit history. wrap is left at the default so
  // appendDetailRow's width heuristic decides per row whether to pair
  // with a neighbour or take the full sidebar width.
  const auto customFields = ItemMetadataStore::parseCustomFields(metadata.customFields);
  for (const auto &pair : customFields) {
    appendDetailRow(pair.first, pair.second);
  }

  // re-apply the active sidebar-font override so the just-
  // appended detail rows pick up the same font as the static labels. The
  // override falls back to a no-op when no override is in effect.
  m_host->applySidebarFont(m_host->m_activeSidebarFontFamily, m_host->m_activeSidebarFontPointSize);

  // Unscraped items have nothing to put in the metadata grid. The
  // metadataBackdrop QFrame has an Expanding size policy so without
  // content it stretches to fill all remaining sidebar height and
  // paints its bubble background across the whole region — visible
  // as an oversized empty card under the artwork tile. Hide the
  // scroll wrapper (and the description widgets, if appendScrollingDescription
  // never built them) when there's nothing to show, so the layout
  // collapses cleanly. Restored automatically on the next
  // setExtendedMetadata call with non-empty content.
  const bool haveDescription = m_host->m_descriptionScroll != nullptr;
  const bool haveMetadataRows = m_host->m_detailsLayout && m_host->m_detailsLayout->count() > 0;
  if (m_host->m_metadataScroll) m_host->m_metadataScroll->setVisible(haveMetadataRows);
  if (!haveDescription && !haveMetadataRows && m_host->m_manualPath.isEmpty()) {
    // Nothing scraped, no manual link — hide the whole container so
    // it doesn't reserve layout space below the gallery.
    m_host->m_detailsContainer->hide();
  } else {
    m_host->m_detailsContainer->show();
  }
}

void DetailsPaneMetadataView::setUsageStats(const UsageStatsStore::ItemUsageStats &stats) {
  if (!m_host) return;
  // Item-tab only. Usage stats append to m_detailsContainer alongside
  // the extended-metadata rows; on Collection tab the same container
  // holds the summary, on File tab it's hidden — pushing rows there
  // would either clobber the summary or leak item data into a hidden box.
  if (m_host->m_activeTab != DetailsPaneTab::Item) {
    return;
  }
  // Tracking columns default to zero/empty for items that have never been
  // launched; treat them as "no rows to add" so the Details section stays
  // hidden on bare items.
  if (stats.isEmpty()) {
    return;
  }
  // The Details section may already be hidden if extended metadata was empty
  // — reveal it for usage rows alone so first-launched items still surface
  // play_count without scraper data.
  ensureDetailsSection();
  if (!m_host->m_detailsContainer) {
    return;
  }
  if (stats.playCount > 0) {
    appendDetailRow(QObject::tr("Play count"), QString::number(stats.playCount));
  }
  if (!stats.lastPlayed.isEmpty()) {
    appendDetailRow(QObject::tr("Last played"), UsageStatsStore::formatTimestamp(stats.lastPlayed));
  }
  if (stats.totalPlaySeconds > 0) {
    appendDetailRow(QObject::tr("Time played"),
                    UsageStatsStore::formatDuration(stats.totalPlaySeconds));
  }
  // same rationale as setExtendedMetadata — pull the new rows
  // under the active sidebar font.
  m_host->applySidebarFont(m_host->m_activeSidebarFontFamily, m_host->m_activeSidebarFontPointSize);
  // Usage stats just added rows to the grid, so the metadata scroll
  // has content again — reveal it. setExtendedMetadata may have
  // hidden it on an unscraped item.
  if (m_host->m_metadataScroll && m_host->m_detailsLayout && m_host->m_detailsLayout->count() > 0) {
    m_host->m_metadataScroll->show();
  }
  m_host->m_detailsContainer->show();
}

void DetailsPaneMetadataView::ensureManualButton() {
  // Manual button lives inside the Details container's outer layout. Build
  // the container on-demand so an item with only a manual (no extended
  // metadata) still gets a visible button.
  ensureDetailsSection();
}

void DetailsPaneMetadataView::setManualFile(const QString &manualPath) {
  if (!m_host) return;
  m_host->m_manualPath = manualPath;
  const bool hasManual = !manualPath.isEmpty();
  // Manual button is item-only chrome. On other tabs the button stays
  // hidden regardless of whether the underlying item has a manual.
  if (m_host->m_activeTab != DetailsPaneTab::Item) {
    if (m_host->m_manualButton) {
      m_host->m_manualButton->setVisible(false);
    }
    return;
  }
  if (hasManual) {
    ensureManualButton();
  }
  if (!m_host->m_manualButton) {
    return;
  }
  m_host->m_manualButton->setVisible(hasManual);
  if (hasManual) {
    m_host->m_manualButton->setToolTip(manualPath);
    if (m_host->m_detailsContainer) {
      m_host->m_detailsContainer->show();
    }
  } else {
    m_host->m_manualButton->setToolTip(QString());
    // Only hide the container when there are also no detail rows; a
    // populated row layout means setExtendedMetadata wants it visible.
    if (m_host->m_detailsContainer && m_host->m_detailsLayout &&
        m_host->m_detailsLayout->count() == 0) {
      m_host->m_detailsContainer->hide();
    }
  }
}

void DetailsPaneMetadataView::openCurrentManual() {
  if (!m_host || m_host->m_manualPath.isEmpty()) {
    return;
  }
  // QDesktopServices::openUrl wraps xdg-open on Linux / open on macOS /
  // ShellExecute on Windows, so the user's default handler for the file
  // type takes over (Okular for PDF, web browser for HTML, etc.).
  QDesktopServices::openUrl(QUrl::fromLocalFile(m_host->m_manualPath));
}
