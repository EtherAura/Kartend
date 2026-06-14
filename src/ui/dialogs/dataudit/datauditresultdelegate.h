#ifndef DATAUDITRESULTDELEGATE_H
#define DATAUDITRESULTDELEGATE_H

#include <QColor>
#include <QIcon>
#include <QStyledItemDelegate>

#include "dataudittypes.h"

namespace DatAudit {

/// Paints audit-result rows with a semantic status tint + a status icon
/// (Kartend-m6qsb.20). Lives in the UI layer so DatAuditModel stays
/// QtCore-only and unit-testable: the delegate reads DatAuditModel::StatusRole
/// and maps it to colour/icon here, against the live widget palette.
///
/// The colour mapping is a free function so it can be unit-tested without a
/// running view: Have = green, Wrong-name / Duplicate = amber, Wrong-content /
/// Corrupt / Missing = red, Unknown / reserved = neutral. Tints are alpha-
/// blended so they read on both light and dark themes.
[[nodiscard]] QColor statusTint(Status status, const QColor &base);

/// Themed icon name for a status (first match wins via QIcon::fromTheme).
[[nodiscard]] QIcon statusIcon(Status status);

class DatAuditResultDelegate : public QStyledItemDelegate {
  Q_OBJECT
public:
  explicit DatAuditResultDelegate(QObject *parent = nullptr);

protected:
  // Tint (via backgroundBrush, so the style fills it reliably) + status icon
  // are applied here rather than in paint() — letting the style draw avoids
  // theme-specific overdraw that a manual fillRect would lose to.
  void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override;
};

} // namespace DatAudit

#endif // DATAUDITRESULTDELEGATE_H
