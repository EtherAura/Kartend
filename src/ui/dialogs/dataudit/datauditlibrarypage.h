#ifndef DATAUDITLIBRARYPAGE_H
#define DATAUDITLIBRARYPAGE_H

#include <QWidget>

class QLabel;
class QPushButton;

/// The "DAT library" stacked page of the DAT Manager (Kartend-oa0lu): the
/// watched-library folder + scan/review + check-for-updates actions, and the
/// DAT-import actions. A pure view — it owns its widgets and emits intent
/// signals; DatAuditDialog keeps the orchestration (the library opener, the
/// update-check, the import handler) and drives button state through the
/// setters. Mirrors the DatAuditBrowserPage precedent.
class DatAuditLibraryPage : public QWidget {
  Q_OBJECT

public:
  explicit DatAuditLibraryPage(QWidget *parent = nullptr);

  /// Empty path shows the "No library folder set." placeholder.
  void setLibraryPathText(const QString &path);
  void setReviewEnabled(bool enabled);
  void setCheckUpdatesVisible(bool visible);
  /// While a check is running: disables the button and shows "Checking…".
  void setCheckUpdatesBusy(bool busy);
  void setImportButtonsVisible(bool visible);

signals:
  void reviewRequested();
  void checkUpdatesRequested();
  void importZipRequested();
  void importFolderRequested();

private:
  QLabel *m_pathLabel = nullptr;
  QPushButton *m_reviewButton = nullptr;
  QPushButton *m_checkUpdatesButton = nullptr;
  QPushButton *m_importZipButton = nullptr;
  QPushButton *m_importFolderButton = nullptr;
};

#endif // DATAUDITLIBRARYPAGE_H
