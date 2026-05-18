#ifndef EXTENSIONUTILS_H
#define EXTENSIONUTILS_H

#include <QStringList>

class ExtensionUtils {
public:
  [[nodiscard]] static QStringList parseUserExtensionList(const QString &text);
  [[nodiscard]] static QStringList normalizeStoredExtensions(const QStringList &raw);
  [[nodiscard]] static const QStringList &imageBaseExtensions();
  [[nodiscard]] static QStringList imageFilters();

  /// True when `path`'s extension is one Kartend will hand to an image
  /// decoder (QImageReader / QPixmap). Image-load sites MUST gate on this:
  /// a non-image file — notably a scraped `.pdf` manual — routed into Qt's
  /// image plugins reaches the PDFium-backed PDF plugin, which calls
  /// abort() on some inputs and takes down the whole process.
  [[nodiscard]] static bool isDecodableImagePath(const QString &path);
};

#endif