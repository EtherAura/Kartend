// Sibling translation unit for InteractionManager.
// Right-click context menu implementation (Kartend-g6f).
// These remain InteractionManager members; this is a translation-unit split.
#include "interactionmanager.h"

#include <QApplication>
#include <QFileDialog>
#include <QMenu>

#include "collectionutils.h"
#include "customfieldsdialog.h"
#include "databasemanager.h"
#include "itemmetadata.h"
#include "itemwidget.h"
#include "launchmanager.h"
#include "metadatasidebar.h"
#include "navigationmanager.h"
#include "pathutils.h"
#include "scrollmanager.h"
#include "selectionmanager.h"
#include "sidebarmanager.h"

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcInteractionManager)

void InteractionManager::showContextMenu(ItemWidget *widget, int visualIndex,
                                         const QPoint &globalPos) {
  if (!widget || visualIndex < 0) {
    return;
  }

  // Select the right-clicked item so the user sees which item is targeted
  selectItemByIndex(visualIndex, true);

  const bool isSubcollection = widget->isSubcollection();
  const bool isVirtualFolder = widget->isVirtualFolder();
  const bool isMediaItem = !isSubcollection && !isVirtualFolder;
  const QString filePath = widget->getFilePath();

  QMenu menu;

  // --- Launch action (only for media items with a file path) ---
  if (isMediaItem && !filePath.isEmpty()) {
    QAction *launchAction = menu.addAction(tr("Launch"));
    QObject::connect(launchAction, &QAction::triggered, this, [this, filePath]() {
      if (!m_databaseManager || !m_currentCollectionIndex) {
        return;
      }
      const int cIdx = m_databaseManager->getCollectionIndexForFile(filePath);
      const int ownerIdx = (cIdx >= 0 ? cIdx : *m_currentCollectionIndex);
      launchItemWithCollection(filePath, ownerIdx);
    });
  }

  // --- Open (enter) action for subcollections and virtual folders ---
  if (isSubcollection || isVirtualFolder) {
    QAction *openAction = menu.addAction(tr("Open"));
    QObject::connect(openAction, &QAction::triggered, this, [this]() {
      if (m_scrollManager) {
        const int totalItems = m_scrollManager->getTotalItems();
        processEnterOrReturnKey(totalItems);
      }
    });
  }

  menu.addSeparator();

  // --- Toggle sidebar (properties) action ---
  QAction *propertiesAction = menu.addAction(tr("Properties"));
  QObject::connect(propertiesAction, &QAction::triggered, this, [this]() {
    if (m_sidebarManager) {
      m_sidebarManager->toggleSidebar();
    }
  });

  // --- Refresh action (soft reload of current collection) ---
  QAction *refreshAction = menu.addAction(tr("Refresh"));
  QObject::connect(refreshAction, &QAction::triggered, this, [this]() {
    if (m_navigationManager && m_currentCollectionIndex && *m_currentCollectionIndex >= 0) {
      m_navigationManager->safeReloadCollection(*m_currentCollectionIndex);
    }
  });

  // --- Edit custom fields (Kartend-hpln, media items only) ---
  if (isMediaItem && !filePath.isEmpty() && m_databaseManager && m_collections &&
      m_currentCollectionIndex) {
    menu.addSeparator();
    QAction *customFieldsAction = menu.addAction(tr("Edit custom fields..."));
    const QString itemName = widget->getItemName();
    QObject::connect(customFieldsAction, &QAction::triggered, this,
                     [this, filePath, itemName]() { editCustomFields(filePath, itemName); });

    // --- Set / clear per-item manual override (Kartend-9jdv) ---
    // Show "Set manual file..." always (lets the user point at any file).
    // Show "Clear manual override" only when an override is currently set,
    // mirroring how custom fields silently no-op when none exist.
    QAction *setManualAction = menu.addAction(tr("Set manual file..."));
    QObject::connect(setManualAction, &QAction::triggered, this, [this, filePath]() {
      const QString picked = QFileDialog::getOpenFileName(
          QApplication::activeWindow(), tr("Select Manual File"), QString(),
          tr("Manual Files (*.pdf *.epub *.cbr *.cbz *.djvu *.txt *.md *.html *.htm "
             "*.rtf *.doc *.docx *.odt *.png *.jpg *.jpeg);;All Files (*)"));
      if (picked.isEmpty()) {
        return;
      }
      setItemManualPath(filePath, picked);
    });

    // Only offer "Clear" when a manual_path override actually exists for the
    // selected item; querying the DB here keeps the menu honest about what
    // it can do (vs. always offering an action that may no-op).
    int owningIndex = m_databaseManager->getCollectionIndexForFile(filePath);
    if (owningIndex < 0) {
      owningIndex = *m_currentCollectionIndex;
    }
    if (CollectionUtils::isValidIndex(owningIndex, m_collections)) {
      const CollectionConfig &owning = (*m_collections)[owningIndex];
      const QString expandedMediaDir =
          PathUtils::validateAndExpandPath(owning.mediaDirectory, owning.name);
      const QString uuid = CollectionUtils::computeCollectionUuid(owning.name, expandedMediaDir);
      if (!uuid.isEmpty()) {
        const ItemMetadataStore::ItemMetadata md =
            m_databaseManager->loadItemMetadata(uuid, filePath);
        if (!md.manualPath.isEmpty()) {
          QAction *clearManualAction = menu.addAction(tr("Clear manual override"));
          QObject::connect(clearManualAction, &QAction::triggered, this,
                           [this, filePath]() { setItemManualPath(filePath, QString()); });
        }
      }
    }
  }

  menu.exec(globalPos);
}

