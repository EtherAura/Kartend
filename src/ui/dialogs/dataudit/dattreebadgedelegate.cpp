#include "dattreebadgedelegate.h"

#include <QFontMetrics>
#include <QPainter>

#include "datauditbrowsermodels.h"
#include "datauditbuckets.h"

namespace DatAudit {

namespace {

struct Chip {
  QColor fill;
  int count = 0;
};

// Non-zero buckets as coloured chips, in a fixed display order. Semantic hues
// (green good, orange-red gap, amber fixable, salmon MIA) — universal meaning,
// like the row tint; the chip text colour is picked for contrast per chip.
QList<Chip> chipsFor(const BucketCounts &c) {
  QList<Chip> chips;
  if (c.have > 0) {
    chips.append({QColor(0x2e, 0xcc, 0x71), c.have});
  }
  if (c.missing > 0) {
    chips.append({QColor(0xe6, 0x7e, 0x22), c.missing});
  }
  if (c.fixable > 0) {
    chips.append({QColor(0xf1, 0xc4, 0x0f), c.fixable});
  }
  if (c.mia > 0) {
    chips.append({QColor(0xd9, 0x7a, 0x8a), c.mia}); // salmon
  }
  return chips;
}

// Black or white text, whichever contrasts better with the chip fill.
QColor textOn(const QColor &fill) {
  const double luma = 0.299 * fill.red() + 0.587 * fill.green() + 0.114 * fill.blue();
  return luma > 150.0 ? QColor(Qt::black) : QColor(Qt::white);
}

constexpr int kChipPadH = 6;   // horizontal text padding inside a chip
constexpr int kChipGap = 4;    // gap between chips
constexpr int kChipMargin = 6; // right margin

int chipsWidth(const QList<Chip> &chips, const QFontMetrics &fm) {
  int w = 0;
  for (const Chip &c : chips) {
    w += fm.horizontalAdvance(QString::number(c.count)) + 2 * kChipPadH + kChipGap;
  }
  return w > 0 ? w + kChipMargin : 0;
}

} // namespace

DatTreeBadgeDelegate::DatTreeBadgeDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

void DatTreeBadgeDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                 const QModelIndex &index) const {
  const auto counts = qvariant_cast<BucketCounts>(index.data(AuditTreeModel::CountsRole));
  const QList<Chip> chips = chipsFor(counts);
  const int reserve = chipsWidth(chips, option.fontMetrics);

  // Draw the standard item (branch indicator, icon, label) into a rect that
  // leaves room for the chips on the right.
  QStyleOptionViewItem base(option);
  base.rect.adjust(0, 0, -reserve, 0);
  QStyledItemDelegate::paint(painter, base, index);

  if (chips.isEmpty()) {
    return;
  }

  painter->save();
  painter->setRenderHint(QPainter::Antialiasing, true);
  const int h = qMin(option.rect.height() - 4, option.fontMetrics.height() + 2);
  const int y = option.rect.center().y() - h / 2;
  // Anchor the chip strip at the right edge (reserve already includes the
  // right margin), then draw left-to-right in display order.
  int x = option.rect.right() - reserve;
  for (const Chip &chip : chips) {
    const QString text = QString::number(chip.count);
    const int w = option.fontMetrics.horizontalAdvance(text) + 2 * kChipPadH;
    const QRect chipRect(x, y, w, h);
    painter->setPen(Qt::NoPen);
    painter->setBrush(chip.fill);
    painter->drawRoundedRect(chipRect, h / 2.0, h / 2.0);
    painter->setPen(textOn(chip.fill));
    painter->drawText(chipRect, Qt::AlignCenter, text);
    x += w + kChipGap;
  }
  painter->restore();
}

QSize DatTreeBadgeDelegate::sizeHint(const QStyleOptionViewItem &option,
                                     const QModelIndex &index) const {
  QSize s = QStyledItemDelegate::sizeHint(option, index);
  const auto counts = qvariant_cast<BucketCounts>(index.data(AuditTreeModel::CountsRole));
  s.setWidth(s.width() + chipsWidth(chipsFor(counts), option.fontMetrics));
  s.setHeight(qMax(s.height(), option.fontMetrics.height() + 8));
  return s;
}

} // namespace DatAudit
