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

} // namespace QueryHelpers
