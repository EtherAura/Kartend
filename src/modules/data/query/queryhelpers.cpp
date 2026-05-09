#include "queryhelpers.h"

#include <QChar>
#include <QRegularExpression>
#include <QStringList>

namespace QueryHelpers {

auto buildFtsPrefixQuery(const QString &raw) -> QString {
  QString trimmed = raw.trimmed();
  if (trimmed.isEmpty()) {
    return {};
  }

  // Sanitize into simple terms to avoid FTS query parser edge-cases.
  // Keep letters/numbers/underscore; replace everything else with spaces.
  QString cleaned = trimmed;
  cleaned.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}_]+")), QStringLiteral(" "));
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

auto displayNameForBase(const QString &baseName) -> QString {
  return QString(baseName).replace(QLatin1Char('_'), QLatin1Char(' ')).simplified();
}

auto characterSortPriority(const QString &text) -> int {
  if (text.isEmpty()) {
    return 3;
  }

  QChar firstChar = text[0];
  if (firstChar == QLatin1Char('[') || firstChar == QLatin1Char('(')) {
    return 0;
  }
  if (firstChar == QLatin1Char('\'') && text.length() > 1 &&
      (text[1].isDigit() || text[1].isLetter())) {
    return text[1].isDigit() ? 2 : 3;
  }
  if (firstChar.isDigit()) {
    return 2;
  }
  if (firstChar.isLetter()) {
    return 3;
  }
  return 1;
}

auto orderByForSortMode(SortMode mode, bool useSortPrefix, bool withLimitOffset) -> QString {
  // sort_-prefixed aliases are produced by the deduplicating GROUP BY
  // queries (e.g. MIN(name) AS sort_name); bare names are direct table
  // columns. collection_uuid is never aliased — it's the grouping key.
  const QString name = useSortPrefix ? QStringLiteral("sort_name") : QStringLiteral("name");
  const QString lastMod =
      useSortPrefix ? QStringLiteral("sort_last_modified") : QStringLiteral("last_modified");
  const QString fileSize =
      useSortPrefix ? QStringLiteral("sort_file_size") : QStringLiteral("file_size");

  QString clause;
  switch (mode) {
  case SortMode::NameDescending:
    clause = QStringLiteral("ORDER BY %1 COLLATE NOCASE DESC").arg(name);
    break;
  case SortMode::DateDescending:
    clause = QStringLiteral("ORDER BY %1 DESC, %2 COLLATE NOCASE").arg(lastMod, name);
    break;
  case SortMode::DateAscending:
    clause = QStringLiteral("ORDER BY %1 ASC, %2 COLLATE NOCASE").arg(lastMod, name);
    break;
  case SortMode::SizeDescending:
    clause = QStringLiteral("ORDER BY %1 DESC, %2 COLLATE NOCASE").arg(fileSize, name);
    break;
  case SortMode::SizeAscending:
    clause = QStringLiteral("ORDER BY %1 ASC, %2 COLLATE NOCASE").arg(fileSize, name);
    break;
  case SortMode::CollectionAscending:
    clause = QStringLiteral("ORDER BY collection_uuid, %1 COLLATE NOCASE").arg(name);
    break;
  case SortMode::CollectionDescending:
    clause = QStringLiteral("ORDER BY collection_uuid DESC, %1 COLLATE NOCASE").arg(name);
    break;
  case SortMode::NameAscending:
  case SortMode::ArtworkFirst:
  case SortMode::ArtworkLast:
  case SortMode::Random:
  default:
    clause = QStringLiteral("ORDER BY %1 COLLATE NOCASE").arg(name);
    break;
  }

  if (withLimitOffset) {
    clause += QStringLiteral(" LIMIT ? OFFSET ?");
  }
  return clause;
}

auto placeholderList(int n) -> QString {
  if (n <= 0) {
    return {};
  }
  QString result;
  result.reserve(3 * n);
  for (int i = 0; i < n; ++i) {
    if (i > 0) {
      result += QStringLiteral(", ");
    }
    result += QChar('?');
  }
  return result;
}

} // namespace QueryHelpers
