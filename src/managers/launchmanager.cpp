// Launches media items with configured emulators, handling RetroArch cores and parameters.
#include "launchmanager.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QMessageBox>
#include <QProcess>

LaunchManager::LaunchManager(QObject *parent) : QObject(parent) {}

void LaunchManager::setupReferences(const LaunchManagerSetup &setup) {
  m_collections = setup.collections;
}

bool LaunchManager::canLaunch(const QString &filePath) const {
  if (!m_lastLaunchTimes.contains(filePath)) {
    return true;
  }
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  return (now - m_lastLaunchTimes[filePath]) >= kDoubleLaunchGuardMs;
}

void LaunchManager::recordLaunch(const QString &filePath) {
  m_lastLaunchTimes[filePath] = QDateTime::currentMSecsSinceEpoch();
}

void LaunchManager::launchItem(const QString &filePath, int collectionIndex) {
  if ((m_collections == nullptr) || collectionIndex < 0 ||
      collectionIndex >= m_collections->size()) {
    QMessageBox::warning(nullptr, "Invalid Collection",
                         "Invalid collection specified.");
    return;
  }

  const CollectionConfig &collection = (*m_collections)[collectionIndex];

  auto expandOnly = [&](const QString &text) -> QString {
    QString out = text;
    out.replace("%collection%", collection.name, Qt::CaseInsensitive);
    return out.trimmed();
  };

  QString expandedLauncherPath = expandOnly(collection.launcherPath);
  QString expandedCorePath = expandOnly(collection.corePath);

  if (expandedLauncherPath.isEmpty()) {
    QMessageBox::warning(nullptr, "No Launcher",
                         "No launcher configured for " + collection.name);
    return;
  }

  QString program;
  QStringList arguments;

  if (expandedLauncherPath.contains("retroarch", Qt::CaseInsensitive)) {
    if (expandedCorePath.isEmpty()) {
      QMessageBox::warning(nullptr, "No Core",
                           "No RetroArch core configured for " +
                               collection.name);
      return;
    }

    program = expandedLauncherPath;
    arguments << "-L" << expandedCorePath << filePath;
  } else {
    program = expandedLauncherPath;
    arguments << filePath;

    if (!expandedCorePath.isEmpty()) {
      QString params = expandedCorePath.trimmed();
      if (!params.isEmpty()) {
        arguments.removeLast();
        QStringList paramList = parseParameters(params);
        arguments.append(paramList);
        arguments << filePath;
      }
    }
  }

  bool success = QProcess::startDetached(program, arguments);

  if (!success) {
    QString errorMsg =
        QString("Failed to launch: %1\n\nCommand attempted:\n%2 %3\n\nMake "
                "sure the launcher path is correct and the file is executable.")
            .arg(expandedLauncherPath)
            .arg(program)
            .arg(arguments.join(" "));

    QMessageBox::critical(nullptr, "Launch Error", errorMsg);
  }
}

auto LaunchManager::parseParameters(const QString &paramString) -> QStringList {
  QStringList result;
  if (paramString.trimmed().isEmpty()) {
    return result;
  }

  QString params = paramString.trimmed();
  bool inQuotes = false;
  QString currentParam;
  QChar quoteChar;

  for (int i = 0; i < params.length(); ++i) {
    QChar currentChar = params[i];

    if (!inQuotes && (currentChar == '"' || currentChar == '\'')) {
      inQuotes = true;
      quoteChar = currentChar;
    } else if (inQuotes && currentChar == quoteChar) {
      inQuotes = false;
    } else if (currentChar == ' ' && !inQuotes) {
      if (!currentParam.isEmpty()) {
        result.append(currentParam);
        currentParam.clear();
      }
    } else {
      currentParam.append(currentChar);
    }
  }

  if (!currentParam.isEmpty()) {
    result.append(currentParam);
  }

  return result;
}
