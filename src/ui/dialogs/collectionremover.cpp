// Multi-step collection-removal pipeline lifted out of
// settingsdialogremove.cpp. Operates on a SettingsModel (data) plus a
// CollectionRemoverHost (tree state + UI helpers) — no friend access into
// SettingsDialog.
#include "collectionremover.h"

#include "collectionutils.h"
#include "settingsmodel.h"

#include <algorithm>
#include <functional>
#include <QMessageBox>

CollectionRemover::CollectionRemover(SettingsModel *model, CollectionRemoverHost *host,
                                     QObject *parent)
    : QObject(parent), m_model(model), m_host(host) {}

void CollectionRemover::performRemovalAt(int index) {
  if (!m_model) {
    return;
  }
  m_model->collections->removeAt(index);
  if (index >= 0 && index < m_model->workingCollections->size()) {
    m_model->workingCollections->removeAt(index);
  }
}

void CollectionRemover::rebuildParentIndices() {
  if (!m_model) {
    return;
  }
  // Parent indices should already be set correctly after removals — just
  // sync the working copy with the live collections.
  auto &live = *m_model->collections;
  auto &working = *m_model->workingCollections;
  for (int i = 0; i < live.size() && i < working.size(); ++i) {
    working[i].parentCollectionIndex = live[i].parentCollectionIndex;
    working[i].isSubcollection = live[i].isSubcollection;
  }
}

void CollectionRemover::restoreExpandedStates(const QList<int> &expandedBefore, int removedIndex) {
  if (!m_host) {
    return;
  }
  for (int expIdx : expandedBefore) {
    if (expIdx == removedIndex) {
      continue;
    }
    const int adjustedIdx = (expIdx > removedIndex) ? expIdx - 1 : expIdx;
    m_host->expandCollectionAtIndex(adjustedIdx);
  }
}

void CollectionRemover::selectTargetAfter(int parentIdx, int removedIndex) {
  if (!m_model || !m_host) {
    return;
  }
  if (parentIdx >= removedIndex) {
    parentIdx -= 1;
  }
  auto &live = *m_model->collections;
  const int targetIndex = (parentIdx >= 0 && parentIdx < live.size()) ? parentIdx : 0;
  const int newCurrent = live.isEmpty() ? -1 : qBound(0, targetIndex, live.size() - 1);

  if (newCurrent >= 0) {
    m_host->selectCollection(newCurrent);
    m_host->expandPathToCollection(newCurrent);
    m_host->loadCollectionToUI(newCurrent);
    *m_model->originalCollection = m_model->workingCollections->at(newCurrent);
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
  QList<int> descendants = CollectionUtils::collectDescendantIndices(index, live);
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

  const QMessageBox::StandardButton reply = QMessageBox::question(
      m_host->dialogWidget(), "Remove Collection", message, QMessageBox::Yes | QMessageBox::No);
  if (reply != QMessageBox::Yes) {
    return;
  }

  const QList<int> expandedBefore = m_host->expandedCollectionIndices();

  // Capture every name about to disappear so we can scrub them out of
  // other collections' additionalParentNames *before* the index list
  // shifts under us. Empty newName tells propagateCollectionNameChange to
  // delete the entry.
  QStringList namesAboutToVanish;
  namesAboutToVanish.reserve(descendants.size() + 1);
  namesAboutToVanish.append(live[index].name);
  for (int descIndex : descendants) {
    namesAboutToVanish.append(live[descIndex].name);
  }

  // Remove descendants first (in reverse order to maintain indices).
  std::sort(descendants.begin(), descendants.end(), std::greater<>());
  for (int descIndex : descendants) {
    performRemovalAt(descIndex);
  }

  // Recalculate the target's index after descendant removals.
  int adjustedIndex = index;
  for (int descIndex : descendants) {
    if (descIndex < index) {
      adjustedIndex--;
    }
  }
  performRemovalAt(adjustedIndex);

  // Update parent references — multiple removals happened, so any
  // collection whose parent was removed needs to be orphaned to root.
  auto &working = *m_model->workingCollections;
  for (int i = 0; i < live.size(); ++i) {
    const int origParent = live[i].parentCollectionIndex;
    if (origParent >= 0) {
      if (origParent == index || descendants.contains(origParent)) {
        live[i].parentCollectionIndex = -1;
        working[i].parentCollectionIndex = -1;
        live[i].isSubcollection = false;
        working[i].isSubcollection = false;
      }
    }
  }
  rebuildParentIndices();

  // Scrub any link references to the removed names so the cache doesn't
  // keep silently dropping them on every rebuild.
  for (const QString &removed : namesAboutToVanish) {
    m_host->propagateCollectionNameChange(removed, QString{});
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
      *m_model->originalCollection = m_model->workingCollections->at(0);
      m_host->emitCollectionSaved(live);
    }
  } else {
    restoreExpandedStates(expandedBefore, index);
    selectTargetAfter(parentIdx, adjustedIndex);
  }

  *m_model->collectionSaved = true;
  m_host->updateSaveButtonStyle();
  m_host->updateDeleteButtonState();
}
