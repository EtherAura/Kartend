#ifndef DATLIBRARYREVIEWDIALOG_H
#define DATLIBRARYREVIEWDIALOG_H

#include <functional>

#include <QDialog>
#include <QList>

#include "datcollectionmatch.h"
#include "datlibraryscan.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

/// Confirm-only review of the DAT-library scan's proposals
/// (Kartend-m6qsb.5). Each row pairs one catalogue file with a picker of the
/// collections it plausibly belongs to (best match preselected, every
/// candidate listed); the user attaches or dismisses rows explicitly —
/// nothing is ever applied silently. The dialog owns no persistence: every
/// side effect goes through the hooks the controller injects, so the widget
/// logic stays headlessly testable.
class DatLibraryReviewDialog : public QDialog {
  Q_OBJECT
public:
  /// Side-effect hooks, wired by DatAuditController. All optional — an unset
  /// hook turns the corresponding action into a no-op (tests / standalone).
  struct Hooks {
    /// Append `datPath` to the collection's DAT list and persist the
    /// collections INI.
    std::function<void(const QString &collectionUuid, const QString &datPath)> attach;
    /// Record a "don't ask again" for this catalogue revision.
    std::function<void(const QString &canonicalPath, qint64 mtimeMs)> dismiss;
    /// Re-scan `root` and return fresh proposals (synchronous; the library
    /// scan is a cheap header-probe pass).
    std::function<DatLibraryScan::ScanResult(const QString &root)> rescan;
    /// Persist a changed library-folder path.
    std::function<void(const QString &root)> saveLibraryPath;
    /// Create a brand-new collection for `datPath` (prompting the user) with
    /// the DAT already attached, and return its uuid — empty when the user
    /// cancels. Powers the combo's "Add to new collection…" choice.
    std::function<QString(const QString &datPath)> addToNewCollection;
  };

  DatLibraryReviewDialog(const QString &libraryPath,
                         const QList<DatCollectionMatch::CollectionInfo> &collections,
                         const DatLibraryScan::ScanResult &initial, Hooks hooks,
                         QWidget *parent = nullptr);

private slots:
  void onBrowseLibrary();
  void onRescan();
  void onAttachSelected();
  void onDismissSelected();
  void onAttachAllBestMatches();

private:
  void populate(const DatLibraryScan::ScanResult &result);
  void rebuildRows(); ///< Render m_result (proposals + optionally unmatched) into the tree.
  [[nodiscard]] QString collectionName(const QString &uuid) const;
  [[nodiscard]] int rowCount() const;

  QList<DatCollectionMatch::CollectionInfo> m_collections;
  DatLibraryScan::ScanResult m_result;         ///< Latest scan (proposals + unmatched).
  QList<DatLibraryScan::Proposal> m_proposals; ///< Currently-shown rows, row-indexed.
  Hooks m_hooks;

  QLineEdit *m_libraryPath = nullptr;
  QTreeWidget *m_tree = nullptr;
  QLabel *m_status = nullptr;
  QCheckBox *m_showUnmatched = nullptr;
  QPushButton *m_attachAllButton = nullptr;
};

#endif // DATLIBRARYREVIEWDIALOG_H
