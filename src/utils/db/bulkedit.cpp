#include "bulkedit.h"

#include <QCoreApplication>
#include <QSet>

namespace BulkEdit {

namespace {

/// lupdate attributes a bare Tr::tr() in a namespace to a class it cannot find a
/// Q_OBJECT on and warns; those warnings then mask a real context mismatch
/// elsewhere (Kartend-r4tno — that is how MediaFolderPage stayed latent).
/// Declaring the context on a struct keeps extraction and runtime agreeing on
/// "BulkEdit" — exactly what the hand-written wrapper did — while giving lupdate
/// something it recognises. Call sites read Tr::tr("…").
struct Tr {
  Q_DECLARE_TR_FUNCTIONS(BulkEdit)
};

/// Adds `tag` to the metadata's tag list, dedup case-insensitively.
/// Returns true when the list actually changed.
bool addTagInPlace(ItemMetadataStore::ItemMetadata &md, const QString &tag) {
  const QString trimmed = tag.trimmed();
  if (trimmed.isEmpty()) {
    return false;
  }
  QStringList current = ItemMetadataStore::parseTags(md.tags);
  // First-seen casing wins on dedupe — the editor's existing convention.
  for (const QString &existing : current) {
    if (existing.compare(trimmed, Qt::CaseInsensitive) == 0) {
      return false;
    }
  }
  current.append(trimmed);
  md.tags = ItemMetadataStore::serializeTags(current);
  return true;
}

bool removeTagInPlace(ItemMetadataStore::ItemMetadata &md, const QString &tag) {
  const QString trimmed = tag.trimmed();
  if (trimmed.isEmpty()) {
    return false;
  }
  QStringList current = ItemMetadataStore::parseTags(md.tags);
  const int sizeBefore = current.size();
  current.erase(std::remove_if(current.begin(), current.end(),
                               [&trimmed](const QString &t) {
                                 return t.compare(trimmed, Qt::CaseInsensitive) == 0;
                               }),
                current.end());
  if (current.size() == sizeBefore) {
    return false;
  }
  md.tags = ItemMetadataStore::serializeTags(current);
  return true;
}

} // namespace

QList<PendingChange> applyAction(Action action, const QString &parameter,
                                 const QList<ItemMetadataStore::ItemMetadata> &items) {
  QList<PendingChange> out;
  out.reserve(items.size());
  for (const ItemMetadataStore::ItemMetadata &md : items) {
    PendingChange change;
    change.metadata = md;
    switch (action) {
    case Action::AddTag:
      change.changed = addTagInPlace(change.metadata, parameter);
      break;
    case Action::RemoveTag:
      change.changed = removeTagInPlace(change.metadata, parameter);
      break;
    case Action::MarkPinned:
      if (!change.metadata.isPinned) {
        change.metadata.isPinned = true;
        change.changed = true;
      }
      break;
    case Action::UnmarkPinned:
      if (change.metadata.isPinned) {
        change.metadata.isPinned = false;
        change.changed = true;
      }
      break;
    case Action::MarkHidden:
      if (!change.metadata.isHidden) {
        change.metadata.isHidden = true;
        change.changed = true;
      }
      break;
    case Action::UnmarkHidden:
      if (change.metadata.isHidden) {
        change.metadata.isHidden = false;
        change.changed = true;
      }
      break;
    case Action::MarkContinueLater:
      if (!change.metadata.continueLater) {
        change.metadata.continueLater = true;
        change.changed = true;
      }
      break;
    case Action::UnmarkContinueLater:
      if (change.metadata.continueLater) {
        change.metadata.continueLater = false;
        change.changed = true;
      }
      break;
    case Action::ClearRating:
      if (change.metadata.rating >= 0) {
        change.metadata.rating = -1;
        change.changed = true;
      }
      break;
    }
    // Stamp source on actually-changed rows so future scraper integrations
    // know the user touched them — matches the per-item editor's behaviour.
    if (change.changed) {
      change.metadata.source = QStringLiteral("user");
    }
    out.append(change);
  }
  return out;
}

QString actionLabel(Action action) {
  switch (action) {
  case Action::AddTag:
    return Tr::tr("Add tag");
  case Action::RemoveTag:
    return Tr::tr("Remove tag");
  case Action::MarkPinned:
    return Tr::tr("Pin to top");
  case Action::UnmarkPinned:
    return Tr::tr("Unpin");
  case Action::MarkHidden:
    return Tr::tr("Hide");
  case Action::UnmarkHidden:
    return Tr::tr("Unhide");
  case Action::MarkContinueLater:
    return Tr::tr("Mark continue later");
  case Action::UnmarkContinueLater:
    return Tr::tr("Clear continue-later marker");
  case Action::ClearRating:
    return Tr::tr("Clear rating");
  }
  return Tr::tr("Unknown action");
}

bool actionRequiresParameter(Action action) {
  return action == Action::AddTag || action == Action::RemoveTag;
}

} // namespace BulkEdit
