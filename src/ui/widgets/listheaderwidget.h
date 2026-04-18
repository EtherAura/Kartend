#ifndef LISTHEADERWIDGET_H
#define LISTHEADERWIDGET_H

#include <QPoint>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QHBoxLayout;
QT_END_NAMESPACE

/// Sort column identifier for list view
enum class ListSortColumn { Name = 0, Collection = 1, Artwork = 2 };

/// Header widget for list view mode with clickable column headers for sorting.
/// Displays column names and indicates current sort column/direction.
/// Supports drag-to-resize columns.
class ListHeaderWidget : public QWidget {
  Q_OBJECT

public:
  explicit ListHeaderWidget(QWidget *parent = nullptr);
  ~ListHeaderWidget() override = default;

  /// Set the current sort column and direction
  void setSortColumn(ListSortColumn column, bool ascending);

  /// Get/set the collection column width (resizable)
  [[nodiscard]] int collectionColumnWidth() const {
    return m_collectionColumnWidth;
  }
  void setCollectionColumnWidth(int width);

  /// Get/set the artwork column width (resizable)
  [[nodiscard]] int artworkColumnWidth() const { return m_artworkColumnWidth; }
  void setArtworkColumnWidth(int width);

signals:
  /// Emitted when a column header is clicked
  void columnClicked(ListSortColumn column);

  /// Emitted when collection column width changes via drag resize
  void columnWidthChanged(int collectionWidth);

  /// Emitted when artwork column width changes via drag resize
  void artworkColumnWidthChanged(int artworkWidth);

protected:
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  QLabel *m_artworkLabel = nullptr; // Artwork column header (non-sortable)
  QLabel *m_nameLabel = nullptr;
  QLabel *m_collectionLabel = nullptr;
  QHBoxLayout *m_layout = nullptr;

  ListSortColumn m_sortColumn = ListSortColumn::Name;
  bool m_sortAscending = true;
  int m_collectionColumnWidth =
      150;                       // Default collection column width (resizable)
  int m_artworkColumnWidth = 32; // Default artwork column width (resizable)

  // Column resize drag state
  bool m_draggingColumn = false;
  bool m_draggingArtworkColumn =
      false; // Distinguishes which column being dragged
  int m_dragStartX = 0;
  int m_dragStartWidth = 0;
  QPoint m_pressPos; // Track press position for click detection

  static constexpr int RESIZE_HANDLE_WIDTH =
      6; // Pixels on each side of separator
  static constexpr int MIN_COLLECTION_WIDTH = 80;
  static constexpr int MAX_COLLECTION_WIDTH = 400;
  static constexpr int MIN_ARTWORK_WIDTH = 24;
  static constexpr int MAX_ARTWORK_WIDTH = 128;

  [[nodiscard]] bool isOverResizeHandle(int x) const;
  [[nodiscard]] bool isOverArtworkResizeHandle(int x) const;
  void updateSortIndicators();
  void setupUI();
};

#endif // LISTHEADERWIDGET_H
