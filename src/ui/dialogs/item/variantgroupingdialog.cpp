#include "variantgroupingdialog.h"

#include "uiconstants/color.h"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

VariantGroupingDialog::VariantGroupingDialog(const QList<VariantGrouping::VariantGroup> &groups,
                                             QWidget *parent)
    : QDialog(parent) {
  setupUi(groups);
}

void VariantGroupingDialog::setupUi(const QList<VariantGrouping::VariantGroup> &groups) {
  setWindowTitle(tr("Duplicates and variants"));
  resize(620, 460);

  auto *outer = new QVBoxLayout(this);

  auto *intro = new QLabel(tr("Items sharing the same name with different extensions or tags. "
                              "Select a row to launch or jump to it; individual files remain "
                              "accessible from the grid."),
                           this);
  intro->setWordWrap(true);
  intro->setStyleSheet(UIConstants::Color::MUTED_ITALIC_TEXT);
  outer->addWidget(intro);

  m_summaryLabel = new QLabel(this);
  outer->addWidget(m_summaryLabel);

  m_tree = new QTreeWidget(this);
  m_tree->setColumnCount(2);
  m_tree->setHeaderLabels({tr("Variant"), tr("Path")});
  m_tree->header()->setStretchLastSection(true);
  m_tree->setRootIsDecorated(true);
  m_tree->setUniformRowHeights(true);
  outer->addWidget(m_tree, /*stretch=*/1);

  int totalVariants = 0;
  for (const VariantGrouping::VariantGroup &group : groups) {
    auto *root = new QTreeWidgetItem(
        m_tree, {tr("%1 (%2 variants)").arg(group.baseName).arg(group.paths.size()), QString()});
    root->setFirstColumnSpanned(true);
    root->setExpanded(true);
    for (const QString &path : group.paths) {
      const QString ext = QFileInfo(path).suffix();
      const QString label =
          ext.isEmpty() ? QFileInfo(path).fileName() : QStringLiteral(".") + ext.toLower();
      auto *row = new QTreeWidgetItem(root, {label, path});
      row->setData(0, Qt::UserRole, path);
    }
    totalVariants += group.paths.size();
  }
  m_tree->resizeColumnToContents(0);

  m_summaryLabel->setText(
      tr("%1 group(s), %2 variant file(s)").arg(groups.size()).arg(totalVariants));

  auto *row = new QHBoxLayout();
  auto *launchButton = new QPushButton(tr("Launch selected"), this);
  auto *selectButton = new QPushButton(tr("Reveal in grid"), this);
  row->addWidget(launchButton);
  row->addWidget(selectButton);
  row->addStretch(1);
  outer->addLayout(row);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  outer->addWidget(buttons);

  connect(launchButton, &QPushButton::clicked, this, &VariantGroupingDialog::onLaunchClicked);
  connect(selectButton, &QPushButton::clicked, this, &VariantGroupingDialog::onSelectClicked);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

  // Double-click launches by default — matches the grid's primary
  // activation gesture so muscle-memory carries across.
  connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
          [this](QTreeWidgetItem *, int) { onLaunchClicked(); });
}

QString VariantGroupingDialog::currentVariantPath() const {
  if (!m_tree) return QString();
  QTreeWidgetItem *item = m_tree->currentItem();
  if (!item || !item->parent()) return QString();
  return item->data(0, Qt::UserRole).toString();
}

void VariantGroupingDialog::onLaunchClicked() {
  const QString path = currentVariantPath();
  if (path.isEmpty() || !m_onLaunch) return;
  m_onLaunch(path);
}

void VariantGroupingDialog::onSelectClicked() {
  const QString path = currentVariantPath();
  if (path.isEmpty() || !m_onSelect) return;
  m_onSelect(path);
}
