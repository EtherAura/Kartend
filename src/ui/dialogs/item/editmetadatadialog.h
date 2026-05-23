#ifndef EDITMETADATADIALOG_H
#define EDITMETADATADIALOG_H

#include <QDialog>

#include "itemmetadata.h"

QT_BEGIN_NAMESPACE
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
QT_END_NAMESPACE

class StarRatingWidget;

/// Per-item editor for notes, tags, rating, source URL, and custom
/// key/value pairs. Operates on an in-memory `EditMetadataPayload`; the
/// caller persists via `DatabaseManager::saveItemMetadata()`.
///
/// Partial input survives a round-trip: empty rows in the custom-fields
/// table are pruned by the store's serializer rather than blocked here, the
/// tags line accepts arbitrary commas (split + trimmed on save), and the
/// rating widget supports a "right-click to clear" gesture for the unset
/// state.
class EditMetadataDialog : public QDialog {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(EditMetadataDialog)
public:
  explicit EditMetadataDialog(QWidget *parent = nullptr);

  /// Header text shown above the form. Typically the displayed item name so
  /// the user knows which item they are editing.
  void setItemTitle(const QString &title);

  /// Replaces the current field values with the provided payload.
  void setPayload(const EditMetadataPayload &payload);

  /// Returns the form contents. Tag splitting / trimming happens here so
  /// the caller always gets a clean QStringList regardless of how messy the
  /// user's commas were.
  [[nodiscard]] EditMetadataPayload payload() const;

private slots:
  void onAddCustomFieldRow();
  void onRemoveSelectedCustomFieldRows();

private:
  void setupUi();
  void appendCustomFieldRow(const QString &key, const QString &value);

  QLabel *m_headerLabel = nullptr;
  QPlainTextEdit *m_notesEdit = nullptr;
  QLineEdit *m_tagsEdit = nullptr;
  StarRatingWidget *m_ratingWidget = nullptr;
  QPushButton *m_clearRatingButton = nullptr;
  QLabel *m_ratingValueLabel = nullptr;
  QLineEdit *m_sourceUrlEdit = nullptr;
  QTableWidget *m_customFieldsTable = nullptr;
  QPushButton *m_addRowButton = nullptr;
  QPushButton *m_removeRowButton = nullptr;
};

#endif // EDITMETADATADIALOG_H
