#ifndef DATAUDITCONTROLLER_H
#define DATAUDITCONTROLLER_H

#include <functional>

#include <QObject>
#include <QString>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

class DatAuditDialog;
struct CollectionConfig;

template <typename T> class QList;

/// Owns the DAT Audit sub-window, mirroring ScraperController's role for the
/// scraper: the dialog is constructed lazily on first open and reused across
/// opens (hidden, not destroyed, on close), so it survives the user closing
/// the window. Lives on MainWindow as a unique_ptr(nullptr-parent) member.
///
/// The audit dialog is self-contained (it picks its own DAT files + scan
/// folders and runs the engine off-thread); beyond the parent window it borrows
/// MainWindow's live collection list so the profile editor can offer an
/// optional "Linked collection" picker (Kartend-x9mkif.3). Profile-driven
/// launches remain a later follow-up.
struct DatAuditControllerContext {
  std::function<QWidget *()> getParentWindow;

  /// The live collection list (each collection's UUID is derived from its name
  /// + media dir), borrowed from MainWindow. Null/empty is fine — the picker
  /// then offers only "(none)".
  std::function<QList<CollectionConfig> *()> getCollections;
};

class DatAuditController : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(DatAuditController)
public:
  explicit DatAuditController(QObject *parent = nullptr);
  ~DatAuditController() override;

  void setContext(const DatAuditControllerContext &context);

public slots:
  /// Show (or raise) the DAT Audit window, constructing it on first call.
  void openDialog();

  /// Show the DAT Audit window aimed at a specific collection (Kartend-4mqkof,
  /// launched from collection settings): selects the linked audit profile if
  /// one exists, otherwise seeds an unsaved working profile from the collection.
  /// See DatAuditDialog::openForCollection.
  void openForCollection(const QString &collectionUuid, const QString &collectionName,
                         const QString &mediaDir, const QStringList &datPaths);

private:
  /// Build the dialog on first use and refresh its borrowed collection list.
  /// Does not show — callers raise it after any per-open setup.
  DatAuditDialog *ensureDialog();

  DatAuditControllerContext m_ctx;
  DatAuditDialog *m_dialog = nullptr;
};

#endif // DATAUDITCONTROLLER_H
