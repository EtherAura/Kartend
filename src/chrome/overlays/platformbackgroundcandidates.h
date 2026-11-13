#ifndef PLATFORMBACKGROUNDCANDIDATES_H
#define PLATFORMBACKGROUNDCANDIDATES_H

#include <QDir>
#include <QFileInfo>
#include <QList>
#include <QString>
#include <QStringList>

#include "artworkcandidates.h"

/// Read-only source of the platform-background candidates a platform scrape
/// retained on disk (Kartend-ji94z). A platform scrape writes every still image
/// the catalogue advertises to `_shared/<type>/platform_<systemeid>.<ext>` but
/// wires only ONE into the collection background; this enumerates the rest so a
/// later picker can feed them to ArtworkPreviewOverlay::setGalleryEntries
/// (label = type, path = path, isVideo = false).
///
/// Foundation slice only: nothing calls this yet, and it never decodes images,
/// builds widgets, writes files or touches config. Pure filesystem listing with
/// no shared state, so it is safe to call from any thread; the returned values
/// own their strings and outlive the root they were read from.
namespace PlatformBackgroundCandidates {

struct Candidate {
  /// The `_shared/<type>/` directory name — one of backgroundTypes().
  QString type;
  /// Absolute-or-root-relative path of the existing file, as built from the
  /// shared root passed in.
  QString path;

  bool operator==(const Candidate &other) const { return type == other.type && path == other.path; }
};

/// The platform media types that carry EntityArtRole::Background, in the
/// rolePriority order ScreenScraperProvider's kPlatformMediaTypes declares
/// (illustration, photo, background). Mirrored here rather than included: the
/// provider lives in modules/data, above chrome/ in the layering graph. Logos,
/// SVG logo variants, controller art and templates are deliberately absent.
[[nodiscard]] inline const QStringList &backgroundTypes() {
  static const QStringList types = {
      QStringLiteral("illustration"),
      QStringLiteral("photo"),
      QStringLiteral("background"),
  };
  return types;
}

/// Every retained background still image for ONE platform under `sharedRoot`
/// (the collection art directory's `_shared` folder). `systemId` is the
/// ScreenScraper systemeid; the filename prefix matches
/// Scraper::sharedScopePrefix(MediaScope::Platform). Ordered by
/// backgroundTypes() priority, then file name, so repeated calls agree.
/// Extensions are filtered through ArtworkCandidates::imageExtensions().
/// A missing/empty root, a non-numeric id or a platform with no art yields an
/// empty list.
[[nodiscard]] inline QList<Candidate> enumerate(const QString &sharedRoot,
                                                const QString &systemId) {
  QList<Candidate> out;
  if (sharedRoot.isEmpty() || systemId.isEmpty()) return out;
  // systemeids are numeric; refusing anything else also keeps a stray '*' or
  // path separator from turning into a wildcard or another directory.
  for (const QChar c : systemId) {
    if (!c.isDigit()) return out;
  }
  const QDir root(sharedRoot);
  if (!root.exists()) return out;
  const QString baseName = QStringLiteral("platform_") + systemId;
  for (const QString &type : backgroundTypes()) {
    const QDir typeDir(root.filePath(type));
    if (!typeDir.exists()) continue;
    const QStringList files =
        typeDir.entryList({baseName + QStringLiteral(".*")}, QDir::Files, QDir::Name);
    for (const QString &file : files) {
      // The name filter already pins the prefix + '.'; completeBaseName also
      // rejects a multi-dot name like platform_1.old.png.
      const QFileInfo info(file);
      if (info.completeBaseName() != baseName) continue;
      if (!ArtworkCandidates::looksLikeImage(file)) continue;
      out.append(Candidate{type, typeDir.filePath(file)});
    }
  }
  return out;
}

} // namespace PlatformBackgroundCandidates

#endif // PLATFORMBACKGROUNDCANDIDATES_H