void InteractionManager::editCustomFields(const QString &filePath, const QString &itemName) {
  if (!m_databaseManager || !m_collections || !m_currentCollectionIndex) {
    return;
  }
  // The owning collection of the file may differ from the displayed one in
  // showAllSubcollectionItems mode; mirror SidebarManager and prefer the
  // file's resolved collection so the metadata row's UUID matches across
  // navigation modes.
  int owningIndex = m_databaseManager->getCollectionIndexForFile(filePath);
  if (owningIndex < 0) {
    owningIndex = *m_currentCollectionIndex;
  }
  if (!CollectionUtils::isValidIndex(owningIndex, m_collections)) {
    return;
  }
  const CollectionConfig &owning = (*m_collections)[owningIndex];
  const QString expandedMediaDir =
      PathUtils::validateAndExpandPath(owning.mediaDirectory, owning.name);
  const QString uuid = CollectionUtils::computeCollectionUuid(owning.name, expandedMediaDir);
  if (uuid.isEmpty()) {
    return;
  }

  ItemMetadataStore::ItemMetadata metadata = m_databaseManager->loadItemMetadata(uuid, filePath);
  metadata.collectionUuid = uuid;
  metadata.path = filePath;

  CustomFieldsDialog dialog(QApplication::activeWindow());
  dialog.setItemTitle(itemName);
  dialog.setFields(ItemMetadataStore::parseCustomFields(metadata.customFields));
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  metadata.customFields = ItemMetadataStore::serializeCustomFields(dialog.fields());
  // Mark the row as user-edited so future scraper integrations can decide
  // whether to overwrite. Existing rows from a scraper keep their source
  // until the user touches them via this dialog.
  metadata.source = QStringLiteral("user");
  if (!m_databaseManager->saveItemMetadata(metadata)) {
    return;
  }

  // Refresh the sidebar so the new fields render immediately.
  if (m_sidebarManager) {
    m_sidebarManager->updateSidebarMetadata(
        m_selectionManager ? m_selectionManager->selectedWidget() : nullptr);
  }
}

void InteractionManager::setItemManualPath(const QString &filePath, const QString &manualPath) {
  if (!m_databaseManager || !m_collections || !m_currentCollectionIndex) {
    return;
  }
  // Resolve the owning collection the same way editCustomFields does so the
  // (uuid, path) key matches across showAllSubcollectionItems navigation.
  int owningIndex = m_databaseManager->getCollectionIndexForFile(filePath);
  if (owningIndex < 0) {
    owningIndex = *m_currentCollectionIndex;
  }
  if (!CollectionUtils::isValidIndex(owningIndex, m_collections)) {
    return;
  }
  const CollectionConfig &owning = (*m_collections)[owningIndex];
  const QString expandedMediaDir =
      PathUtils::validateAndExpandPath(owning.mediaDirectory, owning.name);
  const QString uuid = CollectionUtils::computeCollectionUuid(owning.name, expandedMediaDir);
  if (uuid.isEmpty()) {
    return;
  }

  ItemMetadataStore::ItemMetadata metadata = m_databaseManager->loadItemMetadata(uuid, filePath);
  metadata.collectionUuid = uuid;
  metadata.path = filePath;
  metadata.manualPath = manualPath;
  // User-driven edit: stamp the source so future scrapers know this row was
  // touched by the user (matches editCustomFields behavior).
  metadata.source = QStringLiteral("user");
  if (!m_databaseManager->saveItemMetadata(metadata)) {
    return;
  }
  if (m_sidebarManager) {
    m_sidebarManager->updateSidebarMetadata(
        m_selectionManager ? m_selectionManager->selectedWidget() : nullptr);
  }
}
