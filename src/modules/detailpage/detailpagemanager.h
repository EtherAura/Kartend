#ifndef DETAILPAGEMANAGER_H
#define DETAILPAGEMANAGER_H

#include "setuputils.h"
#include <QObject>

class DatabaseManager;
class DetailPageOverlay;
class DetailsPaneManager;
struct ApplicationContext;

/**
 * @brief Coordinates the Kartend-uve item detail page.
 *
 * Owns no widget itself — the `DetailPageOverlay` is created by MainWindow
 * (parented to the central widget so it can cover the full window) and
 * handed in via setupReferences. The manager listens for the keyboard
 * "show details" request, pulls the resolved item context from
 * DetailsPaneManager, loads metadata + artwork + usage stats from the database,
 * and pushes the assembled payload to the overlay.
 */
struct DetailPageManagerSetup {
  ApplicationContext *ctx = nullptr;

  DetailPageOverlay *overlay = nullptr;
  DetailsPaneManager *detailsPaneManager = nullptr;
  DatabaseManager *databaseManager = nullptr;

  SETUP_GETTER_DECL(DetailsPaneManager *, DetailsPaneManager)
  SETUP_GETTER_DECL(DatabaseManager *, DatabaseManager)
};

class DetailPageManager : public QObject {
  Q_OBJECT
public:
  explicit DetailPageManager(QObject *parent = nullptr);
  ~DetailPageManager() override;

  void setupReferences(const DetailPageManagerSetup &setup);

  /// Builds the payload from the DetailsPaneManager's cached item context, loads
  /// the DB-backed extended metadata + artwork + usage rows, and shows the
  /// overlay. No-op if no item is currently selected (sidebar context is
  /// invalid).
  void showForCurrentSelection();

  /// Hides the overlay if active. Wired to navigation events that should
  /// dismiss the detail page (collection change, search, etc.).
  void hideOverlay();

  [[nodiscard]] DetailPageOverlay *overlay() const { return m_overlay; }

private:
  DetailPageOverlay *m_overlay = nullptr;
  DetailsPaneManager *m_detailsPaneManager = nullptr;
  DatabaseManager *m_databaseManager = nullptr;
};

#endif // DETAILPAGEMANAGER_H
