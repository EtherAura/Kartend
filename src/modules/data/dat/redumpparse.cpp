#include "redumpparse.h"

#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QRegularExpressionMatchIterator>
#include <QSet>

namespace RedumpParse {

QList<System> parseSystems(const QByteArray &html) {
  QList<System> out;
  QSet<QString> seen;
  // Match <a href="...discs/system/<slug>/">Name</a>. The slug is the same one
  // used at /datfile/<slug>/. Tolerant of absolute/relative hrefs and extra
  // attributes; the link text (tags stripped) is the display name.
  const QRegularExpression re(
      QStringLiteral(
          "<a\\b[^>]*href=[\"'][^\"']*/discs/system/([A-Za-z0-9_-]+)/?[\"'][^>]*>(.*?)</a>"),
      QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
  QRegularExpressionMatchIterator it = re.globalMatch(QString::fromUtf8(html));
  while (it.hasNext()) {
    const QRegularExpressionMatch m = it.next();
    const QString slug = m.captured(1);
    if (seen.contains(slug)) {
      continue;
    }
    // Strip any nested tags from the link text and collapse whitespace.
    QString name = m.captured(2);
    name.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
    name = name.simplified();
    if (slug.isEmpty() || name.isEmpty()) {
      continue;
    }
    seen.insert(slug);
    out.append(System{slug, name});
  }
  return out;
}

} // namespace RedumpParse
