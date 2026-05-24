#ifndef COMMANDPALETTEDIALOG_H
#define COMMANDPALETTEDIALOG_H

#include <functional>
#include <QDialog>
#include <QList>
#include <QString>

QT_BEGIN_NAMESPACE
class QLineEdit;
class QListWidget;
class QKeyEvent;
QT_END_NAMESPACE

/// Keyboard-first command palette. The caller builds the command list
/// once (typically from the live menu actions + collection list) and
/// passes it in; the dialog handles fuzzy filtering, keyboard
/// navigation, and invoking the matching callback on Enter.
///
/// `category` is a short prefix shown alongside the label so commands
/// from different surfaces (Collection / View / Settings) stay
/// distinguishable when ranked together — purely cosmetic; the matcher
/// also considers it.
class CommandPaletteDialog : public QDialog {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(CommandPaletteDialog)
public:
  struct Command {
    QString category;
    QString label;
    std::function<void()> run;
  };

  explicit CommandPaletteDialog(QWidget *parent = nullptr);

  /// Replaces the command list. Idempotent.
  void setCommands(QList<Command> commands);

protected:
  /// Intercepts Down/Up to move the list selection from inside the
  /// search field so the user never has to leave the keyboard.
  bool eventFilter(QObject *obj, QEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;

private slots:
  void onQueryChanged(const QString &text);
  void onAccepted();

private:
  void setupUi();
  void refreshList();

  QLineEdit *m_searchEdit = nullptr;
  QListWidget *m_list = nullptr;

  QList<Command> m_commands;
};

#endif // COMMANDPALETTEDIALOG_H
