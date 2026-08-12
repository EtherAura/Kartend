// Multi-step collection-removal pipeline lifted out of
// settingsdialogremove.cpp. Operates on a SettingsModel (data) plus a
// CollectionRemoverHost (tree state + UI helpers) — no friend access into
// SettingsDialog.
#include "collectionremover.h"

#include "collection/collectionconfig.h"
#include "collection/hierarchyhelpers.h"
#include "collection/validationhelpers.h"
#include "launcherimportservice.h"
#include "settingsmodel.h"

#include <QCheckBox>
#include <QMessageBox>
#include <QSet>

CollectionRemover::CollectionRemover(SettingsModel *model, CollectionRemoverHost *host,
                                     QObject *parent)
    : QObject(parent), m_model(model), m_host(host) {}

void CollectionRemover::restoreExpandedStates(const QList<int> &expandedBefore,
                                              const QList<int> &oldToNew) {
  if (!m_host) {
    return;
  }
  for (int expIdx : expandedBefore) {
    const int mapped = (expIdx >= 0 && expIdx < oldToNew.size()) ? oldToNew[expIdx] : -1;
    if (mapped >= 0) {
      m_host->expandCollectionAtIndex(mapped);
    }
  }
}

void CollectionRemover::selectTargetAfter(int parentIdx) {
  if (!m_model || !m_host) {
    return;
  }
  auto &live = *m_model->collections;
  // Re-select the deleted collection's (already remapped) parent, falling
  // back to the first collection when it was a root.
  const int targetIndex = (parentIdx >= 0 && parentIdx < live.size()) ? parentIdx : 0;
  const int newCurrent = live.isEmpty() ? -1 : qBound(0, targetIndex, live.size() - 1);

  if (newCurrent >= 0) {
    m_host->selectCollection(newCurrent);
    m_host->expandPathToCollection(newCurrent);
    m_host->loadCollectionToUI(newCurrent);
    // value(): the working list is normally the same length as the live
    // list, but value() yields a default-constructed config rather than
    // dereferencing past the end should they ever diverge.
    *m_model->originalCollection = m_model->workingCollections->value(newCurrent);
  } else {
    m_host->selectCollection(-1);
    *m_model->originalCollection = CollectionConfig();
  }
}

