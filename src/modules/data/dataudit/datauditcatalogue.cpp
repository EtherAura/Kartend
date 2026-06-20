#include "datauditcatalogue.h"

#include <QSet>

namespace DatAudit {

namespace {
/// The set identity a clone's cloneof references. Prefer the explicit setId
/// (MAME name= / Logiqx+clrmamepro game name); fall back to gameName for any
/// pre-setId cache record (Kartend-m6qsb.29).
QString setKeyOf(const DatLookup::DatRecord &r) {
  return r.setId.isEmpty() ? r.gameName : r.setId;
}
} // namespace

int Catalogue::addSource(const QString &name) {
  m_sourceNames.append(name);
  return static_cast<int>(m_sourceNames.size()) - 1;
}

void Catalogue::addRecord(const DatLookup::DatRecord &r, int sourceId) {
  const int idx = static_cast<int>(m_records.size());
  m_records.append(r);
  m_recordSource.append(sourceId);
  // First-seen wins (QHash::insert overwrites, so guard with contains()).
  if (!r.sha1.isEmpty() && !m_bySha1.contains(r.sha1)) {
    m_bySha1.insert(r.sha1, idx);
  }
  if (!r.md5.isEmpty() && !m_byMd5.contains(r.md5)) {
    m_byMd5.insert(r.md5, idx);
  }
  if (!r.crc.isEmpty() && !m_byCrc.contains(r.crc)) {
    m_byCrc.insert(r.crc, idx);
  }
  if (!r.romName.isEmpty() && !m_byName.contains(r.romName)) {
    m_byName.insert(r.romName, idx);
  }
  // Clone index: every record of a set is reachable by the set-id, so a clone's
  // cloneof can resolve to its parent's roms (Kartend-m6qsb.29).
  m_bySetKey[setKeyOf(r)].append(idx);
}

QList<int> Catalogue::recordsForSet(const QString &setKey) const {
  return m_bySetKey.value(setKey);
}

QList<int> Catalogue::ancestorRecords(int i) const {
  QList<int> out;
  QSet<QString> visited;                     // set-keys seen — cycle guard
  visited.insert(setKeyOf(m_records.at(i))); // never revisit the start set
  QString parent = m_records.at(i).cloneOf;
  while (!parent.isEmpty() && !visited.contains(parent)) {
    visited.insert(parent);
    const QList<int> recs = recordsForSet(parent);
    out.append(recs);
    // Climb to the parent set's own parent. A set's records share one cloneof,
    // so any record of it answers; empty (dangling parent) ends the walk.
    parent = recs.isEmpty() ? QString() : m_records.at(recs.first()).cloneOf;
  }
  return out;
}

QString Catalogue::rootSetGameName(int i) const {
  QSet<QString> visited;
  int rep = i; // representative record of the highest set reached so far
  visited.insert(setKeyOf(m_records.at(rep)));
  QString parent = m_records.at(rep).cloneOf;
  while (!parent.isEmpty() && !visited.contains(parent)) {
    const QList<int> recs = recordsForSet(parent);
    if (recs.isEmpty()) {
      break; // parent set absent from this catalogue — stop at the last resolvable
    }
    visited.insert(parent);
    rep = recs.first();
    parent = m_records.at(rep).cloneOf;
  }
  return m_records.at(rep).gameName;
}

int Catalogue::matchByHash(const QString &crc, const QString &md5, const QString &sha1) const {
  if (!sha1.isEmpty()) {
    auto it = m_bySha1.constFind(sha1.toLower());
    if (it != m_bySha1.constEnd()) {
      return it.value();
    }
  }
  if (!md5.isEmpty()) {
    auto it = m_byMd5.constFind(md5.toLower());
    if (it != m_byMd5.constEnd()) {
      return it.value();
    }
  }
  if (!crc.isEmpty()) {
    auto it = m_byCrc.constFind(crc.toLower());
    if (it != m_byCrc.constEnd()) {
      return it.value();
    }
  }
  return -1;
}

int Catalogue::matchByName(const QString &name) const {
  auto it = m_byName.constFind(name);
  return it != m_byName.constEnd() ? it.value() : -1;
}

} // namespace DatAudit
