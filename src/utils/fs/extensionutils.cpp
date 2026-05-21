// Categorizes file extensions by media type (ROM, disc image, archive, etc.).
#include "extensionutils.h"
#include <QByteArray>
#include <QFileInfo>
#include <QImageReader>
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
// wildcards. webp is unconditional — qt6-imageformats is a hard dep and
// ships libqwebp. avif is gated on QImageReader's reported plugin set so
// a runner without libqavif doesn't admit undecodable paths into the
// allowlist (degrades to null-QImage rather than crashing, but cleaner to
// reject at the seam). The result is cached in the static — first call
// pays the QImageReader::supportedImageFormats() cost, subsequent calls
// return the cached list. All 13 test targets that compile this file
// link Qt6::Gui so QImageReader resolves at link time.
auto ExtensionUtils::imageBaseExtensions() -> const QStringList & {
  static const QStringList exts = []() {
    QStringList base = {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                        QStringLiteral("bmp"), QStringLiteral("gif"), QStringLiteral("webp")};
    const auto formats = QImageReader::supportedImageFormats();
    if (formats.contains(QByteArrayLiteral("avif")) ||
        formats.contains(QByteArrayLiteral("AVIF"))) {
      base.append(QStringLiteral("avif"));
    }
    return base;
  }();
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

// Extension-only allowlist guard for image-decode call sites. Deliberately
// strict — matches imageBaseExtensions() exactly — so a scraped `.pdf` (or
// any other non-image file) can never be routed to Qt's image plugins.
auto ExtensionUtils::isDecodableImagePath(const QString &path) -> bool {
  if (path.isEmpty()) {
    return false;
  }
  return imageBaseExtensions().contains(QFileInfo(path).suffix().toLower());
}