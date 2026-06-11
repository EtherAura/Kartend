#include "mainwindowfixture.h"

#include "mainwindow.h"
#include "settingsutils.h"

#include "settingskeys.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QPair>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace keys = kartend::settings::keys;

namespace KartendTest {

namespace {

QHash<QString, QPair<qint64, qint64>> snapshotDir(const QString &dirPath) {
  QHash<QString, QPair<qint64, qint64>> out;
  if (dirPath.isEmpty() || !QDir(dirPath).exists()) {
    return out;
  }
  QDirIterator it(dirPath, QDir::Files | QDir::NoSymLinks | QDir::Hidden,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString p = it.next();
    const QFileInfo fi(p);
    out.insert(p, qMakePair<qint64, qint64>(fi.size(), fi.lastModified().toMSecsSinceEpoch()));
  }
  return out;
}

RealUserPathSnapshot captureRealUserPaths() {
  // tests/integration/test_main.cpp enables process-wide test mode before
  // any fixture is constructed. Flip it off briefly so QStandardPaths
  // returns the developer's real locations rather than the qttest sandbox
  // — those are the only paths a sandbox-escape can write to. Restore the
  // prior flag before returning so the surrounding test code is unaffected.
  const bool wasTestMode = QStandardPaths::isTestModeEnabled();
  QStandardPaths::setTestModeEnabled(false);

  RealUserPathSnapshot snap;
  snap.configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
  snap.appConfigDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
  snap.appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);

  // Narrow the snapshot to per-app subdirs so we don't churn through every
  // other app's config files in the developer's home directory.
  const QStringList roots{
      QDir(snap.configDir).filePath(QStringLiteral("kartend")),
      QDir(snap.appConfigDir).absolutePath(),
      QDir(snap.appDataDir).absolutePath(),
  };
  for (const QString &root : roots) {
    const auto entries = snapshotDir(root);
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
      snap.entries.insert(it.key(), it.value());
    }
  }
  QStandardPaths::setTestModeEnabled(wasTestMode);
  return snap;
}

void assertNoRealUserWrites(const RealUserPathSnapshot &before) {
  // Recapture with test mode off; any new/changed file in the per-app
  // directories means a write escaped the sandbox.
  const RealUserPathSnapshot after = captureRealUserPaths();
  QStringList violations;
  for (auto it = after.entries.constBegin(); it != after.entries.constEnd(); ++it) {
    const auto before_it = before.entries.constFind(it.key());
    if (before_it == before.entries.constEnd()) {
      violations.append(QStringLiteral("new file: %1").arg(it.key()));
    } else if (before_it.value() != it.value()) {
      violations.append(QStringLiteral("changed: %1 (size %2->%3, mtime %4->%5)")
                            .arg(it.key())
                            .arg(before_it.value().first)
                            .arg(it.value().first)
                            .arg(before_it.value().second)
                            .arg(it.value().second));
    }
  }
  if (!violations.isEmpty()) {
    qFatal("MainWindowFixture sandbox violation — writes escaped QStandardPaths "
           "test mode and modified the real developer config:\n  %s",
           qUtf8Printable(violations.join(QStringLiteral("\n  "))));
  }
}

} // namespace

MainWindowFixture::MainWindowFixture() : MainWindowFixture(std::function<void()>{}) {}

MainWindowFixture::MainWindowFixture(const std::function<void()> &seedSandbox) {
  // Snapshot the developer's real per-app directories BEFORE flipping into
  // test mode. The dtor recaptures and compares so a regression that
  // bypasses QStandardPaths (e.g. an absolute INI path in a future code
  // path) aborts the run instead of silently writing to ~/.config/kartend.
  m_realPathsBefore = captureRealUserPaths();

  QStandardPaths::setTestModeEnabled(true);
  // Test-mode is meant to redirect every QStandardPaths::writableLocation
  // call into the qttest sandbox. Fail loudly if Qt didn't honour the flag
  // — without this guard the wipe + write loop below would target the
  // developer's real config.
  if (!QStandardPaths::isTestModeEnabled()) {
    qFatal("MainWindowFixture: QStandardPaths::setTestModeEnabled(true) "
           "did not stick — refusing to run against real config paths.");
  }

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
      qFatal("MainWindowFixture refusing to wipe non-test path: %s", qUtf8Printable(path));
    }
    QDir(path).removeRecursively();
  };
  wipe(QStandardPaths::ConfigLocation);
  wipe(QStandardPaths::AppConfigLocation);
  wipe(QStandardPaths::AppDataLocation);
  wipe(QStandardPaths::AppLocalDataLocation);
  wipe(QStandardPaths::CacheLocation);
  wipe(QStandardPaths::GenericCacheLocation);

  // Pre-seed firstRunComplete=true so MainWindow's setupInitialTimers
  // doesn't auto-launch the modal first-run wizard inside the test event
  // loop (it would block forever waiting for a user that never arrives).
  // Tests that specifically want to exercise the wizard can clear this.
  {
    QSettings s(SettingsUtils::getConfigPath(), SettingsUtils::getFormat());
    s.beginGroup(keys::kGroupGeneral);
    s.setValue(keys::kFirstRunComplete, true);
    s.endGroup();
    s.sync();
  }

  // Kartend-8h8e2: let the test persist sandbox state (typically a
  // collection INI via SettingsManager::saveCollections) before MainWindow
  // loads it — see the header comment on this constructor.
  if (seedSandbox) {
    seedSandbox();
  }

  m_window = std::make_unique<MainWindow>();
}

MainWindowFixture::~MainWindowFixture() {
  m_window.reset();
  QStandardPaths::setTestModeEnabled(false);
  assertNoRealUserWrites(m_realPathsBefore);
}

QString MainWindowFixture::sandboxConfigPath() {
  return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
}

} // namespace KartendTest
