#ifndef KARTEND_FLOWLAYOUT_H
#define KARTEND_FLOWLAYOUT_H

#include <QLayout>
#include <QList>
#include <QRect>
#include <QSize>

/// Reflowing wrap layout for the metadata chip widgets. Items lay out
/// left-to-right until the right edge of the container is reached, then
/// wrap to the next line. Width changes (user resize) re-flow the items
/// so ultrawide windows pack more chips per row and narrow windows
/// stack them. Adapted from the canonical Qt FlowLayout example. Lives
/// alongside ScrapeResultDialog because that is the only consumer
/// today; if a second caller arrives we can lift it to a more general
/// location.
class FlowLayout : public QLayout {
public:
  explicit FlowLayout(QWidget *parent, int margin = 0, int hSpacing = 8, int vSpacing = 6);
  ~FlowLayout() override;

  void addItem(QLayoutItem *item) override;
  [[nodiscard]] int horizontalSpacing() const { return m_hSpace; }
  [[nodiscard]] int verticalSpacing() const { return m_vSpace; }
  [[nodiscard]] Qt::Orientations expandingDirections() const override { return {}; }
  [[nodiscard]] bool hasHeightForWidth() const override { return true; }
  [[nodiscard]] int heightForWidth(int width) const override;
  [[nodiscard]] int count() const override { return m_items.size(); }
  [[nodiscard]] QLayoutItem *itemAt(int idx) const override { return m_items.value(idx); }
  QLayoutItem *takeAt(int idx) override;
  [[nodiscard]] QSize minimumSize() const override;
  void setGeometry(const QRect &r) override;
  [[nodiscard]] QSize sizeHint() const override { return minimumSize(); }

private:
  int doLayout(const QRect &rect, bool testOnly) const;

  QList<QLayoutItem *> m_items;
  int m_hSpace;
  int m_vSpace;
};

#endif // KARTEND_FLOWLAYOUT_H
