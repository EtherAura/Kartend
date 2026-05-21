#ifndef GENERALSETTINGSPANEL_H
#define GENERALSETTINGSPANEL_H

#include <QStringList>
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class GeneralSettingsPanel;
}
QT_END_NAMESPACE

struct SettingsModel;

/// Standalone panel widget for the global "General" sub-tab in SettingsDialog.
/// Owns four data groups — Startup (collection / home view / startup video),
/// Selection & Display (4 checkboxes), Input & Scroll Timing (9 spin/double
/// fields), and Performance & History (cache / runtime detection / launch
/// history / video-thumbnail timeout). Observes a GeneralSettings* installed
/// by the host dialog. Deferred-save semantics — host wires changed→
/// checkForChanges and bulk persistence + side-effect application
/// (PixmapCache, VideoThumbnailExtractor) happen on Save.
///
/// Note: startupCollection was previously live-saved; now follows the same
/// deferred-save semantics as the rest of the general fields (must click
/// Save). This unifies behavior with the rest of the general tab.
class GeneralSettingsPanel : public QWidget {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(GeneralSettingsPanel)
public:
  explicit GeneralSettingsPanel(QWidget *parent = nullptr);
  ~GeneralSettingsPanel() override;

  void setModel(SettingsModel *model);
  void refresh();

  /// Populate the Startup Collection combo with @p names and select the
  /// entry whose data matches @p currentValue. The combo always has a
  /// "(Default)" entry first.
  void setStartupCollections(const QStringList &names, const QString &currentValue);

signals:
  void changed();

private slots:
  void onBrowseHomeViewIcon();
  void onBrowseStartupVideo();
  void onBrowseRetroarchConfig();

private:
  void writeBack();
  void connectChangeSignals();

  Ui::GeneralSettingsPanel *ui;
  SettingsModel *m_model = nullptr;
};

#endif // GENERALSETTINGSPANEL_H
