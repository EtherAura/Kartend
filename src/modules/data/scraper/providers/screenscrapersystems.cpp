// Pure helpers for ScreenScraper systemeid handling. NO bundled
// system catalog — the live list comes from SS's systemesListe.php
// API at runtime via the cache fetcher (separate follow-up issue).
// Until that ships, the provider receives an empty `systems` list
// and falls back to systemeid=0 for the lookup.
#include "screenscrapersystems.h"

#include <QStringView>

namespace ScreenScraperSystems {

namespace {

QString lower(const QString &s) {
  return s.trimmed().toLower();
}

QStringList lowerList(const QStringList &xs) {
  QStringList out;
  out.reserve(xs.size());
  for (const QString &x : xs) {
    QString l = lower(x);
    if (l.startsWith('.')) l.remove(0, 1);
    if (!l.isEmpty()) out.append(l);
  }
  return out;
}

/// Whole-word alias match. Tokenises the haystack on
/// non-alphanumeric characters and tests whether any token matches
/// the alias exactly. Multi-word aliases (containing a space) fall
/// back to substring matching — those don't have the false-positive
/// risk of the single-word case.
bool aliasMatchesWord(const QString &haystackLower, const QString &aliasLower) {
  if (aliasLower.isEmpty() || haystackLower.isEmpty()) return false;
  if (aliasLower.contains(' ')) return haystackLower.contains(aliasLower);
  int i = 0;
  while (i < haystackLower.size()) {
    while (i < haystackLower.size() && !haystackLower.at(i).isLetterOrNumber()) ++i;
    int start = i;
    while (i < haystackLower.size() && haystackLower.at(i).isLetterOrNumber()) ++i;
    if (i - start == aliasLower.size() &&
        QStringView(haystackLower).mid(start, i - start) == aliasLower) {
      return true;
    }
  }
  return false;
}

} // namespace

const System *find(const QList<System> &systems, int systemeid) {
  for (const auto &s : systems) {
    if (s.id == systemeid) return &s;
  }
  return nullptr;
}

int autodetect(const QString &collectionName, const QString &collectionType,
               const QStringList &extensions, const QList<System> &systems) {
  if (systems.isEmpty()) {
    return -1;
  }
  const QStringList exts = lowerList(extensions);
  const QString name = lower(collectionName);
  const QString type = lower(collectionType);

  int bestScore = 0;
  int bestId = -1;
  for (const auto &s : systems) {
    int score = 0;
    for (const QString &alias : s.aliases) {
      const QString a = lower(alias);
      if (aliasMatchesWord(name, a) || aliasMatchesWord(type, a)) {
        score += 2;
      }
    }
    for (const QString &ext : s.extensions) {
      if (exts.contains(lower(ext))) {
        score += 1;
      }
    }
    // Strict ">": preserves first-match-wins on ties so the
    // SS-supplied catalog ordering breaks ambiguities.
    if (score > bestScore) {
      bestScore = score;
      bestId = s.id;
    }
  }
  return bestId;
}

} // namespace ScreenScraperSystems
