#ifndef PLAYLISTMENUCONTROLLER_H
#define PLAYLISTMENUCONTROLLER_H

#include <functional>
#include <optional>

#include <QList>
#include <QObject>
#include <QPair>
#include <QString>

#include "applicationcontext_fwd.h"
#include "dialogrunners.h"
#include "smartfilter.h"

class IPlaylistManager;
class INavigationManager;
struct CollectionConfig;

// ── Smart-playlist dialog plumbing (moved here with the playlist actions,
//    Kartend-5lmt7; interactionmanager.h re-exports by including this header
//    so DialogController / MainWindow wiring compile unchanged) ─────────────

/// Result of a successful CreateSmartPlaylistDialog run. nullopt return
/// from the runner means the user cancelled.
struct SmartPlaylistEdit {
  QString name;
  /// Kartend-8pn2w: a rule SET. Carries one rule for the playlists that had
  /// one, so nothing about the single-rule flow changes.
  SmartFilter::FilterSet filterSet;
};

/// (displayName, uuid) pairs the smart-playlist dialog uses to populate
/// the ByCollection picker. Built fresh at each invocation so it tracks
/// the live collection list.
using SmartPlaylistCollectionEntries = QList<QPair<QString, QString>>;

/// Runs the modal CreateSmartPlaylistDialog with the given pre-population
/// and returns the edited values, or nullopt on cancel. Kartend-n8kh: lets
/// the input layer pop the dialog without #including its header — the
/// UI layer supplies a closure that builds the dialog with the right
/// parent and reads back the result.
using SmartPlaylistDialogRunner = std::function<std::optional<SmartPlaylistEdit>(
    const QString &initialName, const std::optional<SmartFilter::FilterSet> &initialFilterSet,
    const SmartPlaylistCollectionEntries &collections)>;

/**
 * @brief Setup struct for PlaylistMenuController dependencies.
 */
struct PlaylistMenuControllerSetup {
  const ApplicationContext *ctx = nullptr;

  // Sibling managers come from ctx; only non-manager fields stay here.
  QList<CollectionConfig> *collections = nullptr;
  const int *currentCollectionIndex = nullptr;

  /// Owner-supplied modal smart-playlist dialog (Kartend-n8kh). Null in
  /// headless contexts; the smart-playlist actions guard before invoking.
  SmartPlaylistDialogRunner runSmartPlaylistDialog;

  /// Kartend-sqoq0: generic stock-modal runners for the name prompts,
  /// delete confirmation, import/export pickers, and result message boxes.
  /// Null runners fall back to direct Qt dialog construction parented on
  /// QApplication::activeWindow() (the pre-runner behavior).
  DialogRunners dialogs;
};

/**
 * @brief Playlist actions reachable from the right-click context menu.
 *
 * Extracted from InteractionManager (Kartend-5lmt7): the playlist
 * create/add/rename/delete handlers, the smart-playlist dialog round-trips,
 * and the import/export flows lived on the input coordinator only because
 * the context menu is where the user reaches them. They drive
 * IPlaylistManager (+ a view reload via INavigationManager where the result
 * must render immediately); nothing about them is input coordination.
 * Callers (the context-menu lambdas) reach this controller directly via
 * InteractionManager::playlistMenu() — the facade's one-line delegating
 * wrappers were deleted (Kartend-i5ai0).
 *
 * QObject solely for tr() — the confirmation/completion dialogs carry
 * user-visible strings. No signals or slots.
 */
class PlaylistMenuController : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(PlaylistMenuController)
public:
  explicit PlaylistMenuController(QObject *parent = nullptr);

  void setupReferences(const PlaylistMenuControllerSetup &setup);

  /// Prompt for a name, create the playlist, and add @p filePath (when
  /// non-empty) as its first item.
  void addItemToNewPlaylist(const QString &srcUuid, const QString &filePath);
  void addItemToPlaylist(const QString &playlistId, const QString &srcUuid,
                         const QString &filePath);
  /// Smart-playlist CRUD round-trips through the owner-supplied dialog.
  void createSmartPlaylistDialog();
  void editSmartPlaylistDialog(const QString &playlistId, const QString &currentName);
  void renamePlaylistDialog(const QString &playlistId, const QString &currentName);
  void deletePlaylistConfirm(const QString &playlistId, const QString &currentName);
  /// Export/import. JSON is the lossless Kartend format; M3U is interop.
  void exportPlaylistToFile(const QString &playlistId, const QString &currentName, bool asJson);
  void importPlaylistFromFile();

private:
  // ctx is the single source of truth for sibling managers (same rule as
  // InteractionManager: never cache sibling-manager pointers as fields).
  const ApplicationContext *m_ctx = nullptr;
  [[nodiscard]] IPlaylistManager *playlistMgr() const;
  [[nodiscard]] INavigationManager *navMgr() const;

  /// Snapshot of (displayName, uuid) per real (non-playlist) collection for
  /// the smart-playlist dialog's ByCollection picker.
  [[nodiscard]] SmartPlaylistCollectionEntries collectSmartPlaylistCollectionEntries() const;

  QList<CollectionConfig> *m_collections = nullptr;
  const int *m_currentCollectionIndex = nullptr;
  SmartPlaylistDialogRunner m_runSmartPlaylistDialog;
  DialogRunners m_dialogs;

  // Kartend-sqoq0: runner-or-stock-fallback helpers shared by every action
  // below. Each prefers the owner-supplied closure and falls back to the
  // original direct Qt dialog (parented on QApplication::activeWindow()).
  void warnDialog(const QString &title, const QString &text) const;
  void infoDialog(const QString &title, const QString &text) const;
  [[nodiscard]] bool confirmDialog(const QString &title, const QString &text) const;
  [[nodiscard]] std::optional<QString> getTextDialog(const QString &title, const QString &label,
                                                     const QString &initialText) const;
  [[nodiscard]] QString getOpenFileNameDialog(const QString &caption, const QString &startPath,
                                              const QString &filter) const;
  [[nodiscard]] QString getSaveFileNameDialog(const QString &caption, const QString &startPath,
                                              const QString &filter) const;
};

#endif // PLAYLISTMENUCONTROLLER_H
