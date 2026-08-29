#ifndef SEARCHPRESETSDIALOG_H
#define SEARCHPRESETSDIALOG_H

#include <functional>

#include <QDialog>
#include <QList>
#include <QString>

#include "collection/searchpreset.h"

struct ViewSettings;

QT_BEGIN_NAMESPACE
class QListWidget;
class QPushButton;
QT_END_NAMESPACE

/// Modal manager for the saved-search registry (Kartend-w4knq): the search
/// box's contents plus the filter and sort state around them, kept under a
/// name so a query worth composing twice does not have to be retyped.
///
/// Third dialog of the same family as LayoutProfilesDialog and
/// PresentationProfilesDialog, and deliberately identical in shape — a list
/// with Save-current / Apply / Delete beside it, mutating the caller's list in
/// place while the caller persists on close. It sits at the dialogs/ root
/// rather than under settings/appearance/ with its two siblings because a
/// saved search is a view state, not an appearance setting.
///
/// The one real difference from those siblings: a preset carries the search
/// box's text, which is not part of ViewSettings, so the caller supplies it
/// alongside the settings to snapshot from.
class SearchPresetsDialog : public QDialog {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(SearchPresetsDialog)
public:
  using ApplyHandler = std::function<void(const SearchPreset &)>;

  explicit SearchPresetsDialog(QWidget *parent = nullptr);

  /// @p presets is the live registry, mutated in place. @p currentView and
  /// @p currentSearchText seed Save-current; pass a null @p currentView to
  /// disable it. @p onApply writes the chosen preset onto the application's
  /// live view state.
  void setRegistry(QList<SearchPreset> *presets, const ViewSettings *currentView,
                   const QString &currentSearchText, ApplyHandler onApply);

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

  QList<SearchPreset> *m_presets = nullptr;
  const ViewSettings *m_currentView = nullptr;
  QString m_currentSearchText;
  ApplyHandler m_onApply;
};

#endif // SEARCHPRESETSDIALOG_H
