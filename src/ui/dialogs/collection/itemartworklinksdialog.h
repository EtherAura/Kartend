#ifndef ITEMARTWORKLINKSDIALOG_H
#define ITEMARTWORKLINKSDIALOG_H

#include <QDialog>
#include <QHash>
#include <QString>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QPushButton;
class QTableWidget;
QT_END_NAMESPACE

/// Per-item editor for artwork override paths. Shows one row
/// per artwork type — every standard type (box, screenshot, …) plus the
/// collection's user-defined custom types — letting the user pick a file for
/// any of them. Auto-discovery is intentionally NOT exposed here: the
/// override path either points at a real file or is cleared (which removes
/// the row from `item_artwork`). Standard types fall back to the
/// `{artworkDirectory}/{type}/{baseName}.{ext}` subdirectory layout when no
/// override is set; custom types only resolve via this dialog.
///
/// The dialog operates on an in-memory snapshot. Callers feed the existing
/// override map via `setOverrides()` and read the user's edits via
/// `overrides()` after `accept()`. Persistence (insert/update/delete in the
/// `item_artwork` table) is the caller's responsibility — typically the
/// DetailsPaneManager, which knows the owning collection's UUID.
class ItemArtworkLinksDialog : public QDialog {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ItemArtworkLinksDialog)
public:
  explicit ItemArtworkLinksDialog(QWidget *parent = nullptr);

  /// Header text shown above the table. Typically the displayed item name so
  /// the user knows which item they are editing.
  void setItemTitle(const QString &title);

  /// Provides the artwork-type rows to render. `standardTypes` carries the
  /// canonical (lowercase) ids returned by `ItemArtworkStore::standardTypes()`
  /// — each rendered with a friendly display name. `customTypes` carries the
  /// collection's user-defined ids (rendered as-is until a friendly-name
  /// registry exists).
  void setTypeRows(const QStringList &standardTypes, const QStringList &customTypes);

  /// Replaces the override path column with the supplied map (artwork type id
  /// -> absolute override path). Any types not present in the map render as
  /// blank (auto-discovery for standard types, "(none)" for custom types).
  void setOverrides(const QHash<QString, QString> &overrides);

  /// Returns the current map of artwork type id -> override path. Trimmed
  /// empty strings are dropped so the caller can compute insert/update/delete
  /// diffs against the original map. Rows still holding an auto-discovered
  /// path (untouched by the user since `setAutoResolvedPaths` was called) are
  /// also dropped — those entries should stay auto-discovered, not be frozen
  /// as overrides.
  [[nodiscard]] QHash<QString, QString> overrides() const;

  /// Visualises which type rows already resolve to a file on disk even
  /// when no DB override is set. Each entry's path is rendered in the
  /// path column in italic + muted color so the user can tell the row
  /// is "mapped (auto)" without having to remember that
  /// `{artwork}/{type}/{baseName}.ext` is the auto layout. The caller
  /// must call this AFTER `setTypeRows` + `setOverrides`; auto values
  /// only fill rows whose override column is still empty.
  void setAutoResolvedPaths(const QHash<QString, QString> &autoPaths);

  /// Optional: a starting directory for the file picker. Typically the
  /// collection's artwork directory so the user lands near their existing
  /// art tree.
  void setBrowseStartDirectory(const QString &dir) { m_browseStartDir = dir; }

private slots:
  void onBrowseClicked(int row);
  void onClearClicked(int row);

private:
  void setupUi();
  void appendRow(const QString &typeId, const QString &displayLabel, bool isStandard);
  [[nodiscard]] QString currentOverrideForRow(int row) const;
  void setOverrideTextForRow(int row, const QString &path);

  QTableWidget *m_table = nullptr;
  QString m_browseStartDir;
  /// Tracks each row's canonical type id. The label column is display-only
  /// (translatable for standard types) so we can't recover the id from it
  /// after the row is built.
  QStringList m_rowTypeIds;
  /// Whether the row is a standard type (true) or a user-defined custom type
  /// (false). Drives the placeholder text and the "auto" hint when the
  /// override is empty.
  QList<bool> m_rowIsStandard;
  /// Snapshot of the auto-discovered path for each row, captured at
  /// `setAutoResolvedPaths` time. Used in `overrides()` to skip rows
  /// the user did not touch so the auto value isn't promoted to a
  /// frozen DB override on accept.
  QHash<QString, QString> m_autoPathByType;
};

#endif // ITEMARTWORKLINKSDIALOG_H
