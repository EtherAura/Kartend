#ifndef SELECTIONRESTOREMANAGER_H
#define SELECTIONRESTOREMANAGER_H

#include <QObject>
#include <QPointer>
#include <functional>

#include "setuputils.h"

QT_BEGIN_NAMESPACE
class QLineEdit;
QT_END_NAMESPACE

class InteractionManager;
class InteractionStateHolder;
class ScrollManager;
class SessionManager;
class SettingsManager;
struct ApplicationContext;
struct CollectionConfig;
struct GeneralSettings;

/**
 * @brief Setup struct for SelectionRestoreManager dependencies.
 */
struct SelectionRestoreManagerSetup {
  const ApplicationContext *ctx = nullptr;

  InteractionManager *interactionManager = nullptr;
  ScrollManager *scrollManager = nullptr;
  SessionManager *sessionManager = nullptr;
  SettingsManager *settingsManager = nullptr;
  QLineEdit *searchBar = nullptr;
  int *currentCollectionIndex = nullptr;
  QList<CollectionConfig> *collections = nullptr;
  GeneralSettings *generalSettings = nullptr;
  std::function<bool()> isShuttingDown;

  SETUP_GETTER_DECL(InteractionManager *, InteractionManager)
  SETUP_GETTER_DECL(ScrollManager *, ScrollManager)
  SETUP_GETTER_DECL(SessionManager *, SessionManager)
  SETUP_GETTER_DECL(SettingsManager *, SettingsManager)
  SETUP_GETTER_DECL(QLineEdit *, SearchBar)
  SETUP_GETTER_DECL(int *, CurrentCollectionIndex)
  SETUP_GETTER_DECL(QList<CollectionConfig> *, Collections)
  SETUP_GETTER_DECL(GeneralSettings *, GeneralSettings)
  SETUP_GETTER_DECL_CTX_ONLY(InteractionStateHolder *, InteractionState)
};

/**
 * @brief Manages selection restoration logic for collection navigation.
 *
 * Handles scheduling and executing selection restore operations with
 * cancelable tokens to prevent stale restores from overriding user input.
 * Uses a token-based system to ensure only the most recent restore request
 * is honored when navigating between collections.
 */
class SelectionRestoreManager : public QObject {
  Q_OBJECT

public:
  explicit SelectionRestoreManager(QObject *parent = nullptr);
  ~SelectionRestoreManager() override;

  void setupReferences(const SelectionRestoreManagerSetup &setup);

  /**
   * @brief Schedules selection restoration with retry attempts.
   * @param desiredIndex The index to restore selection to.
   * @param maxAttempts Maximum number of retry attempts (unused, kept for API
   * compatibility).
   * @param attemptDelayMs Delay between attempts (unused, kept for API
   * compatibility).
   * @param finalEnsureDelayMs Final verification delay.
   */
  void scheduleSelectionRestore(int desiredIndex, int maxAttempts,
                                int attemptDelayMs, int finalEnsureDelayMs);

  /**
   * @brief Checks if selection should be restored based on settings and search
   * state.
   * @return True if selection restoration is appropriate.
   */
  [[nodiscard]] bool shouldRestoreSelection() const;

  /**
   * @brief Gets the selection index to restore for a collection.
   * @param collectionIndex The collection to get restore index for.
   * @return The index to restore, or -1 if none.
   */
  [[nodiscard]] int getSelectionRestoreIndex(int collectionIndex) const;

  /**
   * @brief Handles subcollection navigation with selection restore.
   * @param collectionIndex The subcollection being navigated to.
   */
  void handleSubcollectionRestore(int collectionIndex);

  /**
   * @brief Schedules verification of selection restore.
   * @param collectionIndex Collection index for validation.
   * @param selIdx Selection index to verify.
   * @param token Restore token for cancellation.
   */
  void scheduleSelectionRestoreVerification(int collectionIndex, int selIdx,
                                            int token);

  /**
   * @brief Initializes and returns the next selection restore token.
   * @return The new token value.
   */
  [[nodiscard]] int initializeSelectionRestoreToken() const;

private:
  /**
   * @brief Validates basic context for selection restore operations.
   * @return True if context is valid for restore.
   */
  [[nodiscard]] bool validateSelectionRestoreContext() const;

  /**
   * @brief Creates validation lambda for selection restore state.
   * @param scheduledCollectionIndex Collection index at schedule time.
   * @param token Restore token for validation.
   * @return Lambda that validates restore should proceed.
   */
  [[nodiscard]] std::function<bool()>
  createRestoreValidationLambda(int scheduledCollectionIndex, int token) const;

  /**
   * @brief Executes the selection restore with delayed validation.
   * @param desiredIndex Index to restore.
   * @param scheduledCollectionIndex Collection index at schedule time.
   * @param token Restore token for validation.
   */
  void executeSelectionRestore(int desiredIndex, int scheduledCollectionIndex,
                               int token) const;

  /**
   * @brief Creates a lambda for restoring selection with validation.
   * @param collectionIndex Collection index for validation.
   * @param selIdx Selection index to restore.
   * @param token Restore token for cancellation check.
   * @return Lambda that performs the restore.
   */
  [[nodiscard]] std::function<void()>
  createSelectionRestoreLambda(int collectionIndex, int selIdx, int token);

  // Manager references
  InteractionManager *m_interactionManager = nullptr;
  InteractionStateHolder *m_state = nullptr;
  ScrollManager *m_scrollManager = nullptr;
  SessionManager *m_sessionManager = nullptr;
  SettingsManager *m_settingsManager = nullptr;

  // UI references
  QLineEdit *m_searchBar = nullptr;

  // State references
  int *m_currentCollectionIndex = nullptr;
  QList<CollectionConfig> *m_collections = nullptr;
  GeneralSettings *m_generalSettings = nullptr;
  std::function<bool()> m_isShuttingDown;
};

#endif // SELECTIONRESTOREMANAGER_H
