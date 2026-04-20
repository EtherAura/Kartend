// Internal helpers shared between querymanager.cpp and
// querymanagerstatichelpers.cpp. Not part of the public API; do not include
// outside src/modules/query/.
#ifndef QUERYMANAGERHELPERS_H
#define QUERYMANAGERHELPERS_H

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QString>

namespace QueryManagerInternal {

inline auto canonicalKeyPath(const QString &absPath, bool dedup,
                             QHash<QString, QString> *canonicalPathCache)
    -> QString {
  if (!dedup) {
    return absPath;
  }

  if (canonicalPathCache) {
    auto it = canonicalPathCache->constFind(absPath);
    if (it != canonicalPathCache->constEnd()) {
      return it.value();
    }
  }

  QString canon = QFileInfo(absPath).canonicalFilePath();
  if (canon.isEmpty()) {
    canon = QDir::cleanPath(absPath);
  }

  if (canonicalPathCache) {
    canonicalPathCache->insert(absPath, canon);
  }
  return canon;
}

inline auto displayNameForBase(const QString &baseName) -> QString {
  return QString(baseName)
      .replace(QLatin1Char('_'), QLatin1Char(' '))
      .simplified();
}

template <typename Map, typename Key, typename Value>
inline void insertIfAbsent(Map &map, const Key &key, const Value &value) {
  if (map.find(key) == map.end()) {
    map.insert(key, value);
  }
}

} // namespace QueryManagerInternal

#endif // QUERYMANAGERHELPERS_H
