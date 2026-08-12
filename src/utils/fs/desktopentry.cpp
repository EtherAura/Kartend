#include "desktopentry.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QTextStream>

namespace DesktopEntryFile {

auto parse(const QString &filePath) -> Entry {
  Entry entry;
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return entry;
  }
  QTextStream in(&file);
  bool inDesktopEntry = false;
  while (!in.atEnd()) {
    const QString line = in.readLine().trimmed();
    if (line.isEmpty() || line.startsWith('#')) {
      continue;
    }
    if (line.startsWith('[')) {
      inDesktopEntry = (line == QLatin1String("[Desktop Entry]"));
      continue;
    }
    if (!inDesktopEntry) {
      continue;
    }
    const qsizetype eq = line.indexOf('=');
    if (eq <= 0) {
      continue;
    }
    const QString key = line.left(eq).trimmed();
    const QString value = line.mid(eq + 1).trimmed();
    if (key == QLatin1String("Name")) {
      entry.name = value;
    } else if (key == QLatin1String("Icon")) {
      entry.icon = value;
    } else if (key == QLatin1String("Exec")) {
      entry.exec = value;
    } else if (key == QLatin1String("TryExec")) {
      entry.tryExec = value;
    } else if (key == QLatin1String("Type")) {
      entry.type = value;
    } else if (key == QLatin1String("Categories")) {
      entry.categories = value.split(';', Qt::SkipEmptyParts);
    } else if (key == QLatin1String("NoDisplay")) {
      entry.noDisplay = (value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0);
    } else if (key == QLatin1String("Hidden")) {
      entry.hidden = (value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0);
    } else if (key == QLatin1String("X-Flatpak")) {
      entry.flatpakId = value;
    }
  }
  return entry;
}

auto isGame(const QStringList &categories) -> bool {
  static const QStringList kToolCategories = {
      QStringLiteral("Utility"),
      QStringLiteral("Settings"),
      QStringLiteral("System"),
      QStringLiteral("Development"),
  };
  // NOT excluded: "Game;Emulator;" (RetroArch, Dolphin, ES-DE). Emulator
  // frontends are launchable programs a user may legitimately want on the
  // grid — Kartend is a couch frontend, and launching another frontend from
  // it is a real workflow. Deliberate product decision, so a later reader of
  // the tool list does not "fix" it (Kartend-4cff2).
  if (!categories.contains(QLatin1String("Game"))) {
    return false;
  }
  return !std::ranges::any_of(kToolCategories, [&categories](const QString &toolCategory) {
    return categories.contains(toolCategory);
  });
}

auto execProgram(const QString &exec) -> QString {
  const QString trimmed = exec.trimmed();
  if (trimmed.isEmpty()) {
    return {};
  }
  QString token;
  if (trimmed.startsWith('"')) {
    // Quoted program with an embedded space — the spec's escape for a path
    // like "/opt/My Game/run". Backslash escapes inside are unescaped.
    for (qsizetype i = 1; i < trimmed.size(); ++i) {
      const QChar c = trimmed.at(i);
      if (c == QLatin1Char('\\') && i + 1 < trimmed.size()) {
        token.append(trimmed.at(++i));
        continue;
      }
      if (c == QLatin1Char('"')) {
        break;
      }
      token.append(c);
    }
  } else {
    token = trimmed.section(QLatin1Char(' '), 0, 0);
  }
  // A leading field code is not a program (`%f foo` is malformed anyway).
  if (token.startsWith('%')) {
    return {};
  }
  // Env-prefixed Exec lines (`env VAR=1 thegame`) name `env` as the program;
  // that is what the caller's ownership test wants to see, so no unwrapping.
  return token;
}

auto findIcon(const QString &iconName, const QStringList &shareRoots) -> QString {
  if (iconName.isEmpty()) {
    return {};
  }
  if (QDir::isAbsolutePath(iconName)) {
    return QFileInfo::exists(iconName) ? iconName : QString();
  }
  // Largest-first so covers get the best raster the theme carries.
  static const QStringList kSizes = {
      QStringLiteral("512x512"), QStringLiteral("256x256"), QStringLiteral("192x192"),
      QStringLiteral("128x128"), QStringLiteral("96x96"),   QStringLiteral("64x64"),
  };
  for (const QString &root : shareRoots) {
    for (const QString &size : kSizes) {
      const QString candidate = root + QStringLiteral("/icons/hicolor/") + size +
                                QStringLiteral("/apps/") + iconName + QStringLiteral(".png");
      if (QFileInfo::exists(candidate)) {
        return candidate;
      }
    }
  }
  for (const QString &root : shareRoots) {
    const QString candidate =
        root + QStringLiteral("/pixmaps/") + iconName + QStringLiteral(".png");
    if (QFileInfo::exists(candidate)) {
      return candidate;
    }
  }
  return {};
}

auto defaultShareRoots() -> QStringList {
#ifdef Q_OS_WIN
  // There is no XDG menu on Windows, and pretending otherwise is actively
  // wrong here: XDG_DATA_DIRS is COLON-separated, so the split below would
  // shred any Windows path at its drive letter ("C:/Users/..." -> "C",
  // "/Users/..."). Nothing downstream wants that, and the sources that read
  // these roots (the menu scan, the Flatpak exports scan) describe Linux
  // desktop concepts. Report no roots rather than nonsense ones.
  return {};
#else
  const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  QStringList candidates;
  const QString dataHome = env.value(QStringLiteral("XDG_DATA_HOME"));
  candidates.append(dataHome.isEmpty() ? QDir::homePath() + QStringLiteral("/.local/share")
                                       : dataHome);
  const QString dataDirs = env.value(QStringLiteral("XDG_DATA_DIRS"));
  if (dataDirs.isEmpty()) {
    candidates.append(QStringLiteral("/usr/local/share"));
    candidates.append(QStringLiteral("/usr/share"));
  } else {
    candidates.append(dataDirs.split(QLatin1Char(':'), Qt::SkipEmptyParts));
  }

  QStringList roots;
  for (const QString &raw : candidates) {
    const QString root = QDir::cleanPath(raw);
    if (root.isEmpty() || roots.contains(root) || !QDir(root).exists()) {
      continue;
    }
    roots.append(root);
  }
  return roots;
#endif
}

} // namespace DesktopEntryFile
