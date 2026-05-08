#include "mainwindowfixture.h"

#include "mainwindow.h"

#include <QDir>
#include <QStandardPaths>
#include <QString>
#include <QtGlobal>

namespace KartendTest {

MainWindowFixture::MainWindowFixture() {
  QStandardPaths::setTestModeEnabled(true);

  const auto wipe = [](QStandardPaths::StandardLocation loc) {
    const QString path = QStandardPaths::writableLocation(loc);
    if (path.isEmpty()) {
      return;
    }
    // Hard guard: only wipe paths that Qt's test-mode rerouting has marked
    // with "qttest". If we ever land here without test-mode active (e.g. a
    // refactor moves construction outside the fixture), abort instead of
    // recursively deleting the user's real config/cache directories.
    if (!path.contains(QStringLiteral("qttest"))) {
      qFatal("MainWindowFixture refusing to wipe non-test path: %s",
             qUtf8Printable(path));
    }
    QDir(path).removeRecursively();
  };
  wipe(QStandardPaths::ConfigLocation);
  wipe(QStandardPaths::AppConfigLocation);
  wipe(QStandardPaths::AppDataLocation);
  wipe(QStandardPaths::AppLocalDataLocation);
  wipe(QStandardPaths::CacheLocation);
  wipe(QStandardPaths::GenericCacheLocation);

  m_window = std::make_unique<MainWindow>();
}

MainWindowFixture::~MainWindowFixture() {
  m_window.reset();
  QStandardPaths::setTestModeEnabled(false);
}

QString MainWindowFixture::sandboxConfigPath() {
  return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
}

} // namespace KartendTest
