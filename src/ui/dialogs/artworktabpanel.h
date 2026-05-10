#ifndef ARTWORKTABPANEL_H
#define ARTWORKTABPANEL_H

#include "collectionutils.h"
#include <QStringList>
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class ArtworkTabPanel;
}
QT_END_NAMESPACE

struct SettingsModel;

/// Standalone panel widget for the per-collection "Artwork" tab in
/// SettingsDialog. Owns the artwork / video / manual / placeholder asset
/// directories plus the custom-artwork-types comma list. Per-collection
/// load/save shape; internal browse pickers.
class ArtworkTabPanel : public QWidget {
  Q_OBJECT
public:
  explicit ArtworkTabPanel(QWidget *parent = nullptr);
  ~ArtworkTabPanel() override;

  void setModel(SettingsModel *model);
  void load();
  void clear();
  void save() const;
  [[nodiscard]] bool hasChanges() const;

signals:
  void changed();

private slots:
  void onBrowseArtworkDir();
  void onBrowseVideoDir();
  void onBrowseManualDir();
  void onBrowsePlaceholderArtwork();

private:
  QStringList parseCustomArtworkTypes() const;

  Ui::ArtworkTabPanel *ui;
  SettingsModel *m_model = nullptr;
};

#endif // ARTWORKTABPANEL_H
