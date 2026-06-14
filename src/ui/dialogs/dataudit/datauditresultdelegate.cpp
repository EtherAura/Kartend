#include "datauditresultdelegate.h"

#include "datauditmodel.h"

namespace DatAudit {

namespace {

// Semantic hue per status — fixed, conventional colours (green good, red bad)
// rather than palette-derived, since the meaning is universal. Blended into the
// row base in statusTint() so it stays subtle on light and dark themes.
QColor statusHue(Status status) {
  switch (status) {
  case Status::Have:
    return {0x2e, 0xcc, 0x71}; // green
  case Status::WrongName:
  case Status::Duplicate:
    return {0xf1, 0xc4, 0x0f}; // amber
  case Status::WrongHash:
  case Status::Corrupt:
    return {0xe7, 0x4c, 0x3c}; // red
  case Status::Missing:
    return {0xe6, 0x7e, 0x22}; // muted orange-red (a gap, not a corruption)
  case Status::Unknown:
  case Status::Unscanned:
  case Status::BadDump:
    break;
  }
  return {}; // invalid → no tint
}

} // namespace

QColor statusTint(Status status, const QColor &base) {
  const QColor hue = statusHue(status);
  if (!hue.isValid()) {
    return base; // Unknown / reserved — leave the row as-is
  }
  // Blend ~18% of the semantic hue over the base so the tint is legible but
  // never overwhelms the text. Works for both light and dark bases.
  constexpr double k = 0.18;
  return QColor(static_cast<int>(base.red() * (1 - k) + hue.red() * k),
                static_cast<int>(base.green() * (1 - k) + hue.green() * k),
                static_cast<int>(base.blue() * (1 - k) + hue.blue() * k));
}

QIcon statusIcon(Status status) {
  switch (status) {
  case Status::Have:
    return QIcon::fromTheme(QStringLiteral("emblem-success"),
                            QIcon::fromTheme(QStringLiteral("dialog-ok")));
  case Status::WrongName:
    return QIcon::fromTheme(QStringLiteral("document-edit"));
  case Status::WrongHash:
    return QIcon::fromTheme(QStringLiteral("dialog-warning"));
  case Status::Duplicate:
    return QIcon::fromTheme(QStringLiteral("edit-copy"));
  case Status::Unknown:
    return QIcon::fromTheme(QStringLiteral("dialog-question"));
  case Status::Corrupt:
    return QIcon::fromTheme(QStringLiteral("dialog-error"));
  case Status::Missing:
    return QIcon::fromTheme(QStringLiteral("emblem-unavailable"),
                            QIcon::fromTheme(QStringLiteral("list-remove")));
  case Status::Unscanned:
  case Status::BadDump:
    break;
  }
  return {};
}

DatAuditResultDelegate::DatAuditResultDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

void DatAuditResultDelegate::initStyleOption(QStyleOptionViewItem *option,
                                             const QModelIndex &index) const {
  QStyledItemDelegate::initStyleOption(option, index);
  const auto status = static_cast<Status>(index.data(DatAuditModel::StatusRole).toInt());

  // Row tint via backgroundBrush so the style fills it (survives all themes);
  // skip when selected so the highlight wins.
  if ((option->state & QStyle::State_Selected) == 0) {
    const QColor base = option->palette.color(QPalette::Base);
    const QColor tint = statusTint(status, base);
    if (tint != base) {
      option->backgroundBrush = tint;
    }
  }

  // Icon only on the Status column — a per-row glyph without crowding the rest.
  if (index.column() == DatAuditModel::StatusColumn) {
    const QIcon icon = statusIcon(status);
    if (!icon.isNull()) {
      option->features |= QStyleOptionViewItem::HasDecoration;
      option->icon = icon;
      option->decorationSize = QSize(16, 16);
    }
  }
}

} // namespace DatAudit
