#ifndef EXTENSIONUTILS_H
#define EXTENSIONUTILS_H

#include <QStringList>

class QDir;

class ExtensionUtils {
public:
  /// Canonical bare lowercase extension ("mp4") from any token form the
  /// INI / UI / kart import can carry ("*.MP4", ".mp4", " mp4 "). Empty
  /// when the token has no extension content.
  [[nodiscard]] static QString bareExtension(const QString &token);

  /// Parses a free-form user list ("mp4, *.mkv; .avi") into canonical bare
  /// extensions (deduplicated, lowercase, no dots or wildcards).
  [[nodiscard]] static QStringList parseUserExtensionList(const QString &text);

  /// Normalizes stored extension tokens to the ONE canonical in-memory and
  /// on-disk form: bare lowercase ("mp4"). Glob composition happens only at
  /// the QDir boundary via toNameFilters() — storing globs caused
  /// double-prefixed never-matching "*.*.mp4" filters (Kartend-693zb).
  [[nodiscard]] static QStringList normalizeStoredExtensions(const QStringList &raw);

  /// Composes QDir/QDirIterator name filters ("*.mp4") from stored
  /// extensions. Tolerant of non-canonical tokens ("*.mp4", ".mp4") so a
  /// stale glob can never double-prefix. Empty input yields an empty list
  /// (callers treat that as "match everything").
  [[nodiscard]] static QStringList toNameFilters(const QStringList &extensions);
  [[nodiscard]] static const QStringList &imageBaseExtensions();
  [[nodiscard]] static QStringList imageFilters();

  /// Canonical lowercase preview-video extensions (no dots), shared with the
  /// per-item video-lookup path.
  [[nodiscard]] static const QStringList &videoBaseExtensions();

  /// Probe @p dir for "<baseName>.<ext>" across @p extensions, each tried in
  /// lowercase then uppercase; returns the first existing path, else empty.
  /// Shared by artwork and preview-video file lookup.
  [[nodiscard]] static QString findFileWithExtensions(const QDir &dir, const QString &baseName,
                                                      const QStringList &extensions);

  /// True when `path`'s extension is one Kartend will hand to an image
  /// decoder (QImageReader / QPixmap). Image-load sites MUST gate on this:
  /// a non-image file — notably a scraped `.pdf` manual — routed into Qt's
  /// image plugins reaches the PDFium-backed PDF plugin, which calls
  /// abort() on some inputs and takes down the whole process.
  [[nodiscard]] static bool isDecodableImagePath(const QString &path);
};

#endif