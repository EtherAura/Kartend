#include "redumpparse.h"

#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QRegularExpressionMatchIterator>
#include <QSet>
#include <QStringDecoder>

namespace RedumpParse {

namespace {

// Decode raw page bytes to a QString with a UTF-8-then-Latin-1 fallback
// (Kartend-a3s01). A page without a valid UTF-8 encoding would otherwise get
// U+FFFD-mangled; Latin-1 never fails and valid UTF-8 decodes identically.
QString decodePage(const QByteArray &bytes) {
  QStringDecoder utf8(QStringDecoder::Utf8);
  QString s = utf8.decode(bytes);
  if (utf8.hasError()) {
    return QString::fromLatin1(bytes);
  }
  return s;
}

} // namespace

QList<System> parseSystems(const QByteArray &html) {
  QList<System> out;
  QSet<QString> seen;
  // Match <a href="...discs/system/<slug>/">Name</a>. The slug is the same one
  // used at /datfile/<slug>/. Tolerant of absolute/relative hrefs and extra
  // attributes; the link text (tags stripped) is the display name.
  // static const: compiled once, not per call (Kartend-s9k45). QRegularExpression
  // is thread-safe for const matching.
  static const QRegularExpression re(
      QStringLiteral(
          "<a\\b[^>]*href=[\"'][^\"']*/discs/system/([A-Za-z0-9_-]+)/?[\"'][^>]*>(.*?)</a>"),
      QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
  // Inner tag-stripper, also hoisted out of the per-iteration body (Kartend-s9k45).
  static const QRegularExpression tagStrip(QStringLiteral("<[^>]*>"));
  bool sawSystemLink = false; // any /discs/system/ anchor at all?
  QRegularExpressionMatchIterator it = re.globalMatch(decodePage(html));
  while (it.hasNext()) {
    sawSystemLink = true;
    const QRegularExpressionMatch m = it.next();
    const QString slug = m.captured(1);
    if (seen.contains(slug)) {
      continue;
    }
    // Strip any nested tags from the link text and collapse whitespace.
    QString name = m.captured(2);
    name.remove(tagStrip);
    name = name.simplified();
    if (slug.isEmpty() || name.isEmpty()) {
      continue;
    }
    seen.insert(slug);
    out.append(System{slug, name});
  }
  // Distinguish a likely layout change from a legitimately-empty result
  // (Kartend-s9k45): redump.org always lists systems, so zero matching
  // `/discs/system/` anchors means the page shape changed (or we were blocked),
  // not that the catalogue is empty. Surface that distinctly so scraper
  // breakage is detectable rather than silently collapsing into "empty".
  if (!sawSystemLink) {
    qWarning("RedumpParse: no /discs/system/ links found — redump.org page layout "
             "may have changed (or the request was blocked)");
  }
  return out;
}

} // namespace RedumpParse
