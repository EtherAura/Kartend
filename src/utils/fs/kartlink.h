#ifndef KARTEND_UTILS_FS_KARTLINK_H
#define KARTEND_UTILS_FS_KARTLINK_H

#include "errorutils.h"
#include <QString>

/// .kartlink shortcut stubs — tiny JSON files standing in for games that are
/// installed through an external launcher (Steam / Flatpak / Lutris) and so
/// have no media file of their own. LauncherImportService writes one stub per
/// installed game into a Kartend-managed folder; the collection then scans,
/// watches, titles, and art-matches them exactly like ordinary media files
/// (the grid title and artwork lookup key off the stub's completeBaseName()).
/// At launch time LaunchCommandBuilder substitutes the stub's `target`
/// (steam:// URI, Flatpak app id, lutris: URI, …) into the collection's
/// launcher template in place of the stub's own path — that indirection is
/// the whole reason the format exists.
///
/// On-disk shape (version 1):
///   {"version":1,"source":"steam","target":"steam://rungameid/220",
///    "title":"Half-Life 2"}
/// `title` is informational (the filename already carries the display title);
/// `source` names the importer that owns the stub so a sync pass never
/// deletes a stub it didn't write.
namespace KartLink {

/// Bare lowercase extension, no dot — the same form CollectionConfig's
/// `extensions` list stores.
inline constexpr auto kExtension = "kartlink";

struct LinkData {
  int version = 1;
  QString source; ///< Importer id: "steam" / "flatpak" / "lutris".
  QString target; ///< What the launcher receives instead of the stub path.
  QString title;  ///< Informational; display titles come from the filename.

  bool operator==(const LinkData &other) const {
    return version == other.version && source == other.source && target == other.target &&
           title == other.title;
  }
};

/// True iff the path's suffix is `kartlink` (case-insensitive). Cheap string
/// test — no disk access — safe on the launch hot path.
[[nodiscard]] bool isKartLinkPath(const QString &filePath);

/// Reads and parses a stub. Errors: FileReadError when the file can't be
/// opened; a malformed JSON body or an empty `target` reports
/// InvalidConfigValue with the offending path in the details.
[[nodiscard]] ErrorUtils::Result<LinkData> read(const QString &filePath);

/// Serialises `data` and writes it durably via PathUtils::atomicWriteFile
/// (parent dir created, QSaveFile + directory fsync). Returns false after
/// the helper has logged the failing stage.
[[nodiscard]] bool write(const QString &filePath, const LinkData &data);

} // namespace KartLink

#endif
