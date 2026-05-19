#ifndef DETAILPAGEMANAGER_H
#define DETAILPAGEMANAGER_H

#include "idetailpagemanager.h"
#include "setuputils.h"
#include <QObject>

class DatabaseManager;
class DetailPageOverlay;
class DetailsPaneManager;
struct ApplicationContext;

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

  DetailPageOverlay *overlay = nullptr;
};

// QObject must be the first base; IDetailPageManager is a plain (non-QObject)
// role interface — single-QObject-base multiple inheritance.
class DetailPageManager : public QObject, public IDetailPageManager {
  Q_OBJECT
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
  /// dismiss the detail page (collection change, search, etc.).
  void hideOverlay() override;

  /// True while the detail-page overlay is showing.
  [[nodiscard]] bool isOverlayActive() const override;

private:
  // ctx is the single source of truth for sibling managers (DetailsPaneManager,
  // DatabaseManager) — never cache them as direct fields.
  const ApplicationContext *m_ctx = nullptr;
  DetailPageOverlay *m_overlay = nullptr;
};

#endif // DETAILPAGEMANAGER_H
