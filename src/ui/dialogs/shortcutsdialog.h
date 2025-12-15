#ifndef SHORTCUTSDIALOG_H
#define SHORTCUTSDIALOG_H

#include <QDialog>

QT_BEGIN_NAMESPACE
class QVBoxLayout;
class QLabel;
class QScrollArea;
QT_END_NAMESPACE

/**
 * @brief Dialog displaying keyboard shortcuts help.
 * 
 * Shows all available keyboard shortcuts organized by category.
 * Can be opened via F1 or Help menu.
 */
class ShortcutsDialog : public QDialog {
  Q_OBJECT
public:
  explicit ShortcutsDialog(QWidget *parent = nullptr);

protected:
  void showEvent(QShowEvent *event) override;

private:
  void setupUI();
  void populateContent();
  void clearLayout(QLayout *layout);
  void addSection(QVBoxLayout *layout, const QString &title);
  void addShortcut(QVBoxLayout *layout, const QString &keys, const QString &description);

  QScrollArea *m_scrollArea = nullptr;
  QWidget *m_contentWidget = nullptr;
  QVBoxLayout *m_contentLayout = nullptr;
};

#endif // SHORTCUTSDIALOG_H
