#ifndef SETTINGSFORMBINDING_H
#define SETTINGSFORMBINDING_H

// Two-way bindings between Qt form widgets and plain config fields. Centralizes
// the "if (widget) { block signals; set value; unblock; }" boilerplate so the
// settings dialogs read as a flat list of field bindings instead of pages of
// 4-line null-check blocks.
//
// Conventions:
//   loadInto(widget, value)    — set widget from value, with QSignalBlocker
//                                guarding signal emission. No-op when widget
//                                is null. Use during dialog open / refresh.
//   saveFrom(widget, &output)  — read widget into the output reference.
//                                No-op when widget is null. Use during apply.
//
// Each overload pair is intentionally tiny — the goal is call-site clarity,
// not abstraction. New widget types are easy to add as needed.

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QString>

namespace SettingsFormBinding {

// ── Load: write a config value into the form widget ──────────────────────────

inline void loadInto(QCheckBox *widget, bool value) {
  if (!widget) {
    return;
  }
  QSignalBlocker block(widget);
  widget->setChecked(value);
}

inline void loadInto(QSpinBox *widget, int value) {
  if (!widget) {
    return;
  }
  QSignalBlocker block(widget);
  widget->setValue(value);
}

inline void loadInto(QDoubleSpinBox *widget, double value) {
  if (!widget) {
    return;
  }
  QSignalBlocker block(widget);
  widget->setValue(value);
}

inline void loadInto(QLineEdit *widget, const QString &value) {
  if (!widget) {
    return;
  }
  QSignalBlocker block(widget);
  widget->setText(value);
}

// QComboBox carries an integer "current index" semantically distinct from
// "current text"; callers commit to one or the other.
inline void loadIntoIndex(QComboBox *widget, int index) {
  if (!widget) {
    return;
  }
  QSignalBlocker block(widget);
  widget->setCurrentIndex(index);
}

// ── Save: read the form widget into a config field reference ─────────────────

inline void saveFrom(const QCheckBox *widget, bool &out) {
  if (!widget) {
    return;
  }
  out = widget->isChecked();
}

inline void saveFrom(const QSpinBox *widget, int &out) {
  if (!widget) {
    return;
  }
  out = widget->value();
}

inline void saveFrom(const QDoubleSpinBox *widget, double &out) {
  if (!widget) {
    return;
  }
  out = widget->value();
}

// QLineEdit's text is trimmed by default — paths and names with surrounding
// whitespace are almost never intentional. Pass trim=false for the rare case
// where leading/trailing spaces matter.
inline void saveFrom(const QLineEdit *widget, QString &out, bool trim = true) {
  if (!widget) {
    return;
  }
  out = trim ? widget->text().trimmed() : widget->text();
}

inline void saveFromIndex(const QComboBox *widget, int &out) {
  if (!widget) {
    return;
  }
  out = widget->currentIndex();
}

} // namespace SettingsFormBinding

#endif // SETTINGSFORMBINDING_H
