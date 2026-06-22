#ifndef KARTEND_UI_WIDGETS_FORMBUILDERS_H
#define KARTEND_UI_WIDGETS_FORMBUILDERS_H

// Kartend-t06mx: shared widget-construction helpers for the scraper and
// data-audit dialogs. Each dialog used to reinvent the same boilerplate —
// a normalized form-label column, a label + read-only chip pairing, a
// list-with-add/remove group box, and the results-table look. Centralizing
// them here keeps a single styling source so a future spacing/alignment
// tweak lands once instead of N times, and resolves the label-column drift
// that had crept in between the credentials and settings panels.
//
// This is a ui/ widget helper (same layer as the dialogs that consume it),
// so it may freely build Qt widgets. It deliberately stays decoupled from
// any dialog-specific type: the results-table helper takes the delegate as
// an argument rather than pulling in a data-audit delegate header.

#include <QString>
#include <Qt>

class QAbstractItemDelegate;
class QFormLayout;
class QGroupBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableView;
class QWidget;

namespace FormBuilders {

/// Canonical label-column minimum width. Short labels in a QFormLayout
/// otherwise collapse to their text's sizeHint and sit left of long ones;
/// pinning every label to this width keeps the column aligned across
/// groupboxes. Matches the value the scraper settings panels converged on.
constexpr int kLabelColumnMinWidth = 200;

/// Walk every row of @p form and set a uniform minimum width on each
/// auto-created LabelRole QLabel so the label column aligns. No-op for
/// rows whose label slot is empty or not a QLabel (e.g. addRow(widget)
/// spanning rows). Pass @p minWidth to override the canonical default.
void uniformLabelColumn(QFormLayout *form, int minWidth = kLabelColumnMinWidth);

/// Build a fixed-size "chip": a right-aligned label paired with @p edit
/// (re-parented under the returned wrapper). The wrapper is sized to
/// labelWidth + spacing + valueWidth so a FlowLayout treats each chip as a
/// single, uniformly-sized item. @p edit is set to a fixed width; the
/// caller keeps ownership of the pointer it passed in (it lives under the
/// returned widget's child tree).
QWidget *makeChipPair(QWidget *parent, const QString &label, QLineEdit *edit, int labelWidth,
                      int valueWidth, int spacing = 4);

/// Build a group box holding a multi-select QListWidget over an
/// Add / Remove button row. The created list and buttons are returned via
/// the out-params so the caller can wire them up. @p addText labels the
/// add button (the remove button is always tr("Remove")).
QGroupBox *makePathListGroup(const QString &title, QListWidget *&list, QPushButton *&add,
                             QPushButton *&remove, const QString &addText);

/// Apply the shared status-results table look (selection by row, no inline
/// edit, sortable, alternating rows, hidden vertical header). @p delegate,
/// when non-null, is installed via setItemDelegate and re-parented to the
/// table. @p stretchLastSection controls the horizontal header's
/// stretch-last behavior.
void configureResultTable(QTableView *table, QAbstractItemDelegate *delegate = nullptr,
                          bool stretchLastSection = false);

} // namespace FormBuilders

#endif // KARTEND_UI_WIDGETS_FORMBUILDERS_H
