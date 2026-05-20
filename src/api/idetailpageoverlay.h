#ifndef IDETAILPAGEOVERLAY_H
#define IDETAILPAGEOVERLAY_H

#include <QList>
#include <QString>

#include "itemmetadata.h"
#include "usagestatsstore.h"

/**
 * @brief Neutral role interface to the full-window detail-page overlay.
 *
 * Lets `DetailPageManager` (data/media-layer manager) populate and drive
 * the overlay without #including the concrete `DetailPageOverlay` header,
 * which lives in `src/ui/widgets/overlays/` and would otherwise be a
 * `media -> ui` upward edge in the layered DAG. The `DetailPageOverlay`
 * widget inherits this interface and implements the slots / accessors;
 * MainWindow constructs the overlay and hands the pointer to the manager
 * through `DetailPageManagerSetup::overlay`.
 *
 * Kartend-n8kh: the `Payload`/`ArtworkEntry` value types previously
 * nested inside `DetailPageOverlay` are hoisted here so they can be
 * built in the data layer (`DetailPageManager::showForCurrentSelection`)
 * without that file pulling the overlay's QWidget header along for the
 * ride.
 *
 * Plain abstract class, not a QObject: `DetailPageOverlay` already
 * derives QWidget (its single QObject base) and picks this up as a
 * further non-QObject base. The `manualRequested` / `visibilityChanged`
 * signals stay on the concrete widget — connections are made by the
 * UI-layer wiring in MainWindow, not by callers of this interface.
 */
class IDetailPageOverlay {
public:
  virtual ~IDetailPageOverlay() = default;

  /// One artwork tile available for the hero pane. `label` is the human-
  /// readable type name; `path` is the resolved on-disk file. The list is
  /// rendered in order with Left/Right cycling between entries.
  struct ArtworkEntry {
    QString label;
    QString path;
    bool isVideo = false;
  };

  /// Bundle of everything the overlay displays. Populated by
  /// `DetailPageManager` from `DatabaseManager` + filesystem before show().
  struct Payload {
    QString filePath;
    QString itemName; // Display title (scraped title overrides file stem)
    ItemMetadataStore::ItemMetadata metadata;
    UsageStatsStore::ItemUsageStats usage;
    QString manualPath; // Resolved manual file (empty hides the button)
    QList<ArtworkEntry> artwork;
    qint64 fileSize = -1;  // Bytes; negative means "unknown"
    QString fileModified;  // Pre-formatted modification timestamp
    QString fileExtension; // ".sfc" etc., already lower-cased
  };

  /// Show the overlay populated with @p payload. Replaces any currently-
  /// displayed content.
  virtual void showWith(const Payload &payload) = 0;

  /// Hide the overlay if active. No-op otherwise.
  virtual void hideOverlay() = 0;

  /// True while the overlay is showing.
  [[nodiscard]] virtual bool isActive() const = 0;
};

#endif // IDETAILPAGEOVERLAY_H
