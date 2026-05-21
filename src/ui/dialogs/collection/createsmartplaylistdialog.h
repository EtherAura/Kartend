#ifndef CREATESMARTPLAYLISTDIALOG_H
#define CREATESMARTPLAYLISTDIALOG_H

#include <QDialog>
#include <QString>

#include "smartfilter.h"

QT_BEGIN_NAMESPACE
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QStackedWidget;
QT_END_NAMESPACE

/// Modal dialog for creating (or editing) a smart playlist.
///
/// Surfaces the five MVP filter kinds (recently launched, top played,
/// never played, by-extension, has-artwork) with per-kind parameter
/// widgets in a stacked panel that swaps when the user picks a kind. The
/// dialog returns its result via accept()-then-getter pair so the caller
/// can wire the same dialog to both "create" and "edit" flows by
/// pre-loading existing values via setInitialName / setInitialFilter.
class CreateSmartPlaylistDialog : public QDialog {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(CreateSmartPlaylistDialog)
public:
  explicit CreateSmartPlaylistDialog(QWidget *parent = nullptr);

  /// Pre-populate the form with an existing playlist's name + filter so
  /// the same dialog can serve as the edit-filter UI.
  void setInitialName(const QString &name);
  void setInitialFilter(const SmartFilter::Filter &filter);

  /// Trimmed name (empty when the user accepted with a blank field —
  /// caller should validate before persisting).
  [[nodiscard]] QString name() const;
  /// Filter built from the current form state. Defined for any combo
  /// selection — kind-irrelevant fields default to their inert values
  /// (limit=50 for non-counted kinds; extensions empty for non-extension
  /// kinds).
  [[nodiscard]] SmartFilter::Filter filter() const;

private slots:
  void onKindChanged(int index);

private:
  void buildUI();

  QLineEdit *m_nameEdit = nullptr;
  QComboBox *m_kindCombo = nullptr;
  QStackedWidget *m_paramsStack = nullptr;

  // Per-kind param widgets (one stack page each so the dialog height
  // stays stable as the user toggles between criteria).
  QSpinBox *m_recentLimitSpin = nullptr;
  QSpinBox *m_topLimitSpin = nullptr;
  QSpinBox *m_neverLimitSpin = nullptr;
  QLineEdit *m_extensionsEdit = nullptr;
  QLabel *m_hasArtworkNote = nullptr;
  QSpinBox *m_dateAddedDaysSpin = nullptr;
};

#endif // CREATESMARTPLAYLISTDIALOG_H
