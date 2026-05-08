#ifndef FILTERHELPERS_H
#define FILTERHELPERS_H

#include <QHash>
#include <QList>
#include <QString>

// Pure helpers extracted from FilterManager so visual↔actual index mapping
// and display-name resolution can be unit-tested without instantiating
// FilterManager (which depends on DatabaseManager and the collection graph).
//
// See bd for the broader extraction effort.
namespace FilterHelpers {

// Maps a filtered "visual" index to the underlying actual item index.
//
// - When isFiltered is false, returns visualIndex unchanged (passthrough).
// - When isFiltered is true, returns filteredIndices[visualIndex] or -1 if
//   the index is out of range.
[[nodiscard]] auto mapVisualToActualIndex(int visualIndex, bool isFiltered,
                                          const QList<int> &filteredIndices) -> int;

// Case-insensitive substring search for a subcollection name.
//
// Returns true if subcollectionName contains needle (case-insensitive). needle
// can be any case — the comparison is Qt::CaseInsensitive so callers don't
// need to pre-lower it (and shouldn't, since per-item lowering allocates a
// fresh QString per match in hot filter loops).
[[nodiscard]] auto subcollectionNameMatches(const QString &subcollectionName,
                                            const QString &needle) -> bool;

// Resolves the display name for a media-item raw entry, mirroring
// FilterManager::getDisplayNameForMediaItem semantics.
//
// Resolution order:
// 1. If showAllSubcollectionItems is true:
//    a. Look up in filePathToDisplayName (if non-null and a non-empty value
//       is found, return it).
//    b. Otherwise fall back to QFileInfo(rawEntry).completeBaseName().
// 2. Otherwise (per-collection mode):
//    a. If mediaDirectory (trimmed) is empty, return basename.
//    b. Otherwise build the absolute path mediaDirectory/rawEntry and look it
//       up in fileNames; if absent, fall back to basename.
//
// Either map pointer may be null; basename is the safe fallback.
[[nodiscard]] auto displayNameForMediaEntry(const QString &rawEntry, bool showAllSubcollectionItems,
                                            const QString &mediaDirectory,
                                            const QHash<QString, QString> *filePathToDisplayName,
                                            const QHash<QString, QString> *fileNames) -> QString;

} // namespace FilterHelpers

#endif // FILTERHELPERS_H
