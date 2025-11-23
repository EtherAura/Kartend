#include <QApplication>

#include "artworkmanager.h"
#include "cachemanager.h"
#include "mainwindow.h"

auto main(int argc, char *argv[]) -> int {
  QApplication app(argc, argv);
  QApplication::setApplicationName(APP_NAME);
  QApplication::setApplicationVersion(APP_VERSION);
  QApplication::setOrganizationName(APP_AUTHOR);
  QApplication::setWindowIcon(QIcon(":/assets/icons/tag-events.png"));

  QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
    ArtworkManager::s_shuttingDown.store(true);
    CacheManager::instance().releaseGuiResources();
    CacheManager::cleanup();
  });

  MainWindow window;
  window.show();

  int result = QApplication::exec();
  return result;
}