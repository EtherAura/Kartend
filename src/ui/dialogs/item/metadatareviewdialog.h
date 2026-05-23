#ifndef METADATAREVIEWDIALOG_H
#define METADATAREVIEWDIALOG_H

#include <functional>
#include <QDialog>
#include <QList>

#include "metadataqueue.h"

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
QT_END_NAMESPACE

/// One-item-at-a-time review dialog for the missing-metadata workflow.
/// Caller hands in the queue + an edit closure; the dialog walks the
/// queue in order, letting the user edit, skip, or stop. After each
/// edit, the dialog re-evaluates the entry via the supplied
/// `reloadMetadata` closure — fully-populated rows drop off, partial
/// rows stay queued with their updated missing-field list.
class MetadataReviewDialog : public QDialog {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(MetadataReviewDialog)
public:
  /// Caller-supplied editor: opens the per-item metadata dialog and
  /// returns true when the user saved (caller has already persisted via
  /// DatabaseManager). False = cancelled.
  using EditHandler = std::function<bool(const QString &filePath, const QString &itemName)>;

  /// Reloads the current per-item metadata after an edit so the queue
  /// can re-evaluate whether the entry still belongs. Caller routes
  /// through DatabaseManager::loadItemMetadata.
  using ReloadHandler =
      std::function<ItemMetadataStore::ItemMetadata(const QString &uuid, const QString &filePath)>;

  explicit MetadataReviewDialog(QWidget *parent = nullptr);

  /// Provide the entries (already filtered by MetadataQueue::build) and
  /// the closures that bridge to the DatabaseManager / EditMetadataDialog.
  void setQueue(QList<MetadataQueue::Entry> entries, EditHandler onEdit, ReloadHandler onReload);

private slots:
  void onEditClicked();
  void onSkipClicked();

private:
  void setupUi();
  void renderCurrent();
  /// Advances past the current row (used by both Skip and post-edit
  /// when the row was fully populated). Closes the dialog when the
  /// queue is exhausted.
  void advance();

  QLabel *m_headerLabel = nullptr;
  QLabel *m_progressLabel = nullptr;
  QLabel *m_missingLabel = nullptr;
  QLabel *m_pathLabel = nullptr;
  QPushButton *m_editButton = nullptr;
  QPushButton *m_skipButton = nullptr;
  QPushButton *m_closeButton = nullptr;

  QList<MetadataQueue::Entry> m_entries;
  EditHandler m_onEdit;
  ReloadHandler m_onReload;
  int m_cursor = 0;
};

#endif // METADATAREVIEWDIALOG_H
