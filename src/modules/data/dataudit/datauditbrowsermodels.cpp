#include "datauditbrowsermodels.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QIcon>
#include <QStringList>

#include "datauditmodel.h" // DatAuditModel::statusLabel

namespace DatAudit {

namespace {
/// The item-folder a scanned file belongs to: the first path component of its
/// path relative to whichever scan root contains it. A file directly in a root
/// (no subfolder) — or under no root at all — keys under its own basename
/// (Kartend-m6qsb.30).
QString itemFolderFor(const QString &filePath, const QStringList &scanRoots) {
  const QString fileClean = QDir::cleanPath(filePath);
  for (const QString &root : scanRoots) {
    const QString rootClean = QDir::cleanPath(root);
    if (rootClean.isEmpty()) {
      continue;
    }
    const QString prefix = rootClean + QLatin1Char('/');
    if (fileClean.startsWith(prefix)) {
      const QString rel = fileClean.mid(prefix.size());
      const int slash = rel.indexOf(QLatin1Char('/'));
      return slash >= 0 ? rel.left(slash) : rel;
    }
  }
  return QFileInfo(fileClean).fileName();
}
} // namespace

QList<FolderRollup> groupResultsByFolder(const QList<DatAuditProfile::ResultRow> &results,
                                         const QStringList &scanRoots) {
  QHash<QString, BucketCounts> byFolder;
  QStringList order; // first-seen order for deterministic output
  for (const DatAuditProfile::ResultRow &r : results) {
    // A Missing row carries no on-disk file; key it by its game so absent ROMs
    // still count toward the folder (folder == game name in SubfolderPerItem).
    const QString folder = r.filePath.isEmpty() ? r.gameName : itemFolderFor(r.filePath, scanRoots);
    if (folder.isEmpty()) {
      continue;
    }
    auto it = byFolder.find(folder);
    if (it == byFolder.end()) {
      order.append(folder);
      it = byFolder.insert(folder, BucketCounts{});
    }
    it.value().add(static_cast<Status>(r.status), r.mia, 1);
  }
  QList<FolderRollup> out;
  out.reserve(order.size());
  for (const QString &folder : order) {
    out.append(FolderRollup{folder, byFolder.value(folder)});
  }
  return out;
}

// ── AuditTreeModel ─────────────────────────────────────────────────────────

AuditTreeModel::AuditTreeModel(QObject *parent) : QAbstractItemModel(parent) {}

void AuditTreeModel::setTree(const QList<DatAuditProfile::Profile> &profiles,
                             const QList<DatAuditProfile::RollupRow> &rollups) {
  beginResetModel();
  m_nodes.clear();
  m_nodes.append(Node{}); // index 0 — synthetic root

  QHash<qint64, QList<DatAuditProfile::RollupRow>> byProfile;
  for (const auto &r : rollups) {
    byProfile[r.profileId].append(r);
  }

  for (const DatAuditProfile::Profile &p : profiles) {
    Node prof;
    prof.kind = NodeKind::Profile;
    prof.parent = 0;
    prof.profileId = p.id;
    prof.label = p.name;
    const int profIdx = m_nodes.size();
    m_nodes.append(prof);
    m_nodes[0].children.append(profIdx);

    const QList<DatAuditProfile::RollupRow> &rows = byProfile.value(p.id);

    // sourceName (DAT filename) → absolute DAT path, for lazy catalogue loads.
    QHash<QString, QString> srcToPath;
    for (const DatAuditProfile::DatRef &d : p.dats) {
      srcToPath.insert(QFileInfo(d.path).fileName(), d.path);
    }

    QHash<QString, BucketCounts> bySource;
    QStringList sourceOrder;
    for (const auto &r : rows) {
      if (!bySource.contains(r.sourceName)) {
        sourceOrder.append(r.sourceName);
      }
      bySource[r.sourceName].add(static_cast<Status>(r.status), r.mia, r.count);
    }

    BucketCounts profCounts;
    if (rows.isEmpty()) {
      m_nodes[profIdx].unscanned = true;
    } else if (sourceOrder.size() <= 1) {
      // Single source (or pre-v22 rows with an empty source): the profile node
      // carries the counts + the lone source's DAT path directly (leaf).
      const QString src = sourceOrder.value(0);
      profCounts = bySource.value(src);
      m_nodes[profIdx].sourceName = src;
      m_nodes[profIdx].datPath = srcToPath.value(src);
    } else {
      for (const QString &src : sourceOrder) {
        Node sn;
        sn.kind = NodeKind::Source;
        sn.parent = profIdx;
        sn.profileId = p.id;
        sn.sourceName = src;
        sn.datPath = srcToPath.value(src);
        sn.label = src.isEmpty() ? tr("(unknown source)") : src;
        sn.counts = bySource.value(src);
        const int snIdx = m_nodes.size();
        m_nodes.append(sn);
        m_nodes[profIdx].children.append(snIdx);
        profCounts.add(sn.counts);
      }
    }
    m_nodes[profIdx].counts = profCounts;
  }
  endResetModel();
}

const AuditTreeModel::Node *AuditTreeModel::nodeFor(const QModelIndex &index) const {
  if (!index.isValid()) {
    return nullptr;
  }
  const int i = static_cast<int>(index.internalId());
  return (i >= 0 && i < m_nodes.size()) ? &m_nodes.at(i) : nullptr;
}

qint64 AuditTreeModel::profileIdAt(const QModelIndex &index) const {
  const Node *n = nodeFor(index);
  return n ? n->profileId : -1;
}

QString AuditTreeModel::sourceNameAt(const QModelIndex &index) const {
  const Node *n = nodeFor(index);
  return n ? n->sourceName : QString();
}

QString AuditTreeModel::datPathAt(const QModelIndex &index) const {
  const Node *n = nodeFor(index);
  return n ? n->datPath : QString();
}

QModelIndex AuditTreeModel::index(int row, int column, const QModelIndex &parent) const {
  if (m_nodes.isEmpty() || column != 0 || row < 0) {
    return {};
  }
  const int parentIdx = parent.isValid() ? static_cast<int>(parent.internalId()) : 0;
  if (parentIdx < 0 || parentIdx >= m_nodes.size()) {
    return {};
  }
  const QList<int> &kids = m_nodes.at(parentIdx).children;
  if (row >= kids.size()) {
    return {};
  }
  return createIndex(row, column, static_cast<quintptr>(kids.at(row)));
}

QModelIndex AuditTreeModel::parent(const QModelIndex &index) const {
  if (!index.isValid()) {
    return {};
  }
  const int nodeIdx = static_cast<int>(index.internalId());
  if (nodeIdx <= 0 || nodeIdx >= m_nodes.size()) {
    return {};
  }
  const int parentIdx = m_nodes.at(nodeIdx).parent;
  if (parentIdx <= 0) {
    return {}; // parent is the synthetic root → top-level item
  }
  const int grandIdx = m_nodes.at(parentIdx).parent;
  const int row = (grandIdx >= 0) ? m_nodes.at(grandIdx).children.indexOf(parentIdx) : 0;
  return createIndex(row < 0 ? 0 : row, 0, static_cast<quintptr>(parentIdx));
}

int AuditTreeModel::rowCount(const QModelIndex &parent) const {
  if (m_nodes.isEmpty() || parent.column() > 0) {
    return 0;
  }
  const int parentIdx = parent.isValid() ? static_cast<int>(parent.internalId()) : 0;
  if (parentIdx < 0 || parentIdx >= m_nodes.size()) {
    return 0;
  }
  return static_cast<int>(m_nodes.at(parentIdx).children.size());
}

int AuditTreeModel::columnCount(const QModelIndex & /*parent*/) const {
  return 1;
}

QVariant AuditTreeModel::data(const QModelIndex &index, int role) const {
  const Node *n = nodeFor(index);
  if (!n) {
    return {};
  }
  switch (role) {
  case Qt::DisplayRole:
    return n->label;
  case KindRole:
    return static_cast<int>(n->kind);
  case ProfileIdRole:
    return n->profileId;
  case SourceNameRole:
    return n->sourceName;
  case DatPathRole:
    return n->datPath;
  case UnscannedRole:
    return n->unscanned;
  case CountsRole:
    return QVariant::fromValue(n->counts);
  case Qt::DecorationRole:
    return QIcon::fromTheme(n->kind == NodeKind::Source ? QStringLiteral("application-x-archive")
                                                        : QStringLiteral("folder"));
  default:
    return {};
  }
}

// ── GameListModel ──────────────────────────────────────────────────────────

namespace {
// Representative status for tinting a game row by its overall state.
int tintStatusForState(GameState s) {
  switch (s) {
  case GameState::Complete:
    return static_cast<int>(Status::Have);
  case GameState::Partial:
    return static_cast<int>(Status::WrongName); // amber
  case GameState::Empty:
    return static_cast<int>(Status::Missing);
  }
  return static_cast<int>(Status::Missing);
}
} // namespace

GameListModel::GameListModel(QObject *parent) : QAbstractTableModel(parent) {}

void GameListModel::setGames(const QList<DatAuditProfile::GameRollupRow> &rollups) {
  beginResetModel();
  m_rows.clear();
  QHash<QString, int> idx;
  for (const auto &r : rollups) {
    int i = idx.value(r.gameName, -1);
    if (i < 0) {
      i = m_rows.size();
      idx.insert(r.gameName, i);
      GameRow gr;
      gr.gameName = r.gameName;
      m_rows.append(gr);
    }
    m_rows[i].counts.add(static_cast<Status>(r.status), r.mia, r.count);
  }
  for (GameRow &g : m_rows) {
    g.state = gameStateOf(g.counts);
    g.hasFixes = g.counts.fixable > 0;
    g.hasMia = g.counts.mia > 0;
  }
  endResetModel();
}

void GameListModel::clear() {
  beginResetModel();
  m_rows.clear();
  endResetModel();
}

QString GameListModel::gameNameAt(int row) const {
  return (row >= 0 && row < m_rows.size()) ? m_rows.at(row).gameName : QString();
}

int GameListModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

int GameListModel::columnCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : ColumnCount;
}

