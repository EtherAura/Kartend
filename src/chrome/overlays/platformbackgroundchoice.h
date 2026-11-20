#ifndef PLATFORMBACKGROUNDCHOICE_H
#define PLATFORMBACKGROUNDCHOICE_H

#include <optional>
#include <QList>
#include <QString>

#include "collection/collectionbackground.h"
#include "platformbackgroundcandidates.h"

/// Turn a picked platform-background candidate into the collection's explicit
/// wallpaper choice (Kartend-ji94z). Pure value operation: it takes the target
/// collection's current CollectionBackground and returns a modified copy, never
/// touching the input; it reads no files, holds no shared state and outlives
/// no widget, so it is safe to call from anywhere.
///
/// Caller contract (the later gallery/picker): enumerate a FRESH candidate list
/// with PlatformBackgroundCandidates::enumerate() for the intended collection's
/// platform at selection time, pass the path the user picked, and apply a
/// returned value through the normal collection settings update/save path.
/// Nothing here writes config or decides where art lives.
namespace PlatformBackgroundChoice {

/// Proposed update for picking `selectedPath` out of `candidates`.
///
/// Returns a value only when `selectedPath` is nonempty and exactly equals the
/// `path` of one entry in `candidates`; matching is by whole string, never by
/// extension, prefix or file name, so a stale pick, a path from another
/// platform's list or a cancelled (empty) selection yields std::nullopt.
///
/// On success the result is `current` with backgroundType = Image and both
/// backgroundImage and backgroundImageChosenPath set to `selectedPath`; every
/// other field (colours, header logo, video path, vignette, parallax, blur) is
/// kept. Re-picking the already displayed but unmarked image still sets the
/// marker, so the next scrape treats it as user-owned.
[[nodiscard]] inline std::optional<CollectionBackground>
choose(const CollectionBackground &current,
       const QList<PlatformBackgroundCandidates::Candidate> &candidates,
       const QString &selectedPath) {
  if (selectedPath.isEmpty()) return std::nullopt;
  bool listed = false;
  for (const PlatformBackgroundCandidates::Candidate &candidate : candidates) {
    if (candidate.path == selectedPath) {
      listed = true;
      break;
    }
  }
  if (!listed) return std::nullopt;
  CollectionBackground updated = current;
  updated.backgroundType = BackgroundType::Image;
  updated.backgroundImage = selectedPath;
  updated.backgroundImageChosenPath = selectedPath;
  return updated;
}

} // namespace PlatformBackgroundChoice

#endif // PLATFORMBACKGROUNDCHOICE_H
