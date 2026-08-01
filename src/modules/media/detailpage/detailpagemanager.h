#ifndef DETAILPAGEMANAGER_H
#define DETAILPAGEMANAGER_H

#include "idetailpagemanager.h"
#include "idetailpageoverlay.h"
#include "itemdetaildata.h"
#include "setuputils.h"
#include <QObject>
#include <QString>

class DatabaseManager;
class DetailsPaneManager;
#include "applicationcontext_fwd.h"

/**
 * @brief Coordinates the item detail page.
 *
 * Owns no widget itself — the `DetailPageOverlay` is created by MainWindow
 * (parented to the central widget so it can cover the full window) and
 * handed in via setupReferences. The manager listens for the keyboard
 * "show details" request, pulls the resolved item context from
 * DetailsPaneManager, loads metadata + artwork + usage stats from the database,
 * and pushes the assembled payload to the overlay.
 */
struct DetailPageManagerSetup {
  const ApplicationContext *ctx = nullptr;

  /// Neutral pointer to the overlay (Kartend-n8kh). The concrete
  /// `DetailPageOverlay` widget lives in src/ui/ and implements
  /// `IDetailPageOverlay`; MainWindow constructs it and hands the
  /// already-wired instance in here.
  IDetailPageOverlay *overlay = nullptr;
};

// QObject must be the first base; IDetailPageManager is a plain (non-QObject)
// role interface — single-QObject-base multiple inheritance.
class DetailPageManager : public QObject, public IDetailPageManager {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(DetailPageManager)
public:
  explicit DetailPageManager(QObject *parent = nullptr);
  ~DetailPageManager() override;

  void setupReferences(const DetailPageManagerSetup &setup);

  /// Builds the payload from the DetailsPaneManager's cached item context, loads
  /// the DB-backed extended metadata + artwork + usage rows, and shows the
  /// overlay. No-op if no item is currently selected (sidebar context is
  /// invalid).
  void showForCurrentSelection() override;

  /// Hides the overlay if active. Wired to navigation events that should
  /// dismiss the detail page (collection change, search, etc.). Also
  /// invalidates the in-flight load token so a late async result cannot
  /// re-show the dismissed overlay.
  void hideOverlay() override;

  /// True while the detail-page overlay is showing.
  [[nodiscard]] bool isOverlayActive() const override;

private slots:
  /// Kartend-4p8o: receives the off-thread detail-page load. Drops a result
  /// whose token no longer matches the latest request (the user opened another
  /// item meanwhile), otherwise maps it into the overlay Payload and refreshes
  /// the already-shown overlay.
  void onItemDetailLoaded(const ItemDetailData &data, int requestToken);

private:
  // ctx is the single source of truth for sibling managers (DetailsPaneManager,
  // DatabaseManager) — never cache them as direct fields.
  const ApplicationContext *m_ctx = nullptr;
  IDetailPageOverlay *m_overlay = nullptr;
  // Kartend-4p8o: monotonically increasing per-open token; an async result is
  // applied only when it still matches the latest request.
  int m_detailLoadToken = 0;
  // Cheap selection context for the in-flight load. filePath/itemName aren't
  // part of ItemDetailData, so stash them to assemble the final Payload when
  // the worker result lands.
  QString m_pendingFilePath;
  QString m_pendingItemName;
};

#endif // DETAILPAGEMANAGER_H
