#ifndef CUSTOMFIELDSDIALOG_H
#define CUSTOMFIELDSDIALOG_H

#include <QDialog>

#include "itemmetadata.h"

QT_BEGIN_NAMESPACE
class QPushButton;
class QTableWidget;
QT_END_NAMESPACE

/// Per-item dialog for editing user-defined custom metadata fields
/// Operates on an in-memory `CustomFieldList`; the caller is
/// responsible for persisting via `DatabaseManager::saveItemMetadata()`.
///
/// The dialog accepts whatever the user types — empty/duplicate keys are
/// pruned by `ItemMetadataStore::serializeCustomFields()` rather than blocked
/// here, so partially-typed rows can survive a re-open without nagging.
class CustomFieldsDialog : public QDialog {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(CustomFieldsDialog)
public:
  explicit CustomFieldsDialog(QWidget *parent = nullptr);

  /// Header text shown above the table. Typically the displayed item name so
  /// the user knows which item they are editing.
  void setItemTitle(const QString &title);

  /// Replaces the current table contents with the provided fields.
  void setFields(const ItemMetadataStore::CustomFieldList &fields);

  /// Returns the table contents in display order. Empty trimmed keys are
  /// preserved so the caller can decide whether to drop or keep them; the
  /// store's serializer prunes them when saving.
  [[nodiscard]] ItemMetadataStore::CustomFieldList fields() const;

private slots:
  void onAddRow();
  void onRemoveSelected();

private:
  void setupUi();
  void appendRow(const QString &key, const QString &value);

  QTableWidget *m_table = nullptr;
  QPushButton *m_addButton = nullptr;
  QPushButton *m_removeButton = nullptr;
};

#endif // CUSTOMFIELDSDIALOG_H
