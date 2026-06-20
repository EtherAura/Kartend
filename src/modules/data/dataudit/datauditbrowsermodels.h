#ifndef DATAUDITBROWSERMODELS_H
#define DATAUDITBROWSERMODELS_H

#include <QAbstractItemModel>
#include <QAbstractTableModel>
#include <QList>
#include <QSortFilterProxyModel>
#include <QString>

#include "datauditbuckets.h"
#include "datauditprofile.h"
#include "dataudittypes.h" // AuditRow
#include "datlookup.h"

class QSettings;

/// Models backing the RomVault-style DAT-audit browser (Kartend-34lab).
///
/// All three are fed via setters from the dialog (which owns the DB I/O), so
/// they stay QtCore-only and unit-testable — the same separation DatAuditModel
/// uses. The tree spans every profile and never holds games/roms (those load
/// lazily into the right-hand tables on selection), keeping MAME-scale data out
/// of the tree.
namespace DatAudit {

/// Per-item-folder rollup for SubfolderPerItem presentation (Kartend-m6qsb.30):
/// one entry per containing item-folder, with the bucket counts of the audit
/// rows that fall in it.
struct FolderRollup {
  QString folder;
  BucketCounts counts;
  QList<DatAuditProfile::ResultRow> rows; ///< rows in this folder, for the rom-detail drill-down
};

/// Group persisted audit-result rows by their containing item-folder so the
/// browser can present "Game A: 2/3 present" as one unit (Kartend-m6qsb.30). A
/// row with a file is keyed by the first path component of its filePath relative
/// to whichever scan root contains it (the item-folder); a Missing row (no file)
/// is keyed by its gameName, which equals the folder name in a SubfolderPerItem
/// layout, so absent ROMs count toward the folder's total. Folders surface in
/// first-seen order.
[[nodiscard]] QList<FolderRollup>
groupResultsByFolder(const QList<DatAuditProfile::ResultRow> &results,
                     const QStringList &scanRoots);

/// Reconstruct fix-engine AuditRows from a profile's persisted audit snapshot
/// (Kartend-7iqhl.2), so the read-only browser can open the Fix dialog without
/// first running a fresh audit. computeFixPlan() only consults status /
/// filePath / expectedName / gameName / actualName — all recoverable from a
/// ResultRow (detail carries the DAT romName == expectedName; actualName is the
/// filePath basename) — so the hash/size fields the snapshot drops do not
/// matter. Even archive repack reconstructs faithfully: member rows persist
/// their virtual "container/member" path + expected name, which is all
/// planArchiveContainer needs. The only limitation is that the snapshot may
/// predate on-disk changes — a staleness caveat shared by every fix kind and
/// guarded by the Fix dialog's preview-before-apply.
[[nodiscard]] QList<AuditRow>
auditRowsFromResults(const QList<DatAuditProfile::ResultRow> &results);

/// A saved browser view (Kartend-7iqhl.1): the read-only browser's filter
/// configuration only — the five completeness gates, the group-by-folder
/// toggle, and the game-search text. Deliberately profile-independent (no tree
/// node / DAT path), so recalling a preset just reconfigures the view and never
/// dangles on a profile that has since been deleted. Four named slots persist
/// via QSettings.
struct BrowserViewPreset {
  QString name;
  bool complete = true;  ///< show Complete games
  bool partial = true;   ///< show Partial games
  bool empty = true;     ///< show Empty games
  bool fixes = false;    ///< require: only games with fixes
  bool mia = false;      ///< require: only games with MIA
  bool groupByFolder = false; ///< folder-as-item game grouping (Kartend-m6qsb.30)
  QString search;        ///< game-name filter text
};

/// Number of preset slots (RomVault uses four).
inline constexpr int kBrowserPresetCount = 4;

/// Load all `kBrowserPresetCount` presets from `settings`. The helper manages
/// its own "DatAuditBrowserPresets" group, so callers pass the shared
/// QSettings("kartend","ui-state") (or, in tests, a temp IniFormat instance).
/// A slot that was never saved comes back with the page's default filters and
/// the name "Preset N".
[[nodiscard]] QList<BrowserViewPreset> loadBrowserPresets(QSettings &settings);

/// Persist one preset `slot` (0-based, < kBrowserPresetCount) into `settings`
/// under the "DatAuditBrowserPresets" group. Out-of-range slots are ignored.
void saveBrowserPreset(QSettings &settings, int slot, const BrowserViewPreset &preset);

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
  /// Re-display the table grouped by item-folder instead of by game
  /// (Kartend-m6qsb.30): each FolderRollup is one row, its folder name shown in
  /// the game column with the folder's rolled-up completeness.
  void setFolders(const QList<FolderRollup> &folders);
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
    ZipIndexColumn, ///< archive member's central-directory index (Kartend-7iqhl.4)
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
    int zipIndex = -1; ///< archive member index, -1 when not an archive member
  };
  QList<RomRow> m_rows;
};

} // namespace DatAudit

#endif // DATAUDITBROWSERMODELS_H
