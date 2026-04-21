// Internal helpers shared between querymanager.cpp and
// querymanagerstatichelpers.cpp. Not part of the public API; do not include
// outside src/modules/query/.
#ifndef QUERYMANAGERHELPERS_H
#define QUERYMANAGERHELPERS_H

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

namespace QueryManagerInternal {

inline auto buildFtsPrefixQuery(const QString &raw) -> QString {
  QString trimmed = raw.trimmed();
  if (trimmed.isEmpty()) {
    return {};
  }

  // Sanitize into simple terms to avoid FTS query parser edge-cases.
  // Keep letters/numbers/underscore; replace everything else with spaces.
  QString cleaned = trimmed;
  cleaned.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}_]+")),
                  QStringLiteral(" "));
  const QStringList terms = cleaned.split(QLatin1Char(' '), Qt::SkipEmptyParts);
  if (terms.isEmpty()) {
    return {};
  }

  QStringList tokens;
  tokens.reserve(terms.size());
  for (const QString &t : terms) {
    if (t.isEmpty()) {
      continue;
    }
    tokens.append(t + QStringLiteral("*"));
  }
  if (tokens.isEmpty()) {
    return {};
  }
  return tokens.join(QStringLiteral(" AND "));
}

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
