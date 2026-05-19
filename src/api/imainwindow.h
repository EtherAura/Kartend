#ifndef IMAINWINDOW_H
#define IMAINWINDOW_H

#include "collectionutils.h"
#include <QList>
#include <QString>

/**
 * @brief Neutral role interface to the application's main window.
 *
 * Lets lower-layer coordinators (data-layer SettingsManager /
 * SettingsDialogController, input-layer InteractionManager) reach the
 * handful of MainWindow operations they need without #including the
 * concrete src/core/mainwindow.h — which would pull a data->core /
 * input->core edge and break the layered DAG.
 *
 * Plain abstract class, not a QObject: MainWindow already derives QMainWindow
 * (its single QObject base), so it picks this up as a second, non-QObject
 * base. Cross-cast to it with dynamic_cast, not qobject_cast.
 *
 * Add a method here only when a lower-layer module genuinely needs to call
 * it on the main window.
 */
class IMainWindow {
public:
  virtual ~IMainWindow() = default;

  /// The live collection list the main window owns. SettingsManager reads
  /// it to resolve a collection's persistent last-selected index.
  [[nodiscard]] virtual const QList<CollectionConfig> &collections() const = 0;

  /// Refresh the window title for the given collection after a settings save.
  virtual void updateWindowTitleForCollection(int collectionIndex) = 0;

  /// Open the unified scraper dialog, optionally pre-targeted at one
  /// collection / item. InteractionManager's right-click "Scraper…" entry
  /// routes here so it need not #include the concrete MainWindow.
  virtual void openScraperDialog(int preCollectionIndex = -1,
                                 const QString &preItemPath = QString()) = 0;
};

#endif // IMAINWINDOW_H
