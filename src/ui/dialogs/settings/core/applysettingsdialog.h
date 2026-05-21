#ifndef APPLYSETTINGSDIALOG_H
#define APPLYSETTINGSDIALOG_H

#include <QDialog>
#include <QFlags>
#include <QHash>
#include <QList>

#include "collectionutils.h"

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QLabel;
QT_END_NAMESPACE

/// granular field-category selector used by the "Apply settings
/// to..." and "Copy settings from..." actions in the settings dialog.
///
/// The legacy action propagated a fixed bundle of appearance and
/// layout fields. With the advanced flow the user can opt in/out of each
/// category, so the dialog owns the mask AND (in pull mode) the source
/// collection picker. Identity, paths, scan-affecting flags, and launchers
/// stay excluded — those still require an explicit per-collection edit.
class ApplySettingsDialog : public QDialog {
  Q_OBJECT
public:
  /// Bitmask of curated field groups that callers may copy between
  /// collections. The grouping mirrors how the settings UI is organised so
  /// a user can tick "just colors" or "just list view" without dragging
  /// unrelated knobs along.
  enum FieldCategory : uint32_t {
    None = 0,
    GridLayout = 1u << 0, // gridWidth, h/v spacing, item dims, corner radius,
                          // alignment, viewType
    ItemText = 1u << 1,   // grid fontSize, customFontFamily
    Visibility = 1u << 2, // hideTitles, hideSubcollectionTitles, scrollbars,
                          // sidebarMode
    Colors = 1u << 3,     // background type/color/image, primary/tile/selection
    ListView = 1u << 4,   // listFontSize, listRowHeight, list row colors
    All = GridLayout | ItemText | Visibility | Colors | ListView,
  };
  Q_DECLARE_FLAGS(FieldCategories, FieldCategory)

  enum class Mode {
    /// Push: the active collection is the source; the caller already knows
    /// the target set (all / subs / picker). Source combo is hidden.
    Push,
    /// Pull: the active collection is the target; the user chooses a source
    /// collection from the in-dialog combo.
    Pull,
  };

  /// Build the dialog. @p collections is needed for the source combo in pull
  /// mode; in push mode it is ignored. @p currentIndex is excluded from the
  /// combo (a collection cannot copy from itself). @p initialCategories is
  /// the starting check state — defaults to FieldCategory::All so the legacy
  /// behaviour is preserved when the user just clicks OK.
  ApplySettingsDialog(Mode mode, const QList<CollectionConfig> &collections, int currentIndex,
                      FieldCategories initialCategories = FieldCategory::All,
                      QWidget *parent = nullptr);

  /// Set the prompt shown above the category list. Push callers use this to
  /// describe the scope ("Copy from \"X\" to all other collections"); pull
  /// callers describe the destination.
  void setHeaderText(const QString &text);

  /// Returns the user's category mask. Empty mask is allowed — callers
  /// typically treat that as a no-op cancel.
  [[nodiscard]] FieldCategories selectedCategories() const;

  /// Pull mode only: returns the index into the original collections list of
  /// the source collection the user picked. Returns -1 in push mode or when
  /// the combo is empty.
  [[nodiscard]] int selectedSourceIndex() const;

private slots:
  void onSelectAllClicked();
  void onSelectNoneClicked();

private:
  void setupUi();
  void populateSourceCombo();

  Mode m_mode;
  QList<CollectionConfig> m_collections;
  int m_currentIndex;
  FieldCategories m_initialCategories;

  QLabel *m_headerLabel = nullptr;
  QComboBox *m_sourceCombo = nullptr;
  QHash<FieldCategory, QCheckBox *> m_categoryBoxes;
  /// Index-into-m_collections for each entry the source combo exposes. Built
  /// alongside the combo so we don't depend on combo userData ordering.
  QList<int> m_sourceComboMapping;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(ApplySettingsDialog::FieldCategories)

#endif // APPLYSETTINGSDIALOG_H
