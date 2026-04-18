#ifndef EXTENSIONUTILS_H
#define EXTENSIONUTILS_H

#include <QStringList>

class ExtensionUtils {
public:
  [[nodiscard]] static QStringList parseUserExtensionList(const QString &text);
  [[nodiscard]] static QStringList
  normalizeStoredExtensions(const QStringList &raw);
  [[nodiscard]] static const QStringList &imageBaseExtensions();
  [[nodiscard]] static QStringList imageFilters();
};

#endif