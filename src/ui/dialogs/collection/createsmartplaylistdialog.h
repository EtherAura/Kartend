#ifndef CREATESMARTPLAYLISTDIALOG_H
#define CREATESMARTPLAYLISTDIALOG_H

#include <QDialog>
#include <QList>
#include <QPair>
#include <QString>

#include "smartfilter.h"

class SmartRuleEditor;

QT_BEGIN_NAMESPACE
class QComboBox;
class QLineEdit;
class QPushButton;
class QVBoxLayout;
QT_END_NAMESPACE

/// Modal dialog for creating (or editing) a smart playlist.
///
/// A name, a Match all/any selector, and a LIST of rules — each rule a
/// SmartRuleEditor carrying its own criterion and parameters. Kartend-8pn2w
/// grew this from the single-rule form it shipped as; the composing engine
/// (SmartFilter::FilterSet) had landed under Kartend-r5dbe with nothing able
/// to reach it.
///
/// Returns its result via accept()-then-getter, so the same dialog serves
/// both the create and edit flows by pre-loading through
/// setInitialName / setInitialFilterSet.
class CreateSmartPlaylistDialog : public QDialog {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(CreateSmartPlaylistDialog)
public:
  explicit CreateSmartPlaylistDialog(QWidget *parent = nullptr);

  /// Pre-populate the form with an existing playlist's name + rules so
  /// the same dialog can serve as the edit UI.
  void setInitialName(const QString &name);
  /// Loads every rule in @p set, adding rows as needed. An empty rule list
  /// is ignored rather than emptying the form: the dialog must never present
  /// zero rules, since a rule set with none is rejected on parse.
  void setInitialFilterSet(const SmartFilter::FilterSet &set);

  /// Display-name + uuid pair for each rule's ByCollection picker. Caller
  /// passes the live collection list (excluding playlists, which can't host
  /// a smart filter anchored on their own uuid). Must be called before
  /// exec(); the entries are retained and applied to rules added later.
  using CollectionEntry = QPair<QString, QString>; // (displayName, uuid)
  void setCollectionList(const QList<CollectionEntry> &collections);

  /// Trimmed name (empty when the user accepted with a blank field —
  /// caller should validate before persisting).
  [[nodiscard]] QString name() const;

  /// The rule set built from the current form state. Always carries at
  /// least one rule.
  [[nodiscard]] SmartFilter::FilterSet filterSet() const;

private:
  void buildUI();
  /// Append a rule row, seeded with @p filter when given. Returns the new
  /// editor so a caller loading a saved set can keep populating.
  SmartRuleEditor *addRuleRow(const SmartFilter::Filter *filter = nullptr);
  /// Drop @p row from the list. No-op at one rule — the last one cannot go.
  void removeRuleRow(SmartRuleEditor *row);
  /// Enable Ok only when the name is non-blank AND every rule's required
  /// parameter is filled in. Gating on the first rule alone would let a
  /// Match-all set with one empty rule through, which silently matches
  /// nothing.
  void updateOkButtonState();
  /// Remove buttons are disabled at a single rule, and the Match selector
  /// is pointless with one rule, so it is hidden until there are two.
  void updateRowChrome();

  QPushButton *m_okButton = nullptr;
  QLineEdit *m_nameEdit = nullptr;
  QComboBox *m_matchCombo = nullptr;
  QWidget *m_matchRow = nullptr;
  QVBoxLayout *m_rulesLayout = nullptr;

  /// Rule rows in display order. Each entry's container widget is the
  /// editor's parent, so removing a row deletes both.
  QList<SmartRuleEditor *> m_rules;
  /// Retained so rules added after setCollectionList() still get a
  /// populated ByCollection picker.
  QList<CollectionEntry> m_collectionEntries;
};

#endif // CREATESMARTPLAYLISTDIALOG_H
