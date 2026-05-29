#ifndef ARTWORKTABPANEL_H
#define ARTWORKTABPANEL_H

#include "collectiontypes.h"
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
  Q_DISABLE_COPY_MOVE(ArtworkTabPanel)
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
  void onBrowsePlaceholderArtwork();
  void onExportPlaceholderPngs();

private:
  QStringList parseCustomArtworkTypes() const;

  Ui::ArtworkTabPanel *ui;
  SettingsModel *m_model = nullptr;
  /// Programmatically appended below the form rows so we don't have to
  /// touch artworktabpanel.ui — keeps the Designer surface stable.
  class QPushButton *m_exportPlaceholdersButton = nullptr;
};

#endif // ARTWORKTABPANEL_H