void CollectionRemover::run() {
  if (!m_model || !m_host || !m_host->hasSelection()) {
    return;
  }

  auto &live = *m_model->collections;
  const int index = m_host->selectedCollectionIndex();
  if (!CollectionUtils::isValidIndex(index, m_model->collections)) {
    return;
  }
  const int parentIdx = live[index].parentCollectionIndex;

  // Check if this is a root collection with descendants — affects the
  // confirmation prompt copy.
  const QList<int> descendants = CollectionUtils::collectDescendantIndices(index, live);
  const bool isRootCollection = (parentIdx == -1);
  const bool hasDescendants = !descendants.isEmpty();

  QString message;
  if (isRootCollection && hasDescendants) {
    message = QString("Remove \"%1\" and all %2 nested collection(s)?\n\n"
                      "This action cannot be undone.")
                  .arg(live[index].name)
                  .arg(descendants.size());
  } else if (hasDescendants) {
    message = QString("Remove \"%1\" and all %2 nested collection(s)?")
                  .arg(live[index].name)
                  .arg(descendants.size());
  } else {
    message = QString("Remove \"%1\"?").arg(live[index].name);
  }

  // Launcher-import collections manage their own stub+artwork folders under
  // <AppData>/launcher-imports/<source>/; removing the collection alone
  // leaves them behind. Offer an OPT-IN cleanup for any import collection
  // in the removed set (Kartend-i366w). Snapshot the configs now — the
  // removal below rewrites the list.
  QList<CollectionConfig> importConfigs;
  // Named, not `QList<int>() << index << descendants` inline: operator<<
  // returns a REFERENCE to the temporary, and a range-for only extends the
  // lifetime of a temporary bound directly to its range variable — not one
  // reached through a returned reference. The temporary therefore died at the
  // end of the full expression and the loop walked freed memory (TSan
  // heap-use-after-free, confirmed on the SEGFAULT of Kartend-28m3f).
  QList<int> removedIndexes{index};
  removedIndexes.append(descendants);
  for (int i : removedIndexes) {
    if (!live[i].importSource.trimmed().isEmpty()) {
      importConfigs.append(live[i]);
    }
  }

  // Hand-built QMessageBox instead of the static question(): only the
  // instance API carries a checkbox.
  QMessageBox confirm(QMessageBox::Question, QStringLiteral("Remove Collection"), message,
                      QMessageBox::Yes | QMessageBox::No, m_host->dialogWidget());
  QCheckBox *cleanupBox = nullptr;
  if (!importConfigs.isEmpty()) {
    cleanupBox =
        new QCheckBox(tr("Also delete the imported shortcut files and copied artwork"), &confirm);
    cleanupBox->setChecked(false); // opt-in: destructive, so never the default
    confirm.setCheckBox(cleanupBox);
  }
  if (confirm.exec() != QMessageBox::Yes) {
    return;
  }
  const bool cleanupManaged = cleanupBox != nullptr && cleanupBox->isChecked();

  const QList<int> expandedBefore = m_host->expandedCollectionIndices();

  // Capture every name about to disappear so we can scrub them out of
  // other collections' additionalParentNames *before* the list shifts
  // under us. Empty newName tells propagateCollectionNameChange to delete
  // the entry.
  QStringList namesAboutToVanish;
  namesAboutToVanish.reserve(descendants.size() + 1);
  namesAboutToVanish.append(live[index].name);
  for (int descIndex : descendants) {
    namesAboutToVanish.append(live[descIndex].name);
  }

  // Removing the target plus its whole subtree shifts every later index, so
  // build a map from each original index to its post-removal index (-1 once
  // removed). applyCollectionRemoval uses it to drop the removed rows AND
  // remap every survivor's parentCollectionIndex in one pass — the remap the
  // old reverse-order removeAt loop never did, which is what stranded
  // subcollections at the root as "ghosts" and left stale indices behind.
  QSet<int> removedSet(descendants.cbegin(), descendants.cend());
  removedSet.insert(index);
  QList<int> oldToNew;
  oldToNew.reserve(live.size());
  for (int i = 0, next = 0; i < live.size(); ++i) {
    oldToNew.append(removedSet.contains(i) ? -1 : next++);
  }
  // The deleted collection's parent is an ancestor, never part of the
  // removed subtree — remap it now for the post-removal re-selection.
  const int remappedParentIdx =
      (parentIdx >= 0 && parentIdx < oldToNew.size()) ? oldToNew[parentIdx] : -1;

  *m_model->collections = CollectionUtils::applyCollectionRemoval(*m_model->collections, oldToNew);
  *m_model->workingCollections =
      CollectionUtils::applyCollectionRemoval(*m_model->workingCollections, oldToNew);

  // Scrub any link references to the removed names so the cache doesn't
  // keep silently dropping them on every rebuild.
  for (const QString &removed : namesAboutToVanish) {
    m_host->propagateCollectionNameChange(removed, QString{});
  }

  // Opt-in managed-folder cleanup, AFTER the list rewrite (order is
  // irrelevant to the deletes — the configs were snapshotted — but doing it
  // post-confirm keeps a mid-flow cancel from having touched disk). The
  // service refuses anything outside its launcher-imports root, so a
  // re-pointed artwork dir survives even with the box ticked.
  if (cleanupManaged) {
    QStringList cleanupErrors;
    for (const CollectionConfig &cfg : importConfigs) {
      const LauncherImportService::CleanupResult cleanup =
          LauncherImportService::removeManagedImportDirs(cfg);
      cleanupErrors.append(cleanup.errors);
    }
    if (!cleanupErrors.isEmpty()) {
      QMessageBox::warning(m_host->dialogWidget(), tr("Remove Collection"),
                           tr("The collection was removed, but some imported files could not "
                              "be deleted:\n%1")
                               .arg(cleanupErrors.join(QLatin1Char('\n'))));
    }
  }

  // Persist the removal immediately.
  m_host->emitCollectionSaved(live);

  m_host->updateCollectionTreeWidget();

  // If all collections were removed, prompt for a new one.
  if (live.isEmpty()) {
    m_host->clearCollectionUI();
    m_host->clearSelection();
    m_host->ensureRootCollectionExists();
    m_host->updateCollectionTreeWidget();
    if (!live.isEmpty()) {
      m_host->selectCollection(0);
      m_host->loadCollectionToUI(0);
      *m_model->originalCollection = m_model->workingCollections->value(0);
      m_host->emitCollectionSaved(live);
    }
  } else {
    restoreExpandedStates(expandedBefore, oldToNew);
    selectTargetAfter(remappedParentIdx);
  }

  *m_model->collectionSaved = true;
  m_host->updateSaveButtonStyle();
  m_host->updateDeleteButtonState();
}
