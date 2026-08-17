#ifndef SIDEBARSLAYOUTPANEL_H
#define SIDEBARSLAYOUTPANEL_H

#include "isettingspanel.h"
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class SidebarsLayoutPanel;
}
QT_END_NAMESPACE

struct SettingsModel;

/// Kartend-auh7u: the per-collection "Sidebars" page — one place to set the
/// SIDE and JUSTIFICATION of both side panels:
///   • the details pane ("sidebar" in code; its own Details Pane page keeps
///     its position control — both write cfg.sidebar.sidebarPosition), and
///   • the navigation sidebar (collection tree, Kartend-ob1c9).
/// Justification chooses between the classic below-toolbar dock (toolbar
/// spans the full window width) and a full-height dock (the panel spans the
/// window height and the toolbar stops at its edge). It applies to
/// Left/Right docks; the details pane's Top/Bottom positions and Overlay
/// mode ignore it.
class SidebarsLayoutPanel : public QWidget, public ISettingsPanel {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(SidebarsLayoutPanel)
public:
  explicit SidebarsLayoutPanel(QWidget *parent = nullptr);
  ~SidebarsLayoutPanel() override;

  void setModel(SettingsModel *model);

  void load() override;
  void clear() override;
  void save() override;

signals:
  /// Emitted on any field change (including during load(); the host dialog
  /// guards with its is-loading flag, same contract as every other panel).
  void changed();

private:
  Ui::SidebarsLayoutPanel *ui;
  SettingsModel *m_model = nullptr;
  /// The pane-side value as loaded — the one field ALIASED with the Details
  /// Pane page's Position combo. save() writes it only when the user edited
  /// it HERE; otherwise an untouched stale copy would overwrite an edit made
  /// on the other page (both panels save in sequence, last writer wins).
  int m_loadedPaneSideIndex = -1;
};

#endif // SIDEBARSLAYOUTPANEL_H
