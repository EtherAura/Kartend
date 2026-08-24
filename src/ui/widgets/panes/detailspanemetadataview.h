#ifndef DETAILSPANEMETADATAVIEW_H
#define DETAILSPANEMETADATAVIEW_H

#include "itemmetadata.h"
#include "usagestatsstore.h"

#include <QObject>
#include <QString>

class DetailsPane;

/// Owns the dynamically-built "Details" section of DetailsPane: the
/// description scroll tile, the two-column metadata grid (genre /
/// players / runtime / etc.), the optional usage-stats rows, and the
/// Manual button. Lazy-builds the container on the first
/// ensureDetailsSection() call and reuses the existing widgets on
/// subsequent rebuilds.
///
/// Coupling: takes the host DetailsPane via setHost so the helper can
/// reach the shared widgets the section anchors against (ui->contentWidget,
/// ui->itemNameValue) and the section-state members on the host
/// (m_detailsContainer, m_descriptionLabel/Scroll, m_metadataBackdrop,
/// m_metadataScroll, m_metadataAutoScrollTimer, m_detailsLayout,
/// m_manualButton, m_manualPath). Same friend-of-host pattern as
/// DetailsPaneArtwork + DetailsPaneGalleryView — the state stays on the
/// host so the ~100 existing access sites in sibling .cpp files are
/// unchanged (Kartend-cd2u / Kartend-5nxz follow-up).
class DetailsPaneMetadataView : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(DetailsPaneMetadataView)

public:
  explicit DetailsPaneMetadataView(QObject *parent = nullptr);

  /// Bind to the owning DetailsPane. Idempotent — call once during
  /// DetailsPane's setup.
  void setHost(DetailsPane *host);

  /// Lazily construct the "Details" section. Appended once to the bottom
  /// of the content layout; subsequent calls reuse the existing widgets
  /// so the layout doesn't churn on every selection change.
  void ensureDetailsSection();
  /// Tear down the per-selection rows + description while keeping the
  /// outer container intact. Reset the grid cursor + auto-scroll timer
  /// so the next rebuild starts from a known state.
  void clearDetailsSection();
  /// Append a label / value pair to the metadata grid. Short pairs
  /// share a row (label in col 0, value in col 1); long values or
  /// wrap=true rows span both columns so they get the full sidebar
  /// width. @p placeholder renders the value dimmed and italic (the
  /// palette's PlaceholderText colour, so it tracks the theme) — used by
  /// the unscraped-item skeleton so a stand-in row cannot be mistaken
  /// for scraped data (Kartend-um69l).
  void appendDetailRow(const QString &label, const QString &value, bool wrap = false,
                       bool placeholder = false, bool linkify = false);
  /// Like appendDetailRow but renders the value inside a fixed-height
  /// scroll area that auto-scrolls vertically when the text overflows.
  /// Used for the description field so long synopses don't push the
  /// rest of the metadata below the scroll fold. Caps the visible
  /// region to ~maxLines of wrapped text at the current font.
  /// @p placeholder dims + italicises the text like appendDetailRow's.
  void appendScrollingDescription(const QString &label, const QString &value, int maxLines,
                                  bool placeholder = false);

  /// Render extended metadata into the Details section. Hides the
  /// section entirely when @p metadata.isEmpty() so empty rows do not
  /// clutter the UI.
  void setExtendedMetadata(const ItemMetadataStore::ItemMetadata &metadata);
  /// Render per-item usage statistics appended to the Details section:
  /// play count, last played, total time played. Empty rows are
  /// skipped so an item that has never been launched shows nothing.
  void setUsageStats(const UsageStatsStore::ItemUsageStats &stats);
  /// Set the resolved manual file path. Non-empty paths surface the
  /// Manual button at the top of the section; empty hides it.
  void setManualFile(const QString &manualPath);

private:
  void ensureManualButton();
  void openCurrentManual();

  DetailsPane *m_host = nullptr;
};

#endif // DETAILSPANEMETADATAVIEW_H
