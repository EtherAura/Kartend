// Categorizes file extensions by media type (ROM, disc image, archive, etc.).
#include "extensionutils.h"
#include <QSet>
#include <QString>

auto ExtensionUtils::normalizeStoredExtensions(const QStringList &raw) -> QStringList {
  QStringList out;
  QSet<QString> seen;
  out.reserve(raw.size());
  for (QString token : raw) {
    token = token.trimmed().toLower();
    if (token.isEmpty()) {
      continue;
    }
    if (token.startsWith("*.")) {
      // keep
    } else if (token.startsWith('.')) {
      token = "*" + token;
    } else {
      token = "*." + token;
    }
    if (!seen.contains(token)) {
      out.append(token);
      seen.insert(token);
    }
  }
  return out;
}

// Parse a free-form extension list into normalized, deduplicated patterns
auto ExtensionUtils::parseUserExtensionList(const QString &text) -> QStringList {
  QString normalized = text;
  normalized.replace(';', ',');
  normalized.replace('\t', ',');
  for (QChar &character : normalized) {
    if (character.isSpace()) {
      character = ',';
    }
  }
  QStringList tokens = normalized.split(',', Qt::SkipEmptyParts);
  return normalizeStoredExtensions(tokens);
}

// Returns a canonical list of lowercase image extensions without dots or
// wildcards
auto ExtensionUtils::imageBaseExtensions() -> const QStringList & {
  static const QStringList exts = {QStringLiteral("png"), QStringLiteral("jpg"),
                                   QStringLiteral("jpeg"), QStringLiteral("bmp"),
                                   QStringLiteral("gif")};
  return exts;
}

// Returns "*.ext" filters suitable for QDir name filters
auto ExtensionUtils::imageFilters() -> QStringList {
  QStringList filters;
  filters.reserve(imageBaseExtensions().size());
  for (const QString &ext : imageBaseExtensions()) {
    filters.append(QStringLiteral("*.") + ext);
  }
  return filters;
}