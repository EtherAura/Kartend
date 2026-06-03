#ifndef NAVIGATIONSTACKMANAGER_H
#define NAVIGATIONSTACKMANAGER_H

#include <QList>
#include <QObject>

/**
 * @brief Manages the navigation stack for collection hierarchy traversal.
 *
 * Encapsulates the navigation stack that tracks the path through collection
 * hierarchy (parent → child navigation). Provides a clean API for:
 * - Pushing/popping collection indices
 * - Tracking navigation depth
 * - Navigation state management
 *
 * Thread-safety: Not thread-safe. All access must be from main thread.
 */
class NavigationStackManager : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(NavigationStackManager)

public:
  explicit NavigationStackManager(QObject *parent = nullptr) : QObject(parent) {}

  // --- Stack Operations ---

  /// Push a collection index onto the navigation stack
  void push(int collectionIndex) { m_stack.append(collectionIndex); }

  /// Pop and return the top collection index, or -1 if empty
  [[nodiscard]] int pop() {
    if (m_stack.isEmpty()) {
      return -1;
    }
    int index = m_stack.takeLast();
    return index;
  }

  /// Peek at the top collection index without removing, or -1 if empty
  [[nodiscard]] int top() const { return m_stack.isEmpty() ? -1 : m_stack.last(); }

  /// Clear the entire navigation stack
  void clear() { m_stack.clear(); }

  // --- State Queries ---

  /// Returns true if the navigation stack is empty
  [[nodiscard]] bool isEmpty() const { return m_stack.isEmpty(); }

  /// Returns the current navigation depth (== stack size; kept for callers).
  [[nodiscard]] int depth() const { return m_stack.size(); }

  /// Returns the number of items on the stack
  [[nodiscard]] int size() const { return m_stack.size(); }

  // --- Navigation In Progress ---

  /// Returns true if a navigation operation is currently in progress
  [[nodiscard]] bool isInProgress() const { return m_inProgress; }

  /// Set navigation in progress state
  void setInProgress(bool inProgress) { m_inProgress = inProgress; }

  // --- Direct Stack Access (for compatibility) ---

  /// Returns const reference to the underlying stack
  [[nodiscard]] const QList<int> &stack() const { return m_stack; }

  /// Returns mutable reference to the underlying stack (use sparingly)
  [[nodiscard]] QList<int> &stackRef() { return m_stack; }

private:
  QList<int> m_stack;
  bool m_inProgress = false;
};

#endif // NAVIGATIONSTACKMANAGER_H