QVariant GameListModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() >= m_rows.size()) {
    return {};
  }
  const GameRow &g = m_rows.at(index.row());
  if (role == Qt::DisplayRole) {
    switch (index.column()) {
    case GameColumn:
      return g.gameName;
    case HaveColumn:
      return g.counts.have > 0 ? QString::number(g.counts.have) : QString();
    case MissingColumn:
      return g.counts.missing > 0 ? QString::number(g.counts.missing) : QString();
    case FixableColumn:
      return g.counts.fixable > 0 ? QString::number(g.counts.fixable) : QString();
    case MiaColumn:
      return g.counts.mia > 0 ? QString::number(g.counts.mia) : QString();
    default:
      return {};
    }
  }
  if (role == Qt::TextAlignmentRole && index.column() != GameColumn) {
    return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
  }
  switch (role) {
  case BucketStatusRole:
    return tintStatusForState(g.state);
  case GameStateRole:
    return static_cast<int>(g.state);
  case HasFixesRole:
    return g.hasFixes;
  case HasMiaRole:
    return g.hasMia;
  case GameNameRole:
    return g.gameName;
  default:
    return {};
  }
}

QVariant GameListModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
    return {};
  }
  switch (section) {
  case GameColumn:
    return tr("Game");
  case HaveColumn:
    return tr("Have");
  case MissingColumn:
    return tr("Missing");
  case FixableColumn:
    return tr("Fixable");
  case MiaColumn:
    return tr("MIA");
  default:
    return {};
  }
}

