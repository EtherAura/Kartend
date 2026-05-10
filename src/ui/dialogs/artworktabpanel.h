#ifndef ARTWORKTABPANEL_H
#define ARTWORKTABPANEL_H

#include "collectionutils.h"
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class ArtworkTabPanel;
}
QT_END_NAMESPACE

/// Standalone panel widget for the per-collection "Artwork" tab in
/// SettingsDialog. Owns the artwork / video / manual / placeholder asset
/// directories plus the custom-artwork-types comma list. Per-collection
/// load/save shape; internal browse pickers.
class ArtworkTabPanel : public QWidget {
  Q_OBJECT
public:
  explicit ArtworkTabPanel(QWidget *parent = nullptr);
  ~ArtworkTabPanel() override;

  void load(const CollectionConfig &config);
  void clear();
  void save(CollectionConfig &config) const;
  [[nodiscard]] bool hasChanges(const CollectionConfig &original) const;

signals:
  void changed();

private slots:
  void onBrowseArtworkDir();
  void onBrowseVideoDir();
  void onBrowseManualDir();
  void onBrowsePlaceholderArtwork();

private:
  Ui::ArtworkTabPanel *ui;
};

#endif // ARTWORKTABPANEL_H
