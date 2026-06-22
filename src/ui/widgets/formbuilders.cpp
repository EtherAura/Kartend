#include "formbuilders.h"

#include <QAbstractItemDelegate>
#include <QAbstractItemView>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTableView>
#include <QVBoxLayout>
#include <QWidget>

namespace FormBuilders {

void uniformLabelColumn(QFormLayout *form, int minWidth) {
  if (!form) return;
  for (int row = 0; row < form->rowCount(); ++row) {
    QLayoutItem *item = form->itemAt(row, QFormLayout::LabelRole);
    if (!item) continue;
    if (auto *label = qobject_cast<QLabel *>(item->widget())) {
      label->setMinimumWidth(minWidth);
    }
  }
}

QWidget *makeChipPair(QWidget *parent, const QString &label, QLineEdit *edit, int labelWidth,
                      int valueWidth, int spacing) {
  auto *w = new QWidget(parent);
  auto *h = new QHBoxLayout(w);
  h->setContentsMargins(0, 0, 0, 0);
  h->setSpacing(spacing);
  auto *lbl = new QLabel(label, w);
  lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  lbl->setFixedWidth(labelWidth);
  if (edit) {
    edit->setParent(w);
    edit->setFixedWidth(valueWidth);
  }
  h->addWidget(lbl);
  if (edit) h->addWidget(edit);
  const int height = edit ? edit->sizeHint().height() + 2 : lbl->sizeHint().height() + 2;
  w->setFixedSize(labelWidth + spacing + valueWidth, height);
  return w;
}

QGroupBox *makePathListGroup(const QString &title, QListWidget *&list, QPushButton *&add,
                             QPushButton *&remove, const QString &addText) {
  auto *box = new QGroupBox(title);
  auto *v = new QVBoxLayout(box);
  list = new QListWidget(box);
  list->setSelectionMode(QAbstractItemView::ExtendedSelection);
  v->addWidget(list);
  auto *row = new QHBoxLayout();
  add = new QPushButton(addText, box);
  remove = new QPushButton(QObject::tr("Remove"), box);
  row->addWidget(add);
  row->addWidget(remove);
  row->addStretch();
  v->addLayout(row);
  return box;
}

void configureResultTable(QTableView *table, QAbstractItemDelegate *delegate,
                          bool stretchLastSection) {
  if (!table) return;
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::SingleSelection);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->setSortingEnabled(true);
  table->setAlternatingRowColors(true);
  table->verticalHeader()->setVisible(false);
  table->horizontalHeader()->setStretchLastSection(stretchLastSection);
  if (delegate) {
    delegate->setParent(table);
    table->setItemDelegate(delegate);
  }
}

} // namespace FormBuilders
