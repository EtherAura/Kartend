#include "datauditregion.h"

#include <QHash>
#include <QRegularExpression>

namespace DatAudit::Region {

namespace {

// Lowercased tag token -> canonical No-Intro region name. Full names plus the
// legacy GoodTools single-letter shorthands older sets still use. Language
// codes ("En", "Fr", "De") deliberately absent so "(En,Fr,De)" never reads as
// a region.
const QHash<QString, QString> &regionTable() {
  static const QHash<QString, QString> kTable = {
      {QStringLiteral("usa"), QStringLiteral("USA")},
      {QStringLiteral("u"), QStringLiteral("USA")},
      {QStringLiteral("europe"), QStringLiteral("Europe")},
      {QStringLiteral("e"), QStringLiteral("Europe")},
      {QStringLiteral("japan"), QStringLiteral("Japan")},
      {QStringLiteral("j"), QStringLiteral("Japan")},
      {QStringLiteral("world"), QStringLiteral("World")},
      {QStringLiteral("w"), QStringLiteral("World")},
      {QStringLiteral("asia"), QStringLiteral("Asia")},
      {QStringLiteral("australia"), QStringLiteral("Australia")},
      {QStringLiteral("brazil"), QStringLiteral("Brazil")},
      {QStringLiteral("canada"), QStringLiteral("Canada")},
      {QStringLiteral("china"), QStringLiteral("China")},
      {QStringLiteral("france"), QStringLiteral("France")},
      {QStringLiteral("germany"), QStringLiteral("Germany")},
      {QStringLiteral("italy"), QStringLiteral("Italy")},
      {QStringLiteral("korea"), QStringLiteral("Korea")},
      {QStringLiteral("netherlands"), QStringLiteral("Netherlands")},
      {QStringLiteral("spain"), QStringLiteral("Spain")},
      {QStringLiteral("sweden"), QStringLiteral("Sweden")},
      {QStringLiteral("uk"), QStringLiteral("UK")},
  };
  return kTable;
}

} // namespace

QString baseGameName(const QString &gameName) {
  // Cut at the first " (" or " [": No-Intro titles place every tag after the
  // base name. This intentionally also strips a "(Disc N)" qualifier — the
  // 1G1R grouping uses groupKey(), which re-adds discQualifier(), so multi-disc
  // releases don't collapse into one game.
  int cut = gameName.size();
  const int paren = gameName.indexOf(QStringLiteral(" ("));
  const int brack = gameName.indexOf(QStringLiteral(" ["));
  if (paren >= 0) {
    cut = qMin(cut, paren);
  }
  if (brack >= 0) {
    cut = qMin(cut, brack);
  }
  return gameName.left(cut).trimmed();
}

QString detectRegion(const QString &gameName) {
  // Walk each parenthetical group left to right; within a group split on commas
  // and slashes; the first sub-token that names a region wins.
  static const QRegularExpression kGroup(QStringLiteral("\\(([^)]*)\\)"));
  static const QRegularExpression kSep(QStringLiteral("[,/]"));
  const QHash<QString, QString> &table = regionTable();
  auto it = kGroup.globalMatch(gameName);
  while (it.hasNext()) {
    const QRegularExpressionMatch m = it.next();
    const QStringList tokens = m.captured(1).split(kSep, Qt::SkipEmptyParts);
    for (const QString &tok : tokens) {
      const auto hit = table.constFind(tok.trimmed().toLower());
      if (hit != table.constEnd()) {
        return hit.value();
      }
    }
  }
  return {};
}

int regionRank(const QString &region, const QStringList &priority) {
  if (region.isEmpty()) {
    return priority.size() + 1; // untagged: after even a listed-but-unmatched region
  }
  for (int i = 0; i < priority.size(); ++i) {
    if (priority.at(i).compare(region, Qt::CaseInsensitive) == 0) {
      return i;
    }
  }
  return priority.size(); // tagged but not in the user's list
}

QStringList knownRegions() {
  // Canonical spellings must match regionTable()'s values above. Ordered by
  // how commonly they head a 1G1R priority list.
  return {QStringLiteral("USA"),    QStringLiteral("World"),       QStringLiteral("Europe"),
          QStringLiteral("Japan"),  QStringLiteral("Asia"),        QStringLiteral("Australia"),
          QStringLiteral("Brazil"), QStringLiteral("Canada"),      QStringLiteral("China"),
          QStringLiteral("France"), QStringLiteral("Germany"),     QStringLiteral("Italy"),
          QStringLiteral("Korea"),  QStringLiteral("Netherlands"), QStringLiteral("Spain"),
          QStringLiteral("Sweden"), QStringLiteral("UK")};
}

QString discQualifier(const QString &gameName) {
  static const QRegularExpression kGroup(QStringLiteral("\\(([^)]*)\\)"));
  static const QRegularExpression kDisc(QStringLiteral("^(disc|disk|side)\\s+(.+)$"),
                                        QRegularExpression::CaseInsensitiveOption);
  auto it = kGroup.globalMatch(gameName);
  while (it.hasNext()) {
    const QString inside = it.next().captured(1).trimmed();
    const QRegularExpressionMatch m = kDisc.match(inside);
    if (m.hasMatch()) {
      QString type = m.captured(1).toLower();
      if (type == QLatin1String("disk")) {
        type = QStringLiteral("disc"); // fold Disk -> Disc so the two spellings group together
      }
      return type + QLatin1Char(' ') + m.captured(2).trimmed().toLower();
    }
  }
  return {};
}

QString groupKey(const QString &gameName) {
  const QString base = baseGameName(gameName);
  const QString disc = discQualifier(gameName);
  // A control char as the separator so "Game" + "disc 1" can't collide with a
  // title that literally ends in "disc 1".
  return disc.isEmpty() ? base : base + QChar(0x1F) + disc;
}

} // namespace DatAudit::Region
