#include "datauditcatalogue.h"

namespace DatAudit {

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
