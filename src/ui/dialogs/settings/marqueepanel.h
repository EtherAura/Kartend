#ifndef MARQUEEPANEL_H
#define MARQUEEPANEL_H

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class MarqueePanel;
}
QT_END_NAMESPACE

struct SettingsModel;

/// Standalone panel widget for the global "Secondary Monitor" sub-tab in
/// SettingsDialog. Owns the three marquee fields (enabled / screen name /
/// display mode). Same deferred-save shape as AttractPanel — host wires
/// `changed()` to checkForChanges so the Save button reflects unsaved
/// edits; persistence happens on Save like the rest of the per-collection
/// deferred fields. The screen list is populated from
/// QGuiApplication::screens() at panel-construction time.
class MarqueePanel : public QWidget {
  Q_OBJECT
public:
  explicit MarqueePanel(QWidget *parent = nullptr);
  ~MarqueePanel() override;

  void setModel(SettingsModel *model);
  void refresh();

signals:
  void changed();

private:
  void writeBack();
  /// Rebuild the screen combo from QGuiApplication::screens(). Called
  /// at construction and at refresh() time so unplugged screens
  /// disappear from the list and newly-plugged ones appear.
  void populateScreens();

  Ui::MarqueePanel *ui;
  SettingsModel *m_model = nullptr;
};

#endif // MARQUEEPANEL_H
