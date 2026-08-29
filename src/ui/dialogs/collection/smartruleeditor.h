#ifndef SMARTRULEEDITOR_H
#define SMARTRULEEDITOR_H

#include <QList>
#include <QPair>
#include <QString>
#include <QWidget>

#include "smartfilter.h"

QT_BEGIN_NAMESPACE
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QStackedWidget;
QT_END_NAMESPACE

/// One rule of a smart playlist: a criterion combo plus the parameter
/// widgets that criterion needs, swapped in from a stacked panel.
///
/// Extracted from CreateSmartPlaylistDialog (Kartend-8pn2w) when that dialog
/// grew from one rule to a list of them. The extraction is what makes the
/// combo-index/stack-page coupling safe: the two orders have to agree, and
/// keeping them agreeing inside one small widget is tractable in a way that
/// spreading them across a dialog which owns N of everything is not.
///
/// Emits changed() on any edit so the owning dialog can re-validate — a rule
/// set is only as valid as its weakest rule.
class SmartRuleEditor : public QWidget {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(SmartRuleEditor)
public:
  explicit SmartRuleEditor(QWidget *parent = nullptr);

  /// Display-name + uuid pairs for the ByCollection picker. Call before
  /// setFilter() so the initial-selection lookup can find the saved uuid.
  using CollectionEntry = QPair<QString, QString>; // (displayName, uuid)
  void setCollectionList(const QList<CollectionEntry> &collections);

  /// Load an existing rule into the form.
  void setFilter(const SmartFilter::Filter &filter);

  /// Read the current form state. Defined for any criterion — fields the
  /// selected kind does not use keep their inert defaults.
  [[nodiscard]] SmartFilter::Filter filter() const;

  /// Whether the selected criterion's required parameter is filled in.
  /// False for e.g. "By extension" with an empty extension list, which
  /// would otherwise be accepted into a rule that can never match.
  /// Criteria that take no parameter are always complete.
  [[nodiscard]] bool isComplete() const;

signals:
  /// Any change to the criterion or its parameters.
  void changed();

private:
  void buildUI();

  QComboBox *m_kindCombo = nullptr;
  QStackedWidget *m_paramsStack = nullptr;

  // Per-kind param widgets (one stack page each so the row height stays
  // stable as the user toggles between criteria).
  QSpinBox *m_recentLimitSpin = nullptr;
  QSpinBox *m_topLimitSpin = nullptr;
  QSpinBox *m_neverLimitSpin = nullptr;
  QLineEdit *m_extensionsEdit = nullptr;
  QSpinBox *m_dateAddedDaysSpin = nullptr;
  QComboBox *m_collectionCombo = nullptr;
  QLineEdit *m_titleSearchEdit = nullptr;
};

#endif // SMARTRULEEDITOR_H
