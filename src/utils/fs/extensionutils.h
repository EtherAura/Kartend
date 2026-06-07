#ifndef EXTENSIONUTILS_H
#define EXTENSIONUTILS_H

#include <QStringList>

class QDir;

class ExtensionUtils {
public:
  [[nodiscard]] static QStringList parseUserExtensionList(const QString &text);
  [[nodiscard]] static QStringList normalizeStoredExtensions(const QStringList &raw);
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