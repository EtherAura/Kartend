#include "flowlayout.h"

#include <QLayoutItem>
#include <QPoint>

FlowLayout::FlowLayout(QWidget *parent, int margin, int hSpacing, int vSpacing)
    : QLayout(parent), m_hSpace(hSpacing), m_vSpace(vSpacing) {
  setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::~FlowLayout() {
  while (QLayoutItem *item = takeAt(0)) delete item;
}

void FlowLayout::addItem(QLayoutItem *item) {
  m_items.append(item);
}

int FlowLayout::heightForWidth(int width) const {
  return doLayout(QRect(0, 0, width, 0), true);
}

QLayoutItem *FlowLayout::takeAt(int idx) {
  return (idx >= 0 && idx < m_items.size()) ? m_items.takeAt(idx) : nullptr;
}

QSize FlowLayout::minimumSize() const {
  QSize s;
  for (auto *it : m_items) s = s.expandedTo(it->minimumSize());
  int l, t, r, b;
  getContentsMargins(&l, &t, &r, &b);
  s += QSize(l + r, t + b);
  return s;
}

void FlowLayout::setGeometry(const QRect &r) {
  QLayout::setGeometry(r);
  doLayout(r, false);
}

int FlowLayout::doLayout(const QRect &rect, bool testOnly) const {
  int l, t, r, b;
  getContentsMargins(&l, &t, &r, &b);
  QRect eff = rect.adjusted(l, t, -r, -b);
  int x = eff.x();
  int y = eff.y();
  int lineH = 0;
  for (auto *it : m_items) {
    const QSize sz = it->sizeHint();
    int nextX = x + sz.width() + m_hSpace;
    if (nextX - m_hSpace > eff.right() && lineH > 0) {
      x = eff.x();
      y += lineH + m_vSpace;
      nextX = x + sz.width() + m_hSpace;
      lineH = 0;
    }
    if (!testOnly) it->setGeometry(QRect(QPoint(x, y), sz));
    x = nextX;
    lineH = qMax(lineH, sz.height());
  }
  return y + lineH - rect.y() + b;
}
