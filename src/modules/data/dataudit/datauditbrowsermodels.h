#ifndef DATAUDITBROWSERMODELS_H
#define DATAUDITBROWSERMODELS_H

#include <QAbstractItemModel>
#include <QAbstractTableModel>
#include <QList>
#include <QSortFilterProxyModel>
#include <QString>

#include "datauditbuckets.h"
#include "datauditprofile.h"
#include "datlookup.h"

/// Models backing the RomVault-style DAT-audit browser (Kartend-34lab).
///
/// All three are fed via setters from the dialog (which owns the DB I/O), so
/// they stay QtCore-only and unit-testable — the same separation DatAuditModel
/// uses. The tree spans every profile and never holds games/roms (those load
/// lazily into the right-hand tables on selection), keeping MAME-scale data out
/// of the tree.
namespace DatAudit {

/// Left-pane tree: Root → Profile → Source DAT (Source children only when a
/// profile contributed more than one DAT). Each node exposes rolled-up bucket
/// counts; games/roms are NOT in the tree.
class AuditTreeModel : public QAbstractItemModel {
  Q_OBJECT
public:
  enum class NodeKind { Profile, Source };

  enum Roles {
    KindRole = Qt::UserRole + 1, ///< int(NodeKind)
    ProfileIdRole,               ///< qint64 profile id
    SourceNameRole,              ///< QString DAT filename (Source nodes)
    DatPathRole,                 ///< QString absolute DAT path (Source nodes; for catalogue)
    CountsRole,                  ///< BucketCounts (for the badge delegate)
    UnscannedRole,               ///< bool — profile never audited
  };

  explicit AuditTreeModel(QObject *parent = nullptr);

  /// Rebuild the whole tree from the profile list (names + dats) and every
  /// profile's per-(source,status,mia) rollup rows (one DB read each). Named
  /// setTree (not setData) so it does not shadow QAbstractItemModel::setData.
  void setTree(const QList<DatAuditProfile::Profile> &profiles,
               const QList<DatAuditProfile::RollupRow> &rollups);

  /// Profile id / source name / DAT path for an index (for lazy game loading).
  [[nodiscard]] qint64 profileIdAt(const QModelIndex &index) const;
  [[nodiscard]] QString sourceNameAt(const QModelIndex &index) const;
  [[nodiscard]] QString datPathAt(const QModelIndex &index) const;

  // QAbstractItemModel
  [[nodiscard]] QModelIndex index(int row, int column,
                                  const QModelIndex &parent = QModelIndex()) const override;
  [[nodiscard]] QModelIndex parent(const QModelIndex &index) const override;
  [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  [[nodiscard]] int columnCount(const QModelIndex &parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

private:
  struct Node {
    NodeKind kind = NodeKind::Profile;
    int parent = -1;       ///< index into m_nodes; -1 only for the synthetic root
    QList<int> children;   ///< indices into m_nodes
    qint64 profileId = -1; ///< Profile + Source nodes
    QString sourceName;    ///< Source nodes
    QString datPath;       ///< Source nodes (and single-source Profile nodes)
    QString label;
    BucketCounts counts;
    bool unscanned = false;
  };

  [[nodiscard]] const Node *nodeFor(const QModelIndex &index) const;

  QList<Node> m_nodes; ///< m_nodes[0] is the synthetic (invisible) root
};

/// Right-top game list for the selected tree node. One row per game (grouped
/// from the source's per-(game,status,mia) rollups), with a coarse state for
/// the Complete/Partial/Empty/Fixes/MIA filter.
class GameListModel : public QAbstractTableModel {
  Q_OBJECT
public:
  enum Column { GameColumn = 0, HaveColumn, MissingColumn, FixableColumn, MiaColumn, ColumnCount };

  enum Roles {
    BucketStatusRole =
        Qt::UserRole + 1, ///< int(Status) of the dominant bucket, for the delegate tint
    GameStateRole,        ///< int(GameState)
    HasFixesRole,         ///< bool
    HasMiaRole,           ///< bool
    GameNameRole,         ///< QString
  };

  explicit GameListModel(QObject *parent = nullptr);

  /// Replace the rows from a source's game rollups (one row per distinct game).
  void setGames(const QList<DatAuditProfile::GameRollupRow> &rollups);
  void clear();

  [[nodiscard]] QString gameNameAt(int row) const;

  [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  [[nodiscard]] int columnCount(const QModelIndex &parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
  [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                    int role = Qt::DisplayRole) const override;

private:
  struct GameRow {
    QString gameName;
    BucketCounts counts;
    GameState state = GameState::Empty;
    bool hasFixes = false;
    bool hasMia = false;
  };
  QList<GameRow> m_rows;
};

/// Filters the game list by the completeness checkboxes + a name search.
class GameListFilterProxy : public QSortFilterProxyModel {
  Q_OBJECT
public:
  explicit GameListFilterProxy(QObject *parent = nullptr);

  /// Which of {Complete, Partial, Empty} states pass, plus the orthogonal
  /// Fixes / MIA gates. Empty state set shows nothing; the two bools, when
  /// true, additionally *require* that property.
  void setStateFilter(bool complete, bool partial, bool empty);
  void setRequireFixes(bool on);
  void setRequireMia(bool on);

protected:
  [[nodiscard]] bool filterAcceptsRow(int sourceRow,
                                      const QModelIndex &sourceParent) const override;

private:
  bool m_complete = true;
  bool m_partial = true;
  bool m_empty = true;
  bool m_requireFixes = false;
  bool m_requireMia = false;
};

/// Right-bottom ROM detail for the selected game: one row per catalogue ROM,
/// joined to its audit status by romName.
class RomFileModel : public QAbstractTableModel {
  Q_OBJECT
public:
  enum Column {
    GotColumn = 0,
    RomColumn,
    MergeColumn,
    SizeColumn,
    CrcColumn,
    Sha1Column,
    Md5Column,
    MiaColumn,
    InstanceColumn,
    ColumnCount,
  };

  enum Roles {
    StatusRole = Qt::UserRole + 1, ///< int(Status) for the delegate tint/icon
  };

  explicit RomFileModel(QObject *parent = nullptr);

  /// Replace the rows from the selected game's catalogue records (hashes /
  /// clone / mia) joined with its persisted result rows (status / file path).
  void setGame(const QList<DatLookup::DatRecord> &records,
               const QList<DatAuditProfile::ResultRow> &results);
  void clear();

  [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  [[nodiscard]] int columnCount(const QModelIndex &parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
  [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                    int role = Qt::DisplayRole) const override;

private:
  struct RomRow {
    DatLookup::DatRecord rec;
    Status status = Status::Missing;
    QString filePath;
    int instanceCount = 0;
  };
  QList<RomRow> m_rows;
};

} // namespace DatAudit

#endif // DATAUDITBROWSERMODELS_H
