#ifndef DATTREEBADGEDELEGATE_H
#define DATTREEBADGEDELEGATE_H

#include <QStyledItemDelegate>

namespace DatAudit {

/// Tree delegate for the audit browser (Kartend-34lab): draws the node label +
/// icon normally, then right-aligned coloured count chips ("✓120 ✗5 MIA 2")
/// read from AuditTreeModel::CountsRole — RomVault's at-a-glance rollup, in KDE
/// colours that read on light and dark (the chip text colour is chosen for
/// contrast against each chip's fill). Zero-count buckets are omitted.
class DatTreeBadgeDelegate : public QStyledItemDelegate {
  Q_OBJECT
public:
  explicit DatTreeBadgeDelegate(QObject *parent = nullptr);

  void paint(QPainter *painter, const QStyleOptionViewItem &option,
             const QModelIndex &index) const override;
  [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem &option,
                               const QModelIndex &index) const override;
};

} // namespace DatAudit

#endif // DATTREEBADGEDELEGATE_H
