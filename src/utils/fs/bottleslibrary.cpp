#include "bottleslibrary.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QTextStream>

namespace BottlesLibrary {

namespace {

/// Leading-space count. PyYAML never emits tabs, so a tab means this is not a
/// file we understand and the caller bails out.
int indentOf(const QString &line, bool &sawTab) {
  int indent = 0;
  for (const QChar c : line) {
    if (c == QLatin1Char('\t')) {
      sawTab = true;
      return indent;
    }
    if (c != QLatin1Char(' ')) {
      break;
    }
    ++indent;
  }
  return indent;
}

/// Scalar value as written by PyYAML: plain, single-quoted ('' escapes a
/// quote) or double-quoted (backslash escapes). Block scalars (`|`, `>`) and
/// flow collections are reported as unsupported so the caller can skip the
/// entry instead of storing half a value.
bool scalarValue(const QString &raw, QString &out) {
  const QString value = raw.trimmed();
  if (value.isEmpty()) {
    out.clear();
    return true;
  }
  const QChar first = value.at(0);
  if (first == QLatin1Char('|') || first == QLatin1Char('>') || first == QLatin1Char('&') ||
      first == QLatin1Char('*') || first == QLatin1Char('[')) {
    return false;
  }
  if (first == QLatin1Char('{')) {
    // `{}` is how the dumper writes an empty mapping; a populated flow
    // mapping is not a shape this reader claims to handle.
    out.clear();
    return value == QLatin1String("{}");
  }
  if (first == QLatin1Char('\'')) {
    QString unquoted;
    for (qsizetype i = 1; i < value.size(); ++i) {
      if (value.at(i) == QLatin1Char('\'')) {
        if (i + 1 < value.size() && value.at(i + 1) == QLatin1Char('\'')) {
          unquoted.append(QLatin1Char('\''));
          ++i;
          continue;
        }
        out = unquoted;
        return true;
      }
      unquoted.append(value.at(i));
    }
    return false; // unterminated
  }
  if (first == QLatin1Char('"')) {
    QString unquoted;
    for (qsizetype i = 1; i < value.size(); ++i) {
      const QChar c = value.at(i);
      if (c == QLatin1Char('\\') && i + 1 < value.size()) {
        const QChar escaped = value.at(++i);
        unquoted.append(escaped == QLatin1Char('n') ? QLatin1Char('\n') : escaped);
        continue;
      }
      if (c == QLatin1Char('"')) {
        out = unquoted;
        return true;
      }
      unquoted.append(c);
    }
    return false; // unterminated
  }
  out = value;
  return true;
}

/// Splits `key: value` at the first `: ` (or a trailing bare `:`), which is
/// the only place PyYAML puts an unquoted key/value separator.
bool splitKeyValue(const QString &line, QString &key, QString &value, bool &opensBlock) {
  const QString trimmed = line.trimmed();
  if (trimmed.startsWith(QLatin1Char('-')) || trimmed.startsWith(QLatin1Char('#'))) {
    return false; // list items and comments are not shapes this reader needs
  }
  if (trimmed.endsWith(QLatin1Char(':'))) {
    key = trimmed.left(trimmed.size() - 1).trimmed();
    value.clear();
    opensBlock = true;
    return !key.isEmpty();
  }
  const qsizetype sep = trimmed.indexOf(QLatin1String(": "));
  if (sep <= 0) {
    return false;
  }
  key = trimmed.left(sep).trimmed();
  value = trimmed.mid(sep + 2);
  opensBlock = false;
  return !key.isEmpty();
}

QString unquotedKey(const QString &key) {
  QString out;
  if (scalarValue(key, out)) {
    return out;
  }
  return key;
}

} // namespace

auto parseConfig(const QString &configPath) -> Bottle {
  Bottle bottle;
  QFile file(configPath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return bottle;
  }

  // Three nesting levels are enough: the document, the External_Programs
  // mapping, and one program's fields.
  constexpr int kNoIndent = -1;
  int programsIndent = kNoIndent; // indent of the External_Programs key
  int entryIndent = kNoIndent;    // indent of each program's own key
  int fieldIndent = kNoIndent;    // indent of a program's own fields
  Program current;
  bool currentValid = false;
  const auto flush = [&bottle, &current, &currentValid]() {
    if (currentValid && !current.name.isEmpty()) {
      bottle.programs.append(current);
    }
    current = Program();
    currentValid = false;
  };

  QTextStream in(&file);
  while (!in.atEnd()) {
    const QString line = in.readLine();
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')) ||
        trimmed.startsWith(QLatin1String("---")) || trimmed.startsWith(QLatin1String("..."))) {
      continue;
    }
    bool sawTab = false;
    const int indent = indentOf(line, sawTab);
    if (sawTab) {
      return {}; // not the dumper's output — refuse the whole file
    }

    QString key;
    QString value;
    bool opensBlock = false;
    const bool isMapping = splitKeyValue(line, key, value, opensBlock);

    if (indent == 0) {
      flush();
      programsIndent = kNoIndent;
      entryIndent = kNoIndent;
      fieldIndent = kNoIndent;
      if (!isMapping) {
        continue;
      }
      if (key == QLatin1String("Name")) {
        QString parsed;
        if (scalarValue(value, parsed)) {
          bottle.name = parsed;
        }
      } else if (key == QLatin1String("External_Programs") && opensBlock) {
        programsIndent = indent;
      }
      continue;
    }

    if (programsIndent == kNoIndent) {
      continue; // inside some other block we do not read
    }
    if (!isMapping) {
      continue;
    }

    if (entryIndent == kNoIndent || indent == entryIndent) {
      // A program's own key (its uuid). Its fields follow, indented deeper.
      flush();
      fieldIndent = kNoIndent;
      if (!opensBlock) {
        continue; // `uuid: {}` — an entry with no fields
      }
      entryIndent = indent;
      currentValid = true;
      continue;
    }
    if (indent < entryIndent) {
      // Dedent out of External_Programs into another top-level-ish block.
      flush();
      programsIndent = kNoIndent;
      entryIndent = kNoIndent;
      fieldIndent = kNoIndent;
      continue;
    }
    if (!currentValid) {
      continue;
    }
    if (fieldIndent == kNoIndent) {
      fieldIndent = indent;
    }
    if (indent > fieldIndent) {
      // Inside a nested block belonging to one field (a program's
      // `environment:` map, say). Its keys are NOT the program's own — and
      // some of them share names with the ones read here — so the whole
      // subtree is skipped.
      continue;
    }
    if (opensBlock) {
      continue; // this field is itself a block; only its subtree follows
    }

    QString parsed;
    if (!scalarValue(value, parsed)) {
      currentValid = false; // a shape we do not understand — drop the entry
      continue;
    }
    const QString field = unquotedKey(key);
    if (field == QLatin1String("name")) {
      current.name = parsed;
    } else if (field == QLatin1String("executable")) {
      current.executable = parsed;
    } else if (field == QLatin1String("icon")) {
      // Bottles stores a themed icon name ("com.usebottles.bottles-program")
      // for most programs; only an actual file is usable as artwork.
      if (QDir::isAbsolutePath(parsed) && QFileInfo::exists(parsed)) {
        current.iconPath = parsed;
      }
    }
  }
  flush();

