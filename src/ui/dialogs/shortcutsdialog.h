#ifndef SHORTCUTSDIALOG_H
#define SHORTCUTSDIALOG_H

#include <QDialog>

QT_BEGIN_NAMESPACE
class QVBoxLayout;
class QHBoxLayout;
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
  Q_DISABLE_COPY_MOVE(ShortcutsDialog)
public:
  explicit ShortcutsDialog(QWidget *parent = nullptr);

protected:
  void showEvent(QShowEvent *event) override;

private:
  void setupUI();
  void populateContent();
  void clearLayout(QLayout *layout);
  QVBoxLayout *addSection(QVBoxLayout *parentLayout, const QString &title);
  void addShortcut(QVBoxLayout *layout, const QString &keys, const QString &description);

  QScrollArea *m_scrollArea = nullptr;
  QWidget *m_contentWidget = nullptr;
  QHBoxLayout *m_columnsLayout = nullptr;
  QVBoxLayout *m_leftColumnLayout = nullptr;
  QVBoxLayout *m_rightColumnLayout = nullptr;
};

#endif // SHORTCUTSDIALOG_H
