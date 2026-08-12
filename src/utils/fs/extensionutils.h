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

  /// The extension an image payload should be WRITTEN under, given the URL it
  /// came from and its bytes. Honours a recognised URL suffix; otherwise the
  /// bytes are content-sniffed, falling back to "png".
  ///
  /// Sniffing asks QImageReader rather than hand-rolling magic numbers: that
  /// is the same plugin set which decodes the file later, so a sniffed name is
  /// decodable by construction. The result is gated through
  /// imageBaseExtensions() so a plugin-recognised format the artwork lookup
  /// does not admit (svg, say) still lands on the default rather than an
  /// unresolvable name.
  ///
  /// Exists because CDNs serve images from extension-less URLs: Steam's store
  /// serves WebP bytes that way, and naming those ".png" gave a file whose
  /// name lied about its content (Kartend-aiws7). Shared so the scraper's
  /// media writer and the launcher-import cover fetch cannot drift on it
  /// (Kartend-g1g30).
  [[nodiscard]] static QString imageExtensionForBytes(const QString &urlPath,
                                                      const QByteArray &bytes);

  /// Canonical lowercase preview-video extensions (no dots), shared with the
  /// per-item video-lookup path.
  [[nodiscard]] static const QStringList &videoBaseExtensions();

  /// Canonical lowercase archive extensions (no dots). The single source of
  /// truth for "is this an archive" — LaunchManager::isArchiveFile and
  /// RomHasher::isArchivePath both delegate here so a file the launcher
  /// unpacks is also unpacked for scraper hash-ID.
  [[nodiscard]] static const QStringList &archiveBaseExtensions();

  /// True when `path` ends in one of archiveBaseExtensions() (case-
  /// insensitive suffix match, so "disc.tar.gz" matches via ".gz").
  [[nodiscard]] static bool isArchivePath(const QString &path);

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