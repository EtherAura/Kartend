#ifndef DATAUDITBROWSERPAGE_H
#define DATAUDITBROWSERPAGE_H

#include <QHash>
#include <QList>
#include <QStringList>
#include <QWidget>

#include "datauditbrowsermodels.h" // BrowserViewPreset

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QModelIndex;
class QPushButton;
class QTableView;
class QTreeView;

namespace DatAudit {
class AuditTreeModel;
class GameListModel;
class GameListFilterProxy;
class RomFileModel;
} // namespace DatAudit

/// The RomVault-style browser page of the DAT manager (Kartend-34lab): a global
/// tree of every audit profile's per-source rollups on the left, with a DAT-info
/// header + game list + ROM-file detail on the right, and completeness filters.
/// Read-only: it renders persisted audit results (no rescan), reusing the DAT
/// cache for ROM hashes. KDE-styled via palette roles so it retints with the
/// system accent.
class DatAuditBrowserPage : public QWidget {
  Q_OBJECT
public:
  explicit DatAuditBrowserPage(QWidget *parent = nullptr);

  /// Rebuild the tree from the current profiles + their persisted rollups.
  /// Called when the page is shown (the dialog drives this lazily).
  void refresh();

private slots:
  void onTreeSelectionChanged();
  void onGameSelectionChanged();
  void applyFilters();

private:
  void buildUi();
  void clearDatInfo();
  void loadGamesFor(qint64 profileId, const QString &sourceName, const QString &datPath);

  // Named view presets (Kartend-7iqhl.1): each captures the filter
  // configuration only (the 5 gates + group-by-folder + search), so recall is
  // profile-independent.
  void loadPresets();                                   ///< pull all slots from QSettings into the combo
  void recallPreset(int slot);                          ///< apply the slot's captured view to the controls
  void saveCurrentToPreset();                           ///< overwrite the selected slot with the live view + persist
  void renameSelectedPreset();                          ///< retitle the selected slot via QInputDialog + persist
  [[nodiscard]] DatAudit::BrowserViewPreset captureView() const; ///< the live filter state as a preset payload

  QTreeView *m_tree = nullptr;
  DatAudit::AuditTreeModel *m_treeModel = nullptr;

  // DAT-info header fields.
  QLabel *m_infoName = nullptr;
  QLabel *m_infoDescription = nullptr;
  QLabel *m_infoVersion = nullptr;
  QLabel *m_infoPath = nullptr;
  QLabel *m_infoCounts = nullptr;

  QTableView *m_gameTable = nullptr;
  DatAudit::GameListModel *m_gameModel = nullptr;
  DatAudit::GameListFilterProxy *m_gameProxy = nullptr;

  QTableView *m_romTable = nullptr;
  DatAudit::RomFileModel *m_romModel = nullptr;

  QCheckBox *m_filterComplete = nullptr;
  QCheckBox *m_filterPartial = nullptr;
  QCheckBox *m_filterEmpty = nullptr;
  QCheckBox *m_filterFixes = nullptr;
  QCheckBox *m_filterMia = nullptr;
  /// Kartend-m6qsb.30: when checked, the game list regroups the source's audit
  /// rows by their containing item-folder ("Game A: 2/3 present") instead of by
  /// DAT game.
  QCheckBox *m_groupByFolder = nullptr;
  QLineEdit *m_search = nullptr;

  // Preset controls (Kartend-7iqhl.1).
  QComboBox *m_presetCombo = nullptr;
  QPushButton *m_presetSave = nullptr;
  QPushButton *m_presetRename = nullptr;
  QList<DatAudit::BrowserViewPreset> m_presets;

  // The (profileId, sourceName, datPath) the game list is currently showing.
  qint64 m_currentProfileId = -1;
  QString m_currentSourceName;
  QString m_currentDatPath;
  /// Scan roots per profile id (Kartend-m6qsb.30), cached on refresh() so the
  /// folder-as-item view can derive each file's item-folder relative to a root.
  QHash<qint64, QStringList> m_scanRootsByProfile;
};

#endif // DATAUDITBROWSERPAGE_H
