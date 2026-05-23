#ifndef STARRATINGWIDGET_H
#define STARRATINGWIDGET_H

#include <QWidget>

class QMouseEvent;
class QPaintEvent;
class QEvent;

/// Five-star rating widget with half-star precision.
///
/// Internal rating value is 0-10 (0 = no rating, 2 = one star, 3 = one and a
/// half stars, 10 = five stars). The "unset" sentinel is -1 so callers can
/// distinguish "user explicitly rated 0 stars" from "never rated".
///
/// Interaction: click a star's left half to set a half-rating, right half to
/// set a full-rating. Right-click anywhere clears the rating back to -1.
/// Hover previews update the displayed rating without committing it; the
/// preview reverts on leave if the user doesn't click.
class StarRatingWidget : public QWidget {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(StarRatingWidget)
public:
  explicit StarRatingWidget(QWidget *parent = nullptr);

  [[nodiscard]] int rating() const noexcept { return m_rating; }
  void setRating(int rating);

  [[nodiscard]] QSize sizeHint() const override;
  [[nodiscard]] QSize minimumSizeHint() const override;

signals:
  void ratingChanged(int rating);

protected:
  void paintEvent(QPaintEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void leaveEvent(QEvent *event) override;

private:
  /// Maps a mouse x-coordinate inside the widget to a 0-10 rating. Returns
  /// -1 when the coordinate falls outside any star slot, so leaving the
  /// widget mid-drag doesn't pin a phantom rating.
  [[nodiscard]] int ratingAtPosition(int x) const noexcept;

  int m_rating = -1; // Committed rating, persisted on the model.
  int m_hover = -1;  // Transient hover preview, not committed until click.
};

#endif // STARRATINGWIDGET_H
