#ifndef IUSERACTIVITYSINK_H
#define IUSERACTIVITYSINK_H

/**
 * @brief User-activity-notification role of the artwork layer
 * (Kartend-dl0uz.2).
 *
 * The one call EventManager makes into ArtworkManager: "the user did
 * something", which resets the idle tracking that gates silent background
 * artwork loading. IArtworkManager unions this role; the artwork pipeline
 * surface stays on IArtworkManager itself.
 *
 * Plain abstract class, not a QObject — ArtworkManager derives QObject
 * directly. Reached via ctx->userActivity().
 */
class IUserActivitySink {
public:
  virtual ~IUserActivitySink() = default;

  virtual void updateUserActivity() = 0;
};

#endif // IUSERACTIVITYSINK_H
