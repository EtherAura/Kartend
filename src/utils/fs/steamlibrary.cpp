#include "steamlibrary.h"

#include <algorithm>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace SteamLibrary {

namespace {

/// One token of VDF input: a string (quoted or bare) or a brace.
struct Token {
  enum Kind { String, Open, Close, End } kind = End;
  QString text;
};

class VdfLexer {
public:
  explicit VdfLexer(const QString &text) : m_text(text) {}

  Token next() {
    skipWhitespaceAndComments();
    if (m_pos >= m_text.size()) {
      return {Token::End, {}};
    }
    const QChar c = m_text[m_pos];
    if (c == '{') {
      ++m_pos;
      return {Token::Open, {}};
    }
    if (c == '}') {
      ++m_pos;
      return {Token::Close, {}};
    }
    if (c == '"') {
      return {Token::String, readQuoted()};
    }
    return {Token::String, readBare()};
  }

private:
  void skipWhitespaceAndComments() {
    while (m_pos < m_text.size()) {
      const QChar c = m_text[m_pos];
      if (c.isSpace()) {
        ++m_pos;
      } else if (c == '/' && m_pos + 1 < m_text.size() && m_text[m_pos + 1] == '/') {
        while (m_pos < m_text.size() && m_text[m_pos] != '\n') {
          ++m_pos;
        }
      } else {
        break;
      }
    }
  }

  QString readQuoted() {
    ++m_pos; // opening quote
    QString out;
    while (m_pos < m_text.size()) {
      const QChar c = m_text[m_pos];
      if (c == '\\' && m_pos + 1 < m_text.size()) {
        const QChar esc = m_text[m_pos + 1];
        if (esc == '"' || esc == '\\') {
          out.append(esc);
        } else if (esc == 'n') {
          out.append('\n');
        } else if (esc == 't') {
          out.append('\t');
        } else {
          // Unknown escape: keep both characters, matching Valve's lenient
          // reader (paths like C:\Games survive unescaped in the wild).
          out.append(c);
          out.append(esc);
        }
        m_pos += 2;
        continue;
      }
      if (c == '"') {
        ++m_pos;
        break;
      }
      out.append(c);
      ++m_pos;
    }
    return out;
  }

  QString readBare() {
    QString out;
    while (m_pos < m_text.size()) {
      const QChar c = m_text[m_pos];
      if (c.isSpace() || c == '{' || c == '}' || c == '"') {
        break;
      }
      out.append(c);
      ++m_pos;
    }
    return out;
  }

  const QString &m_text;
  qsizetype m_pos = 0;
};

// Parses one `{ key value ... }` body (the lexer has already consumed the
// opening brace, or depth==0 for the document root). Recursion depth is
// bounded to keep a maliciously nested file from exhausting the stack.
QVariantMap parseBody(VdfLexer &lexer, int depth) {
  constexpr int kMaxDepth = 32;
  QVariantMap map;
  if (depth > kMaxDepth) {
    return map;
  }
  QString pendingKey;
  bool haveKey = false;
  for (;;) {
    const Token tok = lexer.next();
    if (tok.kind == Token::End || tok.kind == Token::Close) {
      return map;
    }
    if (tok.kind == Token::Open) {
      if (haveKey) {
        map.insert(pendingKey, parseBody(lexer, depth + 1));
        haveKey = false;
      } else {
        // Brace with no key — swallow the subtree so the rest still parses.
        parseBody(lexer, depth + 1);
      }
      continue;
    }
    // `[$WIN32]`-style platform conditionals annotate the preceding pair;
    // they are not keys or values.
    if (tok.text.startsWith('[') && tok.text.endsWith(']')) {
      continue;
    }
    if (!haveKey) {
      pendingKey = tok.text;
      haveKey = true;
    } else {
      map.insert(pendingKey, tok.text);
      haveKey = false;
    }
  }
}

QString firstExistingFile(const QStringList &candidates) {
  for (const QString &path : candidates) {
    if (QFileInfo::exists(path)) {
      return path;
    }
  }
  return {};
}

} // namespace

auto parseVdf(const QString &text) -> QVariantMap {
  VdfLexer lexer(text);
  return parseBody(lexer, 0);
}

auto defaultRoot() -> QString {
  const QString home = QDir::homePath();
  const QStringList candidates = {
      home + QStringLiteral("/.steam/steam"),
      home + QStringLiteral("/.local/share/Steam"),
      home + QStringLiteral("/.var/app/com.valvesoftware.Steam/.local/share/Steam"),
      home + QStringLiteral("/.steam/root"),
  };
  for (const QString &root : candidates) {
    if (QDir(root + QStringLiteral("/steamapps")).exists()) {
      return root;
    }
  }
  return {};
}

