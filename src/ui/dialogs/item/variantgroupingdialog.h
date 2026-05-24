#ifndef KARTEND_UI_DIALOGS_ITEM_VARIANTGROUPINGDIALOG_H
#define KARTEND_UI_DIALOGS_ITEM_VARIANTGROUPINGDIALOG_H

#include <functional>
#include <QDialog>

#include "variantgrouping.h"

QT_BEGIN_NAMESPACE
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
QT_END_NAMESPACE

/// Inspector for same-basename variants in the active collection.
///
/// MainWindow constructs the dialog with a pre-computed
/// `QList<VariantGroup>` (typically pulled from
/// `IDatabaseManager::loadAllItemPathsForCollection` and run through
/// `VariantGrouping::groupByBaseName`). Each group is rendered as a top-level
/// row with the basename and variant count; expanding it shows the absolute
/// paths in input order.
///
/// Two callbacks let the dialog drive MainWindow without holding direct
/// references:
///   • `onLaunch(absolutePath)` — fires a normal launcher invocation for the
///     row, matching the right-click "Launch" affordance elsewhere.
///   • `onSelect(absolutePath)` — navigates the main grid to the variant
///     without launching, so the user can see context.
class VariantGroupingDialog : public QDialog {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(VariantGroupingDialog)

public:
  using LaunchHandler = std::function<void(const QString &absolutePath)>;
  using SelectHandler = std::function<void(const QString &absolutePath)>;

  explicit VariantGroupingDialog(const QList<VariantGrouping::VariantGroup> &groups,
                                 QWidget *parent = nullptr);
  ~VariantGroupingDialog() override = default;

  void setLaunchHandler(LaunchHandler handler) { m_onLaunch = std::move(handler); }
  void setSelectHandler(SelectHandler handler) { m_onSelect = std::move(handler); }

private:
  void setupUi(const QList<VariantGrouping::VariantGroup> &groups);
  void onLaunchClicked();
  void onSelectClicked();
  /// Reads the currently-selected leaf-row's absolute path. Returns empty
  /// when nothing is selected or the user clicked a group header.
  [[nodiscard]] QString currentVariantPath() const;

  QLabel *m_summaryLabel = nullptr;
  QTreeWidget *m_tree = nullptr;
  LaunchHandler m_onLaunch;
  SelectHandler m_onSelect;
};

#endif // KARTEND_UI_DIALOGS_ITEM_VARIANTGROUPINGDIALOG_H
