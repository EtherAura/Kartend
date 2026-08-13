#ifndef KARTEND_UTILS_FS_MULTIDISC_H
#define KARTEND_UTILS_FS_MULTIDISC_H

#include <QList>
#include <QString>
#include <QStringList>

/// Multi-disc release detection: the pure string/grouping half of "several
/// files on disk are one item in the library". A release split across discs
/// ("Recital (Disc 1).flac", "Recital (Disc 2).flac") is one thing a person
/// owns, and browsing it as N tiles that each play a fragment is wrong for
/// audio, video and games alike.
///
/// Everything here is pure — no disk access, no QFileInfo probing beyond
/// string splitting — so it is unit-testable without a filesystem and safe to
/// call from a scan worker.
///
/// Deliberately NOT reusing DatAudit::Region::discQualifier(): that one serves
/// No-Intro/Redump catalogue titles and recognises disc/disk/side only.
/// Widening it to cover "(CD 2)" — which real rips use constantly — would
/// silently change which catalogue entries 1G1R collapses, and an audit's
/// completeness numbers are not something to alter as a side effect of a
/// library feature. The two vocabularies are related but answer to different
/// masters; keep them apart.
namespace MultiDisc {

/// A disc marker parsed out of a file name.
struct Marker {
  /// The name with the disc tag (and its surrounding brackets) removed, and
  /// with the extension stripped. "Recital (Disc 2).flac" -> "Recital".
  QString base;
  /// Normalised lowercase qualifier: "disc 2", "cd 1", "side a". Empty when
  /// the name carries no disc marker.
  QString qualifier;
  /// Sort position within the release. Numeric markers order by value so
  /// "disc 10" follows "disc 9" rather than "disc 1"; letter markers order
  /// alphabetically after any numbered ones.
  int order = 0;

  [[nodiscard]] bool isValid() const { return !qualifier.isEmpty(); }
};

/// Parse the disc marker out of a file NAME (not a full path). Returns an
/// invalid Marker when the name has no recognisable disc tag.
///
/// Recognised inside a parenthesised or bracketed tag group, case-insensitive,
/// with or without a space: (Disc 1) (disk 2) (CD3) [Side A]. "Disk" folds to
/// "disc" so both spellings of one release group together.
[[nodiscard]] Marker parse(const QString &fileName);

/// One multi-disc release.
struct Group {
  /// Shared base title — what the library should show as the item's name.
  QString base;
  /// Absolute paths of the member files, in disc order.
  QStringList files;
};

/// Group absolute file paths into multi-disc releases.
///
/// Only genuine releases come back: a base title with a single disc file is
/// NOT a group, because collapsing it would rename a standalone file for no
/// benefit and hide its disc tag. Files with no disc marker are ignored
/// entirely. Grouping is scoped per directory, so identically-named releases
/// filed under different folders stay separate.
[[nodiscard]] QList<Group> group(const QStringList &absolutePaths);

/// Body of an .m3u playlist listing `absolutePaths` in the given order, one
/// per line with LF endings and a trailing newline.
///
/// A path that lives directly under `m3uDir` is written relative, so moving a
/// release's folder does not break the playlist; anything else is written
/// absolute. `m3uDir` may be empty, which forces absolute paths.
[[nodiscard]] QString buildM3uContents(const QStringList &absolutePaths, const QString &m3uDir);

/// File name an .m3u for `base` should take — the base title plus ".m3u",
/// with characters that are illegal or awkward in a file name replaced by
/// underscores. Never returns an empty string.
[[nodiscard]] QString m3uFileNameFor(const QString &base);

} // namespace MultiDisc

#endif // KARTEND_UTILS_FS_MULTIDISC_H
