#ifndef ISETTINGSPANEL_H
#define ISETTINGSPANEL_H

/// Kartend-ny2ki: one lifecycle contract for every SettingsDialog panel.
///
/// Before this, ~half the panels exposed refresh()/writeBack() and half
/// exposed load()/save()/clear() for the *identical* "populate widgets from
/// the working model → user edits → flush back to the model" cycle. The host
/// dialog had to special-case which method to call per panel, no common base
/// was possible, and new panels copied whichever neighbour they were pasted
/// next to. Every panel now implements this interface.
///
/// Semantics:
///   load()  — populate the panel's widgets from the working model.
///   save()  — flush the panel's widgets back into the working model. Global
///             (GeneralSettings-backed) panels additionally call it live on each
///             edit (and emit changed()), while per-collection panels call it on
///             dialog Save. Non-const: global panels emit changed() from save().
///   clear() — reset the panel to its neutral / no-selection state. Global
///             panels are always backed by the single live model and have no
///             empty state, so they implement clear() as a no-op.
///
/// Panels inherit this *after* QWidget (the QObject base must come first):
///   class FooPanel : public QWidget, public ISettingsPanel { ... };
class ISettingsPanel {
public:
  virtual ~ISettingsPanel() = default;
  virtual void load() = 0;
  virtual void save() = 0;
  virtual void clear() = 0;
};

#endif // ISETTINGSPANEL_H
