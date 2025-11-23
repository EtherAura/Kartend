#ifndef PATHUTILS_H
#define PATHUTILS_H

#include <QString>
#include <QStringList>

class PathUtils {
public:
  static QString
  validateAndExpandPath(const QString &path,
                        const QString &collectionName = QString());
  static QString truncatePathForDisplay(const QString &path,
                                        int maxLength = 50);
  static QString normalizeDisplayName(const QString &input);

private:
  PathUtils() = delete;
};

#endif