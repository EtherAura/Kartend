#ifndef KARTEND_UTILS_DB_BULKEDIT_H
#define KARTEND_UTILS_DB_BULKEDIT_H

#include <QString>
#include <QStringList>

#include "itemmetadata.h"

// Tag-or-flag bulk operations on a list of (collection_uuid, path) pairs.
// Pure logic: takes the existing per-item metadata, mutates one field,
// returns the modified rows. The caller is responsible for persisting via
// DatabaseManager::saveItemMetadata so this stays unit-testable without
// touching SQLite.

namespace BulkEdit {

enum class Action {
  AddTag,              // tag name in `parameter` — added if absent
  RemoveTag,           // tag name in `parameter` — removed if present
  MarkPinned,          // sets isPinned = true
  UnmarkPinned,        // sets isPinned = false
  MarkHidden,          // sets isHidden = true
  UnmarkHidden,        // sets isHidden = false
  MarkContinueLater,   // sets continueLater = true
  UnmarkContinueLater, // sets continueLater = false
  ClearRating,         // sets rating = -1 (unset)
};

/// One row of the in-memory bulk operation. The caller supplies the
/// current metadata (typically from `DatabaseManager::loadItemMetadataBatch`)
/// so the helper doesn't have to know about cache layers.
struct PendingChange {
  ItemMetadataStore::ItemMetadata metadata; // mutated in place
  bool changed = false;
};

/// Applies `action` to each entry in `items`. `parameter` is the tag
/// name when the action is AddTag / RemoveTag; ignored for the flag /
/// rating actions. Returns the list of (possibly changed) rows in the
/// same order as `items`; rows the action did nothing to (e.g. AddTag
/// for a tag already present) have `changed = false` so the caller can
/// skip the database write for them.
[[nodiscard]] QList<PendingChange> applyAction(Action action, const QString &parameter,
                                               const QList<ItemMetadataStore::ItemMetadata> &items);

/// Human-readable label for an action, e.g. "Add tag", "Mark hidden".
/// Used by the dialog's combobox + the confirmation message.
[[nodiscard]] QString actionLabel(Action action);

/// True iff `action` requires a non-empty parameter (i.e. AddTag /
/// RemoveTag). Drives the dialog's enable-disable of the parameter
/// line edit.
[[nodiscard]] bool actionRequiresParameter(Action action);

} // namespace BulkEdit

#endif // KARTEND_UTILS_DB_BULKEDIT_H