  for (Program &program : bottle.programs) {
    program.bottle = bottle.name;
  }
  if (bottle.name.isEmpty()) {
    return {}; // a config with no Name cannot be launched against
  }
  std::sort(bottle.programs.begin(), bottle.programs.end(), [](const Program &a, const Program &b) {
    return a.name.localeAwareCompare(b.name) < 0;
  });
  return bottle;
}

auto defaultDataDir() -> QString {
  const QString dataHome =
      QProcessEnvironment::systemEnvironment().value(QStringLiteral("XDG_DATA_HOME"));
  const QStringList candidates = {
      (dataHome.isEmpty() ? QDir::homePath() + QStringLiteral("/.local/share") : dataHome) +
          QStringLiteral("/bottles"),
      QDir::homePath() + QStringLiteral("/.var/app/com.usebottles.bottles/data/bottles"),
  };
  for (const QString &dir : candidates) {
    if (QDir(dir + QStringLiteral("/bottles")).exists()) {
      return dir;
    }
  }
  return {};
}

auto bottles(const QString &dataDir) -> QList<Bottle> {
  QList<Bottle> found;
  if (dataDir.isEmpty()) {
    return found;
  }
  const QDir root(dataDir + QStringLiteral("/bottles"));
  const QStringList names = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
  for (const QString &name : names) {
    const Bottle bottle = parseConfig(root.filePath(name) + QStringLiteral("/bottle.yml"));
    if (bottle.name.isEmpty() || bottle.programs.isEmpty()) {
      continue;
    }
    found.append(bottle);
  }
  return found;
}

auto installedPrograms(const QString &dataDir) -> QList<Program> {
  QList<Program> programs;
  for (const Bottle &bottle : bottles(dataDir)) {
    programs.append(bottle.programs);
  }
  std::sort(programs.begin(), programs.end(), [](const Program &a, const Program &b) {
    const int byName = a.name.localeAwareCompare(b.name);
    return byName != 0 ? byName < 0 : a.bottle.localeAwareCompare(b.bottle) < 0;
  });
  return programs;
}

auto stubLaunchArguments(const Program &program) -> QStringList {
  // Completes the Exec line Bottles writes into its own desktop entries:
  // `bottles-cli run -p "<program>" -b "<bottle>" --`, of which the template
  // contributes `run -p <program>`. The trailing `--` terminates option
  // parsing, which matters for a program name that starts with a dash.
  return {QStringLiteral("-b"), program.bottle, QStringLiteral("--")};
}

} // namespace BottlesLibrary
