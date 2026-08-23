#ifndef SIDEBARSLAYOUTPANEL_H
#define SIDEBARSLAYOUTPANEL_H

#include "isettingspanel.h"
#include "retroarchicons.h"

#include <QList>
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
  /// Kartend-1kkk2. Fill the icon-set combo from the packs present in the
  /// local RetroArch install (plus a leading "Automatic" entry), and the
  /// system combo from whichever pack is in play. Repopulating the systems is
  /// separate because changing the subject or the pack changes which systems
  /// are available, while the pack list itself only changes when RetroArch does.
  void populateSystemIconPacks();
  void populateSystemIconSystems();
  /// The pack the current subject + override resolve to, or empty when
  /// RetroArch has nothing to offer.
  [[nodiscard]] QString resolvedSystemIconPack() const;
  /// Enable/disable the group's rows and set the status line — "RetroArch not
  /// detected", or a note when the chosen set does not cover the chosen
  /// system, which is otherwise a silently blank sidebar.
  void updateSystemIconState();

  Ui::SidebarsLayoutPanel *ui;
  SettingsModel *m_model = nullptr;
  /// RetroArch assets tree, re-resolved on each load() so installing
  /// RetroArch (or repointing the override on the Launchers page) is picked up
  /// without reopening the dialog.
  QString m_assetsDirectory;
  /// The packs in that tree, enumerated ONCE per load() alongside it. Every
  /// pack lookup would otherwise re-walk every pack directory — on a full
  /// install that is nine directories of several hundred files each, and the
  /// pack list cannot change while the dialog is open.
  QList<RetroArchIcons::Pack> m_systemIconPacks;
  /// Whether the loaded system came from DETECTION rather than a hand pick —
  /// mirrored back into the config on save so a later re-run knows whether it
  /// may revise the value. See SystemIconSettings::systemAutoDetected.
  bool m_systemIconAutoDetected = false;
  /// Guards the repopulate handlers while load() sets the widgets, so
  /// restoring a saved system does not immediately overwrite itself.
  bool m_loadingSystemIcon = false;
  /// The pane-side value as loaded — the one field ALIASED with the Details
  /// Pane page's Position combo. save() writes it only when the user edited
  /// it HERE; otherwise an untouched stale copy would overwrite an edit made
  /// on the other page (both panels save in sequence, last writer wins).
  int m_loadedPaneSideIndex = -1;
};

#endif // SIDEBARSLAYOUTPANEL_H
