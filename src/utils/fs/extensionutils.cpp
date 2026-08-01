// Categorizes file extensions by media type (ROM, disc image, archive, etc.).
#include "extensionutils.h"
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QSet>
#include <QString>

auto ExtensionUtils::bareExtension(const QString &token) -> QString {
  QString out = token.trimmed().toLower();
  if (out.startsWith(QLatin1String("*."))) {
    out.remove(0, 2);
  } else if (out.startsWith(QLatin1Char('.'))) {
    out.remove(0, 1);
  }
  return out.trimmed();
}

auto ExtensionUtils::normalizeStoredExtensions(const QStringList &raw) -> QStringList {
  QStringList out;
  QSet<QString> seen;
  out.reserve(raw.size());
  for (const QString &token : raw) {
    const QString bare = bareExtension(token);
    if (bare.isEmpty() || seen.contains(bare)) {
      continue;
    }
    out.append(bare);
    seen.insert(bare);
  }
  return out;
}

auto ExtensionUtils::toNameFilters(const QStringList &extensions) -> QStringList {
  QStringList filters;
  filters.reserve(extensions.size());
  for (const QString &token : extensions) {
    const QString bare = bareExtension(token);
    if (!bare.isEmpty()) {
      filters << QStringLiteral("*.") + bare;
    }
  }
  return filters;
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

auto ExtensionUtils::videoBaseExtensions() -> const QStringList & {
  static const QStringList exts = {QStringLiteral("mp4"), QStringLiteral("webm"),
                                   QStringLiteral("mkv"), QStringLiteral("mov"),
                                   QStringLiteral("avi")};
  return exts;
}

auto ExtensionUtils::archiveBaseExtensions() -> const QStringList & {
  static const QStringList exts = {QStringLiteral("zip"), QStringLiteral("7z"),
                                   QStringLiteral("rar"), QStringLiteral("gz"),
                                   QStringLiteral("tar"), QStringLiteral("bz2"),
                                   QStringLiteral("xz")};
  return exts;
}

// Suffix match (not QFileInfo::suffix()) so multi-part names like
// "disc.tar.gz" are recognised through their ".gz" tail, matching what the
// launcher and hasher have always accepted.
auto ExtensionUtils::isArchivePath(const QString &path) -> bool {
  const QString lowered = path.toLower();
  for (const QString &ext : archiveBaseExtensions()) {
    if (lowered.endsWith(QLatin1Char('.') + ext)) {
      return true;
    }
  }
  return false;
}

auto ExtensionUtils::findFileWithExtensions(const QDir &dir, const QString &baseName,
                                            const QStringList &extensions) -> QString {
  for (const QString &ext : extensions) {
    QString path = dir.absoluteFilePath(baseName + "." + ext);
    if (QFile::exists(path)) {
      return path;
    }
    path = dir.absoluteFilePath(baseName + "." + ext.toUpper());
    if (QFile::exists(path)) {
      return path;
    }
  }
  return {};
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