auto libraryFolders(const QString &steamRoot) -> QStringList {
  QStringList roots;
  QStringList seenCanonical;
  const auto addRoot = [&roots, &seenCanonical](const QString &candidate) {
    if (candidate.isEmpty() || !QDir(candidate + QStringLiteral("/steamapps")).exists()) {
      return;
    }
    // canonicalFilePath collapses the ~/.steam/steam symlink onto the real
    // ~/.local/share/Steam so the same library isn't scanned twice.
    const QString canonical = QFileInfo(candidate).canonicalFilePath();
    if (canonical.isEmpty() || seenCanonical.contains(canonical)) {
      return;
    }
    seenCanonical.append(canonical);
    roots.append(candidate);
  };

  addRoot(steamRoot);

  QFile vdf(steamRoot + QStringLiteral("/steamapps/libraryfolders.vdf"));
  if (vdf.open(QIODevice::ReadOnly)) {
    const QVariantMap doc = parseVdf(QString::fromUtf8(vdf.readAll()));
    // Case differs between Steam vintages: "libraryfolders" vs "LibraryFolders".
    QVariantMap folders;
    for (auto it = doc.constBegin(); it != doc.constEnd(); ++it) {
      if (it.key().compare(QLatin1String("libraryfolders"), Qt::CaseInsensitive) == 0) {
        folders = it.value().toMap();
        break;
      }
    }
    for (auto it = folders.constBegin(); it != folders.constEnd(); ++it) {
      // Numeric keys only — the map also carries scalar bookkeeping keys
      // ("contentstatsid") in some vintages.
      bool isIndex = false;
      it.key().toInt(&isIndex);
      if (!isIndex) {
        continue;
      }
      if (it.value().canConvert<QVariantMap>() && it.value().toMap().contains("path")) {
        addRoot(it.value().toMap().value("path").toString());
      } else {
        addRoot(it.value().toString()); // legacy: "1"  "/mnt/games/SteamLibrary"
      }
    }
  }
  return roots;
}

auto installedGames(const QString &steamRoot) -> QList<Game> {
  QList<Game> games;
  QStringList seenAppIds;
  const QStringList roots = libraryFolders(steamRoot);
  for (const QString &libraryRoot : roots) {
    QDirIterator it(libraryRoot + QStringLiteral("/steamapps"),
                    {QStringLiteral("appmanifest_*.acf")}, QDir::Files);
    while (it.hasNext()) {
      QFile manifest(it.next());
      if (!manifest.open(QIODevice::ReadOnly)) {
        continue;
      }
      const QVariantMap appState =
          parseVdf(QString::fromUtf8(manifest.readAll())).value("AppState").toMap();
      Game game;
      game.appId = appState.value("appid").toString();
      game.name = appState.value("name").toString();
      game.installDir = appState.value("installdir").toString();
      game.libraryRoot = libraryRoot;
      if (game.appId.isEmpty() || game.name.isEmpty() || isRuntimeTool(game.name) ||
          seenAppIds.contains(game.appId)) {
        continue;
      }
      seenAppIds.append(game.appId);
      games.append(game);
    }
  }
  std::sort(games.begin(), games.end(),
            [](const Game &a, const Game &b) { return a.name.localeAwareCompare(b.name) < 0; });
  return games;
}

auto artworkFor(const QString &steamRoot, const QString &appId) -> Artwork {
  Artwork art;
  const QString cacheDir = steamRoot + QStringLiteral("/appcache/librarycache/");
  const QString subDir = cacheDir + appId + QLatin1Char('/');
  art.cover = firstExistingFile({
      subDir + QStringLiteral("library_600x900.jpg"),
      subDir + QStringLiteral("library_600x900.png"),
      cacheDir + appId + QStringLiteral("_library_600x900.jpg"),
      cacheDir + appId + QStringLiteral("_library_600x900.png"),
      subDir + QStringLiteral("header.jpg"),
      cacheDir + appId + QStringLiteral("_header.jpg"),
  });
  art.logo = firstExistingFile({
      subDir + QStringLiteral("logo.png"),
      cacheDir + appId + QStringLiteral("_logo.png"),
  });
  art.hero = firstExistingFile({
      subDir + QStringLiteral("library_hero.jpg"),
      subDir + QStringLiteral("library_hero.png"),
      cacheDir + appId + QStringLiteral("_library_hero.jpg"),
  });
  return art;
}

auto isRuntimeTool(const QString &name) -> bool {
  static const QRegularExpression kToolRe(
      QStringLiteral("^(Proton|Steam Linux Runtime|Steamworks Common Redistributables|SteamVR)"),
      QRegularExpression::CaseInsensitiveOption);
  return kToolRe.match(name).hasMatch();
}

} // namespace SteamLibrary
