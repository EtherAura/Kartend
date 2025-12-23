#ifndef LISTHEADERWIDGET_H
#define LISTHEADERWIDGET_H

#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QHBoxLayout;
QT_END_NAMESPACE

/// Sort column identifier for list view
enum class ListSortColumn { Name = 0, Collection = 1 };

/// Header widget for list view mode with clickable column headers for sorting.
/// Displays column names and indicates current sort column/direction.
class ListHeaderWidget : public QWidget {
  Q_OBJECT

public:
  explicit ListHeaderWidget(QWidget *parent = nullptr);
  ~ListHeaderWidget() override = default;

  /// Set the current sort column and direction
  void setSortColumn(ListSortColumn column, bool ascending);

  /// Update column widths to match list items
  void setNameColumnWidth(int width);

signals:
  /// Emitted when a column header is clicked
  void columnClicked(ListSortColumn column);

protected:
  void paintEvent(QPaintEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  QLabel *m_nameLabel = nullptr;
  QLabel *m_collectionLabel = nullptr;
  QHBoxLayout *m_layout = nullptr;

  ListSortColumn m_sortColumn = ListSortColumn::Name;
  bool m_sortAscending = true;
  int m_nameColumnWidth = 300;  // Default width

  void updateSortIndicators();
  void setupUI();
};

#endif // LISTHEADERWIDGET_H