// ── GameListFilterProxy ────────────────────────────────────────────────────

GameListFilterProxy::GameListFilterProxy(QObject *parent) : QSortFilterProxyModel(parent) {
  setFilterKeyColumn(GameListModel::GameColumn);
  setFilterCaseSensitivity(Qt::CaseInsensitive);
}

void GameListFilterProxy::setStateFilter(bool complete, bool partial, bool empty) {
  m_complete = complete;
  m_partial = partial;
  m_empty = empty;
  invalidateFilter();
}

void GameListFilterProxy::setRequireFixes(bool on) {
  m_requireFixes = on;
  invalidateFilter();
}

void GameListFilterProxy::setRequireMia(bool on) {
  m_requireMia = on;
  invalidateFilter();
}

bool GameListFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const {
  const QModelIndex idx = sourceModel()->index(sourceRow, GameListModel::GameColumn, sourceParent);
  const auto state = static_cast<GameState>(idx.data(GameListModel::GameStateRole).toInt());
  const bool stateOk = (state == GameState::Complete && m_complete) ||
                       (state == GameState::Partial && m_partial) ||
                       (state == GameState::Empty && m_empty);
  if (!stateOk) {
    return false;
  }
  if (m_requireFixes && !idx.data(GameListModel::HasFixesRole).toBool()) {
    return false;
  }
  if (m_requireMia && !idx.data(GameListModel::HasMiaRole).toBool()) {
    return false;
  }
  // Defer to the base for the name search (filterKeyColumn = Game).
  return QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent);
}

