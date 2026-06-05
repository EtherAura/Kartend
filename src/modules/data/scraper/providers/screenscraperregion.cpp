#include "screenscraperregion.h"

#include <QList>
#include <QPair>
#include <QRegularExpression>

QString ScreenScraperRegion::detectFromFilename(const QString &filenameOrBasename) {
  static const QList<QPair<QRegularExpression, QString>> kPatterns = {
      // Full names (No-Intro convention). Matched as whole-word inside
      // a parenthesised tag, possibly followed by a comma + more
      // regions.
      {QRegularExpression(QStringLiteral("\\(Japan(?:[,)]|\\s)"),
                          QRegularExpression::CaseInsensitiveOption),
       QStringLiteral("jp")},
      {QRegularExpression(QStringLiteral("\\bUSA(?:[,)]|\\s)"),
                          QRegularExpression::CaseInsensitiveOption),
       QStringLiteral("us")},
      {QRegularExpression(QStringLiteral("\\bEurope(?:[,)]|\\s)"),
                          QRegularExpression::CaseInsensitiveOption),
       QStringLiteral("eu")},
      {QRegularExpression(QStringLiteral("\\bWorld(?:[,)]|\\s)"),
                          QRegularExpression::CaseInsensitiveOption),
       QStringLiteral("wor")},
      {QRegularExpression(QStringLiteral("\\bKorea(?:[,)]|\\s)"),
                          QRegularExpression::CaseInsensitiveOption),
       QStringLiteral("kr")},
      {QRegularExpression(QStringLiteral("\\bFrance(?:[,)]|\\s)"),
                          QRegularExpression::CaseInsensitiveOption),
       QStringLiteral("fr")},
      {QRegularExpression(QStringLiteral("\\bGermany(?:[,)]|\\s)"),
                          QRegularExpression::CaseInsensitiveOption),
       QStringLiteral("de")},
      {QRegularExpression(QStringLiteral("\\bItaly(?:[,)]|\\s)"),
                          QRegularExpression::CaseInsensitiveOption),
       QStringLiteral("it")},
      {QRegularExpression(QStringLiteral("\\bSpain(?:[,)]|\\s)"),
                          QRegularExpression::CaseInsensitiveOption),
       QStringLiteral("sp")},
      {QRegularExpression(QStringLiteral("\\bBrazil(?:[,)]|\\s)"),
                          QRegularExpression::CaseInsensitiveOption),
       QStringLiteral("br")},
      // Single-letter shorthand. Anchored to '(L)' exactly to avoid
      // matching '(J-Pop)' or similar incidental text.
      {QRegularExpression(QStringLiteral("\\(J\\)")), QStringLiteral("jp")},
      {QRegularExpression(QStringLiteral("\\(U\\)")), QStringLiteral("us")},
      {QRegularExpression(QStringLiteral("\\(E\\)")), QStringLiteral("eu")},
      {QRegularExpression(QStringLiteral("\\(W\\)")), QStringLiteral("wor")},
      {QRegularExpression(QStringLiteral("\\(K\\)")), QStringLiteral("kr")},
      {QRegularExpression(QStringLiteral("\\(F\\)")), QStringLiteral("fr")},
      {QRegularExpression(QStringLiteral("\\(G\\)")), QStringLiteral("de")},
      {QRegularExpression(QStringLiteral("\\(I\\)")), QStringLiteral("it")},
      {QRegularExpression(QStringLiteral("\\(S\\)")), QStringLiteral("sp")},
      {QRegularExpression(QStringLiteral("\\(B\\)")), QStringLiteral("br")},
  };
  for (const auto &[re, tag] : kPatterns) {
    if (re.match(filenameOrBasename).hasMatch()) {
      return tag;
    }
  }
  return {};
}
