#ifndef PATHUTILS_H
#define PATHUTILS_H

#include <QString>
#include <QStringList>

namespace PathUtils {

QString validateAndExpandPath(const QString &path,
                              const QString &collectionName = QString());
QString truncatePathForDisplay(const QString &path, int maxLength = 50);
QString normalizeDisplayName(const QString &input);

} // namespace PathUtils

#endif