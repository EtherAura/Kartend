#ifndef IARTWORKMANAGER_H
#define IARTWORKMANAGER_H

#include <QString>

class ItemWidget;
namespace TimerUtils {
class Coordinator;
}

/**
 * @brief Sibling-facing interface to the artwork pipeline.
 *
 * ArtworkManager has a wide (~35-method) surface, most of which is
 * lifecycle / configuration wiring its owner (ApplicationManager /
 * MainWindow) drives through the concrete class. This is a deliberate
 * *role interface* (interface-segregation): only the slice sibling
 * managers reach for through ApplicationContext. The owner keeps a
 * concrete ArtworkManager*; ctx hands siblings an IArtworkManager*.
 *
 * Plain abstract class, not a QObject — ArtworkManager derives QObject
 * directly; see its multiple-inheritance declaration. Add a method here
 * only when a sibling genuinely needs to call it via ctx.
 */
class IArtworkManager {
public:
  virtual ~IArtworkManager() = default;

  virtual void cancelAllArtworkLoading() = 0;
  virtual void clearWidgetReferences() = 0;
  virtual void clearPendingArtworkForWidget(ItemWidget *widget) = 0;
  virtual void clearLoadedArtworkState() = 0;
  virtual void updateViewportArtwork() = 0;
  virtual void scheduleViewportUpdate() = 0;
  virtual void updateUserActivity() = 0;
  virtual void stopSilentLoading() = 0;
  virtual void startEarlyDentryPrewarm(int collectionIndex) = 0;
  virtual void addPendingArtwork(ItemWidget *widget, const QString &artworkPath) = 0;
  virtual void cycleArtworkType(ItemWidget *widget, const QString &fullPath,
                                int collectionIndex) = 0;
  [[nodiscard]] virtual QString artworkTypeOverrideFor(const QString &fullPath) const = 0;
  [[nodiscard]] virtual bool hasArtworkForWidget(ItemWidget *widget) const = 0;
  [[nodiscard]] virtual TimerUtils::Coordinator *getTimerCoordinator() const = 0;
};

#endif // IARTWORKMANAGER_H
