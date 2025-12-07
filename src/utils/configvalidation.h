#ifndef CONFIGVALIDATION_H
#define CONFIGVALIDATION_H

#include "collectionutils.h"
#include "errorutils.h"
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>

/**
 * @brief Validation utilities for config file entries.
 * 
 * Provides schema validation, path existence checks, and comprehensive
 * error reporting for collection configurations.
 */
namespace ConfigValidation {

// Validation result with multiple issues
struct ValidationResult {
  bool valid = true;
  QStringList warnings;  // Non-critical issues
  QStringList errors;    // Critical issues that prevent operation
  
  void addWarning(const QString &msg) {
    warnings << msg;
  }
  
  void addError(const QString &msg) {
    errors << msg;
    valid = false;
  }
  
  [[nodiscard]] bool hasIssues() const {
    return !warnings.isEmpty() || !errors.isEmpty();
  }
  
  [[nodiscard]] QString summary() const {
    QStringList lines;
    if (!errors.isEmpty()) {
      lines << "Errors:";
      for (const QString &e : errors) {
        lines << "  • " + e;
      }
    }
    if (!warnings.isEmpty()) {
      lines << "Warnings:";
      for (const QString &w : warnings) {
        lines << "  • " + w;
      }
    }
    return lines.join("\n");
  }
};

// Validate a single collection configuration
[[nodiscard]] inline ValidationResult validateCollection(
    const CollectionConfig &config, int index) {
  ValidationResult result;
  QString prefix = QString("Collection '%1' (index %2): ")
                       .arg(config.name.isEmpty() ? "<unnamed>" : config.name)
                       .arg(index);

  // Required fields
  if (config.name.isEmpty()) {
    result.addError(prefix + "missing name");
  }

  // Media directory validation
  if (config.mediaDirectory.isEmpty()) {
    result.addWarning(prefix + "no media directory specified");
  } else {
    QString expandedPath = config.mediaDirectory;
    if (expandedPath.startsWith("~")) {
      expandedPath = QDir::homePath() + expandedPath.mid(1);
    }
    QFileInfo mediaInfo(expandedPath);
    if (!mediaInfo.exists()) {
      result.addError(prefix + "media directory does not exist: " +
                      config.mediaDirectory);
    } else if (!mediaInfo.isDir()) {
      result.addError(prefix + "media path is not a directory: " +
                      config.mediaDirectory);
    } else if (!mediaInfo.isReadable()) {
      result.addError(prefix + "media directory is not readable: " +
                      config.mediaDirectory);
    }
  }

  // Artwork directory validation (optional but validate if present)
  if (!config.artworkDirectory.isEmpty()) {
    QString expandedPath = config.artworkDirectory;
    if (expandedPath.startsWith("~")) {
      expandedPath = QDir::homePath() + expandedPath.mid(1);
    }
    QFileInfo artworkInfo(expandedPath);
    if (!artworkInfo.exists()) {
      result.addWarning(prefix + "artwork directory does not exist: " +
                        config.artworkDirectory);
    } else if (!artworkInfo.isDir()) {
      result.addWarning(prefix + "artwork path is not a directory: " +
                        config.artworkDirectory);
    }
  }

  // Launcher validation (optional but validate if present)
  if (!config.launcherPath.isEmpty()) {
    QString expandedPath = config.launcherPath;
    if (expandedPath.startsWith("~")) {
      expandedPath = QDir::homePath() + expandedPath.mid(1);
    }
    QFileInfo launcherInfo(expandedPath);
    if (!launcherInfo.exists()) {
      result.addWarning(prefix + "launcher does not exist: " +
                        config.launcherPath);
    } else if (!launcherInfo.isExecutable()) {
      result.addWarning(prefix + "launcher is not executable: " +
                        config.launcherPath);
    }
  }

  // Numeric range validation (additional checks beyond clampValues)
  if (config.gridWidth < 1) {
    result.addWarning(prefix + "gridWidth less than 1, will be clamped");
  }
  if (config.itemWidth < 50 || config.itemHeight < 50) {
    result.addWarning(prefix + "very small item dimensions may cause display issues");
  }

  // Parent index validation
  if (config.parentCollectionIndex < -1) {
    result.addError(prefix + "invalid parentCollectionIndex: " +
                    QString::number(config.parentCollectionIndex));
  }

  return result;
}

// Validate all collections including cross-references
[[nodiscard]] inline ValidationResult validateAllCollections(
    const QList<CollectionConfig> &collections) {
  ValidationResult result;

  if (collections.isEmpty()) {
    result.addWarning("No collections configured");
    return result;
  }

  // Validate each collection
  for (int i = 0; i < collections.size(); ++i) {
    ValidationResult collResult = validateCollection(collections[i], i);
    result.warnings << collResult.warnings;
    result.errors << collResult.errors;
    if (!collResult.valid) {
      result.valid = false;
    }
  }

  // Cross-reference validation: check parent indices
  for (int i = 0; i < collections.size(); ++i) {
    int parentIndex = collections[i].parentCollectionIndex;
    if (parentIndex >= 0 && parentIndex >= collections.size()) {
      result.addError(
          QString("Collection '%1' (index %2) has invalid parent index %3")
              .arg(collections[i].name)
              .arg(i)
              .arg(parentIndex));
    }
    // Detect circular parent references
    if (parentIndex == i) {
      result.addError(
          QString("Collection '%1' (index %2) has itself as parent")
              .arg(collections[i].name)
              .arg(i));
    }
  }

  // Check for duplicate names at same hierarchy level
  QHash<QString, QList<int>> nameToIndices;
  for (int i = 0; i < collections.size(); ++i) {
    QString key = QString("%1:%2")
                      .arg(collections[i].parentCollectionIndex)
                      .arg(collections[i].name);
    nameToIndices[key].append(i);
  }
  for (auto it = nameToIndices.begin(); it != nameToIndices.end(); ++it) {
    if (it.value().size() > 1) {
      result.addWarning(
          QString("Duplicate collection name '%1' at same hierarchy level")
              .arg(it.key().split(':').last()));
    }
  }

  return result;
}

// Log validation results using Qt logging
inline void logValidationResult(const ValidationResult &result,
                               const QString &context = QString()) {
  if (!result.hasIssues()) {
    return;
  }

  QString prefix = context.isEmpty() ? "Config validation" : context;

  for (const QString &error : result.errors) {
    qWarning() << prefix << "ERROR:" << error;
  }
  for (const QString &warning : result.warnings) {
    qWarning() << prefix << "WARNING:" << warning;
  }
}

}  // namespace ConfigValidation

#endif  // CONFIGVALIDATION_H
