#ifndef CONFIGVALIDATION_H
#define CONFIGVALIDATION_H

#include "collection/collectionconfig.h"
#include <QString>
#include <QStringList>

/**
 * @brief Validation utilities for config file entries.
 *
 * Provides schema validation, path existence checks, and comprehensive
 * error reporting for collection configurations.
 */
namespace ConfigValidation {

// Check if a command is available in PATH (for launchers configured by name
// rather than absolute path).
[[nodiscard]] bool isCommandInPath(const QString &command);

// Validation result with multiple issues
struct ValidationResult {
  bool valid = true;
  QStringList warnings; // Non-critical issues
  QStringList errors;   // Critical issues that prevent operation
  /// The data-corruption subset of `errors`: UUID collisions (two
  /// collections sharing one DB key). Tracked apart from missing-path and
  /// other quality errors so the startup path can warn about genuine
  /// corruption risks without nagging about a common unmounted NAS (cj462).
  QStringList collisions;

  void addWarning(const QString &msg) { warnings << msg; }

  void addError(const QString &msg) {
    errors << msg;
    valid = false;
  }

  [[nodiscard]] bool hasIssues() const { return !warnings.isEmpty() || !errors.isEmpty(); }

  [[nodiscard]] QString summary() const;
};

// Validate a single collection configuration
// isContainer: true if this collection has children (shell/container
// collection)
[[nodiscard]] ValidationResult validateCollection(const CollectionConfig &config, int index,
                                                  bool isContainer = false);

// Validate all collections including cross-references
[[nodiscard]] ValidationResult validateAllCollections(const QList<CollectionConfig> &collections);

// Log validation results using Qt logging
void logValidationResult(const ValidationResult &result, const QString &context = QString());

} // namespace ConfigValidation

#endif // CONFIGVALIDATION_H
