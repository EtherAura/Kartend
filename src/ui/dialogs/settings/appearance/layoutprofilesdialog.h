#ifndef LAYOUTPROFILESDIALOG_H
#define LAYOUTPROFILESDIALOG_H

#include <QDialog>
#include <QList>

#include "collection/themepreset.h"

struct CollectionConfig;

QT_BEGIN_NAMESPACE
class QListWidget;
class QPushButton;
QT_END_NAMESPACE

/// Modal dialog for managing the user's layout profile registry. The
/// dialog mutates the supplied `profiles` list in-place — Save / Delete
/// add / remove entries against the running list — and the caller
/// persists the list to disk after the dialog closes. Apply invokes the
/// caller-supplied closure so the policy decision (save-then-reload,
/// confirm-on-destructive, etc.) stays out of the dialog.
class LayoutProfilesDialog : public QDialog {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(LayoutProfilesDialog)
public:
  using ApplyHandler = std::function<void(const ThemePreset &)>;

  explicit LayoutProfilesDialog(QWidget *parent = nullptr);

  /// Caller passes the live registry list (the dialog mutates it via
  /// Save / Delete), the current collection (for Save-current and Apply
  /// preview), and the apply closure invoked when the user activates
  /// Apply. Must be called before exec().
  void setRegistry(QList<ThemePreset> *profiles, const CollectionConfig *currentCollection,
                   ApplyHandler onApply);

private slots:
  void onSaveCurrent();
  void onApplySelected();
  void onDeleteSelected();
  void onSelectionChanged();

private:
  void setupUi();
  void refreshList(const QString &selectName = {});
  [[nodiscard]] int selectedRow() const;

  QListWidget *m_list = nullptr;
  QPushButton *m_saveButton = nullptr;
  QPushButton *m_applyButton = nullptr;
  QPushButton *m_deleteButton = nullptr;

  QList<ThemePreset> *m_profiles = nullptr;
  const CollectionConfig *m_currentCollection = nullptr;
  ApplyHandler m_onApply;
};

#endif // LAYOUTPROFILESDIALOG_H