// ── RomFileModel ───────────────────────────────────────────────────────────

RomFileModel::RomFileModel(QObject *parent) : QAbstractTableModel(parent) {}

void RomFileModel::setGame(const QList<DatLookup::DatRecord> &records,
                           const QList<DatAuditProfile::ResultRow> &results) {
  beginResetModel();
  m_rows.clear();

  QHash<QString, const DatAuditProfile::ResultRow *> statusByName;
  QHash<QString, int> countByName;
  for (const DatAuditProfile::ResultRow &rr : results) {
    statusByName.insert(rr.detail, &rr); // detail == romName; last write wins
    countByName[rr.detail] += 1;
  }

  for (const DatLookup::DatRecord &rec : records) {
    RomRow row;
    row.rec = rec;
    if (auto it = statusByName.constFind(rec.romName); it != statusByName.constEnd()) {
      row.status = static_cast<Status>(it.value()->status);
      row.filePath = it.value()->filePath;
    } else {
      row.status = Status::Missing;
    }
    row.instanceCount = countByName.value(rec.romName, 0);
    m_rows.append(row);
  }

  // No catalogue records (no DAT path, or a pre-v22 snapshot) — still show the
  // persisted result rows so status is visible even without hashes.
  if (records.isEmpty()) {
    for (const DatAuditProfile::ResultRow &rr : results) {
      RomRow row;
      row.rec.romName = rr.detail;
      row.rec.mia = rr.mia;
      row.status = static_cast<Status>(rr.status);
      row.filePath = rr.filePath;
      row.instanceCount = countByName.value(rr.detail, 0);
      m_rows.append(row);
    }
  }
  endResetModel();
}

void RomFileModel::clear() {
  beginResetModel();
  m_rows.clear();
  endResetModel();
}

int RomFileModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

int RomFileModel::columnCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : ColumnCount;
}

QVariant RomFileModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() >= m_rows.size()) {
    return {};
  }
  const RomRow &r = m_rows.at(index.row());
  if (role == StatusRole) {
    return static_cast<int>(r.status);
  }
  if (role == Qt::DisplayRole) {
    switch (index.column()) {
    case GotColumn:
      return DatAuditModel::statusLabel(r.status);
    case RomColumn:
      return r.rec.romName;
    case MergeColumn:
      return r.rec.cloneOf;
    case SizeColumn:
      return r.rec.size >= 0 ? QString::number(r.rec.size) : QString();
    case CrcColumn:
      return r.rec.crc.toUpper();
    case Sha1Column:
      return r.rec.sha1;
    case Md5Column:
      return r.rec.md5;
    case MiaColumn:
      return r.rec.mia ? tr("yes") : QString();
    case InstanceColumn:
      return r.instanceCount > 1 ? QString::number(r.instanceCount) : QString();
    default:
      return {};
    }
  }
  return {};
}

QVariant RomFileModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
    return {};
  }
  switch (section) {
  case GotColumn:
    return tr("Got");
  case RomColumn:
    return tr("ROM File");
  case MergeColumn:
    return tr("Merge");
  case SizeColumn:
    return tr("Size");
  case CrcColumn:
    return tr("CRC32");
  case Sha1Column:
    return tr("SHA1");
  case Md5Column:
    return tr("MD5");
  case MiaColumn:
    return tr("MIA");
  case InstanceColumn:
    return tr("Instances");
  default:
    return {};
  }
}

} // namespace DatAudit
