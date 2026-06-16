#ifndef DATAUDITBROWSERPAGE_H
#define DATAUDITBROWSERPAGE_H

#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QModelIndex;
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
  QLineEdit *m_search = nullptr;

  // The (profileId, sourceName, datPath) the game list is currently showing.
  qint64 m_currentProfileId = -1;
  QString m_currentSourceName;
  QString m_currentDatPath;
};

#endif // DATAUDITBROWSERPAGE_H
