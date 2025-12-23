#ifndef ITEMWIDGET_H
#define ITEMWIDGET_H

#include <QElapsedTimer>
#include <QFrame>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QRect>

#include "ui_itemwidget.h"
#include <QString>

class QLabel;
class QMouseEvent;
class QEnterEvent;
class QPaintEvent;
class QShowEvent;
class QResizeEvent;
class QTimer;

class ItemWidget : public QWidget {
  Q_OBJECT
  Q_PROPERTY(qreal pulseOpacity READ pulseOpacity WRITE setPulseOpacity)
  bool isSelectedState = false;
  QPropertyAnimation *pulseAnimation = nullptr;
  qreal m_pulseOpacity = 1.0;
  QPixmap storedPixmap;

public:
  explicit ItemWidget(QWidget *parent = nullptr);
  ~ItemWidget() override;

  void setItemName(const QString &name);
  void setFilePath(const QString &path);
  void setArtworkPixmap(const QPixmap &pixmap);
  virtual void setSelected(bool selected);
  void resetForReuse();

  [[nodiscard]] QString getItemName() const { return itemName; }
  [[nodiscard]] QString getFilePath() const { return filePath; }
  [[nodiscard]] QRect selectionBorderRectInParent() const;
  [[nodiscard]] bool isSelected() const { return isSelectedState; }

  void setPulseOpacity(qreal opacity);
  [[nodiscard]] qreal pulseOpacity() const { return m_pulseOpacity; }

  void setItemDimensions(int width, int height);
  void setFontSize(int fontSize);
  void setHideTitles(bool hide);
  void setHideSubcollectionTitles(bool hide);
  void setCornerRadius(int radius);
  void setListMode(bool listMode);
  void setRowIndex(int index) { m_rowIndex = index; }
  [[nodiscard]] bool isListMode() const { return m_isListMode; }
  int m_itemWidth;
  int m_itemHeight;
  int m_artworkSize = 0; // Computed artwork size for triangle indicator positioning
  int m_fontSize = 12; // Default font size
  int m_cornerRadius = 0; // Corner radius for artwork clipping
  bool m_hideTitles = false;
  bool m_hideSubcollectionTitles = false;
  bool m_hideSubfolderTitle = false;
  QLabel *imageLabel = nullptr;
  QLabel *nameLabel = nullptr;
  QWidget *triangleIndicator;
  void setAsSubcollection(int index, const QString &name);
  void setAsVirtualFolder(const QString &folderPath, const QString &displayName, bool hideTitle = false);
  [[nodiscard]] bool isSubcollection() const { return m_isSubcollection; }
  [[nodiscard]] bool isVirtualFolder() const { return m_isVirtualFolder; }
  [[nodiscard]] QString virtualFolderPath() const { return m_virtualFolderPath; }
  void applyTitleTint();
  static QColor titleTint();
  QColor m_titleTintColor;  // Cached tint color for custom painting

  // Static configuration for title appearance (set from GeneralSettings)
  static void setTitleTintSaturation(int saturation);
  static void setTitleTintLightness(int lightness);
  static void setTitleBaseColor(const QString &hexColor);
  static void setCustomFontFamily(const QString &fontFamily);
  static void setPrimaryColor(const QString &hexColor);
  static void setTileColor(const QString &hexColor);
  static void setSelectionColor(const QString &hexColor);
  static int s_titleTintSaturation;
  static int s_titleTintLightness;
  static QString s_titleBaseColor;
  static QString s_customFontFamily;
  static QString s_primaryColor;
  static QString s_tileColor;
  static QString s_selectionColor;

  void mousePressEvent(QMouseEvent *event) override;

  void onArtworkChanged();

signals:
  // Only subcollectionDoubleClicked is used - EventManager intercepts all other clicks
  void subcollectionDoubleClicked(int index);
  void virtualFolderDoubleClicked(const QString &folderPath);

protected:
  void paintEvent(QPaintEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  void enterEvent(QEnterEvent *event) override;
#else
  void enterEvent(QEvent *event) override;
#endif
  void leaveEvent(QEvent *event) override;
  void showEvent(QShowEvent *event) override;
  bool event(QEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private:
  QString itemName;
  QString filePath;
  bool m_isSubcollection = false;
  int m_subcollectionIndex = -1;
  bool m_isVirtualFolder = false;
  bool m_isListMode = false;  // True when displaying in list view (no artwork)
  int m_rowIndex = -1;  // Row index for alternating background colors in list mode
  QString m_virtualFolderPath;
  void updateTriangleIndicator();
  void paintTriangleIndicator();
  [[nodiscard]] QPixmap buildPlaceholderPattern(int width, int height) const;
  void setupPulseAnimation();
  void startPulseAnimation();
  QTimer *m_pulseDelayTimer;
  QElapsedTimer m_lastClickTimer;
  QPoint m_lastClickPos;
  static constexpr int DOUBLE_CLICK_TIMEOUT_MS = 300;
  static constexpr int CLICK_POSITION_TOLERANCE = 5;

  [[nodiscard]] bool isGlideActive() const;
  [[nodiscard]] QRect computeSelectionBorderRect() const;
  void applySelectedUiEffects();
  void applyDeselectedUiEffects();
  void scheduleSelectionBorderUpdate();
  void applyDimensions();
};

#endif // ITEMWIDGET_H