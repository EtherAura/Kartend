// Categorizes file extensions by media type (ROM, disc image, archive, etc.).
#include "extensionutils.h"
#include <QBuffer>
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
                        QStringLiteral("bmp"), QStringLiteral("gif"), QStringLiteral("webp"),
                        // SVG (Kartend field report 2026-08-17): ScreenScraper's
                        // logo-svg variants and the Wikidata/Commons logos are
                        // vectors — without the whitelist entry every SVG
                        // payload was renamed .png on write (the tree's
                        // suffix-keyed SVG renderer then never fired). Kept
                        // unconditional: the Qt SVG imageformats plugin ships
                        // with every supported install, and a missing plugin
                        // only degrades rendering, not naming.
                        QStringLiteral("svg")};
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

auto ExtensionUtils::imageExtensionForBytes(const QString &urlPath, const QByteArray &bytes)
    -> QString {
  static const QSet<QString> kImageExts(imageBaseExtensions().begin(), imageBaseExtensions().end());
  // QFileInfo::suffix on the URL's PATH (never the full URL) so a query string
  // cannot contribute a bogus suffix. Whitelisted, so a pathological
  // ".../cover.php" does not become a ".php" file.
  const QString rawExt = QFileInfo(urlPath).suffix().toLower();
  if (kImageExts.contains(rawExt)) {
    return rawExt;
  }
  // SVG IS DECIDED HERE, FROM THE BYTES — before QImageReader is consulted,
  // and its answer for svg is discarded below (Kartend-mwfqr).
  //
  // QImageReader delegates to whichever plugins are DEPLOYED, and the SVG
  // plugin's canRead has never been a reliable oracle: Qt 6.7's accepts a bare
  // "<?xml" prefix, so an ordinary non-SVG XML document came back as "svg",
  // while 6.11's rejects the same bytes. That made this function's output a
  // property of the runner's plugin set rather than of the payload — green on
  // Linux, red on Windows, for the same input. Sniffing here makes the answer
  // identical everywhere, and it is also the more correct answer: a document
  // with no <svg> element is not an SVG whatever a plugin claims.
  //
  // A leading XML/SVG marker plus an actual <svg> token is definitive; the
  // marker alone is not. This is also the path that rescues an extension-less
  // vector whose suffix was lost through a redirect (field report 2026-08-17).
  const QByteArray head = bytes.left(256).trimmed();
  if ((head.startsWith(QByteArrayLiteral("<svg")) || head.startsWith(QByteArrayLiteral("<?xml"))) &&
      bytes.left(2048).contains(QByteArrayLiteral("<svg"))) {
    return QStringLiteral("svg");
  }

  QBuffer probe;
  probe.setData(bytes);
  if (probe.open(QIODevice::ReadOnly)) {
    QString format = QString::fromLatin1(QImageReader::imageFormat(&probe)).toLower();
    if (format == QLatin1String("jpeg")) {
      format = QStringLiteral("jpg");
    }
    // Raster formats only. An "svg" answer is ignored on purpose: the block
    // above already settled that question from the bytes, so accepting one
    // here could only ever re-introduce the plugin-dependent result.
    if (format != QLatin1String("svg") && kImageExts.contains(format)) {
      return format;
    }
  }
  return QStringLiteral("png");
}

auto ExtensionUtils::videoBaseExtensions() -> const QStringList & {
  static const QStringList exts = {QStringLiteral("mp4"), QStringLiteral("webm"),
                                   QStringLiteral("mkv"), QStringLiteral("mov"),
                                   QStringLiteral("avi")};
  return exts;
}

auto ExtensionUtils::archiveBaseExtensions() -> const QStringList & {
  static const QStringList exts = {
      QStringLiteral("zip"), QStringLiteral("7z"),  QStringLiteral("rar"), QStringLiteral("gz"),
      QStringLiteral("tar"), QStringLiteral("bz2"), QStringLiteral("xz")};
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