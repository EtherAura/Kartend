#ifndef DATAUDITMODEL_H
#define DATAUDITMODEL_H

#include <optional>

#include <QAbstractTableModel>
#include <QList>
#include <QSet>

#include "dataudittypes.h"

namespace DatAudit {

/// Table model backing the audit results view. Holds the full row set and an
/// optional visible-status filter (the "Files I own" vs "Catalogue
/// completeness" framing toggle, and per-status filtering, are both just
/// status subsets). Pure presentation over AuditRow — lives in the data layer
/// (QtCore only, no widgets) so its row/column/filter logic is unit-testable
/// without the UI binary.
class DatAuditModel : public QAbstractTableModel {
  Q_OBJECT
public:
  enum Column {
    StatusColumn = 0,
    GameColumn,
    ExpectedNameColumn,
    FileColumn,
    SizeColumn,
    ColumnCount,
  };

  /// Custom roles so a view delegate can read the raw Status (for icon/colour)
  /// and the absolute file path (for "open containing folder") without parsing
  /// the display strings.
  enum Roles {
    StatusRole = Qt::UserRole + 1, ///< returns int(Status)
    FilePathRole,                  ///< returns the absolute file path
  };

  explicit DatAuditModel(QObject *parent = nullptr);

  /// Replace the row set (resets any filter's effect by recomputing the
  /// visible index against the new rows).
  void setRows(QList<AuditRow> rows);
  [[nodiscard]] const QList<AuditRow> &allRows() const { return m_rows; }

  /// Restrict visible rows to these statuses. An empty optional shows all.
  void setVisibleStatuses(const std::optional<QSet<Status>> &statuses);

  /// The AuditRow behind a (possibly filtered) view row, or nullptr if out of
  /// range. Index is into the *visible* rows.
  [[nodiscard]] const AuditRow *rowAt(int visibleRow) const;

  /// Human-readable label for a status (column text + legend).
  [[nodiscard]] static QString statusLabel(Status s);

  // QAbstractTableModel
  [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  [[nodiscard]] int columnCount(const QModelIndex &parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
  [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                    int role = Qt::DisplayRole) const override;

private:
  void rebuildVisible();

  QList<AuditRow> m_rows;
  QList<int> m_visible; ///< indices into m_rows after filtering
  std::optional<QSet<Status>> m_filter;
};

} // namespace DatAudit

#endif // DATAUDITMODEL_H
