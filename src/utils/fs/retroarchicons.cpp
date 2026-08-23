// System icons read out of a local RetroArch assets tree. See the header for
// the on-disk layout and for why the `-content` sibling is what separates a
// system icon from RetroArch's own menu chrome.
#include "retroarchicons.h"

#include "pathutils.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <algorithm>

namespace RetroArchIcons {

namespace {

// Bounds for enumeration, which runs inline on the GUI thread when the create
// dialog or the settings page opens — the same reasoning (and the same
// ceiling) as RetroArchUtils::kMaxCoresInspected. A real pack holds a few
// hundred systems; a pathological assets tree yields a bounded result set
// rather than blocking the UI for the full walk.
constexpr int kMaxPackEntriesInspected = 50000;
constexpr int kMaxPacksInspected = 512;

// Suffix that marks a system's media icon, and the thing that identifies a
// system icon at all: `<System>-content.png` beside `<System>.png`.
constexpr QLatin1StringView kContentSuffix("-content");
constexpr QLatin1StringView kPngSuffix(".png");

// Curated preference order per subject. ENUMERATION is always from disk (see
// discoverPacks) — this table only decides which of the installed packs is
// picked AUTOMATICALLY, and what an unfamiliar pack is not allowed to be
// chosen for. A RetroArch update that adds a pack still lists it; it simply
// arrives unclassified until someone looks at it and says what it draws.
//
// Verified against a 2026-08 install: every pack is consistent about its
// subject across systems, so classifying the pack classifies its icons.
// Order within each list is preference, and it is deliberately led by the
// white-on-transparent silhouette packs — those recolour cleanly through the
// sidebar's existing tint/monochrome styles, where the full-colour packs
// fight them.
const QStringList &controllerPacks() {
  static const QStringList kPacks = {
      QStringLiteral("monochrome"), QStringLiteral("retrosystem"), QStringLiteral("flatux"),
      QStringLiteral("daite"),      QStringLiteral("pixel"),       QStringLiteral("flatui"),
  };
  return kPacks;
}

const QStringList &consolePacks() {
  static const QStringList kPacks = {
      QStringLiteral("automatic"),
      QStringLiteral("systematic"),
      QStringLiteral("dot-art"),
  };
  return kPacks;
}

// Shorthand nobody recovers by matching words: "SNES" shares no word with
// "Super Nintendo Entertainment System". Deliberately SMALL — the caller can
// pass ScreenScraper's own alias list for the collection (recalbox / retropie
// / launchbox tags), which is broader and stays current; this covers the
// offline case and the platforms users type as initials.
//
// Keyed by normalised alias, valued by a normalised fragment that DOES appear
// in the libretro system name.
const QHash<QString, QString> &builtinAliases() {
  static const QHash<QString, QString> kAliases = {
      {QStringLiteral("nes"), QStringLiteral("nintendo entertainment system")},
      {QStringLiteral("famicom"), QStringLiteral("nintendo entertainment system")},
      {QStringLiteral("snes"), QStringLiteral("super nintendo entertainment system")},
      {QStringLiteral("sfc"), QStringLiteral("super nintendo entertainment system")},
      {QStringLiteral("superfamicom"), QStringLiteral("super nintendo entertainment system")},
      {QStringLiteral("n64"), QStringLiteral("nintendo 64")},
      {QStringLiteral("gc"), QStringLiteral("gamecube")},
      {QStringLiteral("ngc"), QStringLiteral("gamecube")},
      {QStringLiteral("gb"), QStringLiteral("game boy")},
      {QStringLiteral("gbc"), QStringLiteral("game boy color")},
      {QStringLiteral("gba"), QStringLiteral("game boy advance")},
      {QStringLiteral("nds"), QStringLiteral("nintendo ds")},
      {QStringLiteral("psx"), QStringLiteral("playstation")},
      {QStringLiteral("ps1"), QStringLiteral("playstation")},
      {QStringLiteral("ps2"), QStringLiteral("playstation 2")},
      {QStringLiteral("ps3"), QStringLiteral("playstation 3")},
      {QStringLiteral("psp"), QStringLiteral("playstation portable")},
      {QStringLiteral("psvita"), QStringLiteral("playstation vita")},
      {QStringLiteral("vita"), QStringLiteral("playstation vita")},
      {QStringLiteral("md"), QStringLiteral("mega drive")},
      {QStringLiteral("genesis"), QStringLiteral("mega drive")},
      {QStringLiteral("megadrive"), QStringLiteral("mega drive")},
      {QStringLiteral("sms"), QStringLiteral("master system")},
      {QStringLiteral("gg"), QStringLiteral("game gear")},
      {QStringLiteral("scd"), QStringLiteral("mega cd")},
      {QStringLiteral("segacd"), QStringLiteral("mega cd")},
      {QStringLiteral("dc"), QStringLiteral("dreamcast")},
      {QStringLiteral("tg16"), QStringLiteral("turbografx")},
      {QStringLiteral("pce"), QStringLiteral("pc engine")},
      {QStringLiteral("ngp"), QStringLiteral("neo geo pocket")},
      {QStringLiteral("ws"), QStringLiteral("wonderswan")},
      {QStringLiteral("c64"), QStringLiteral("commodore 64")},
      {QStringLiteral("a2600"), QStringLiteral("atari 2600")},
      {QStringLiteral("vb"), QStringLiteral("virtual boy")},
  };
  return kAliases;
}

/// Lowercase, drop everything that is not a letter or digit, and collapse the
/// gaps to single spaces. "Sony - PlayStation®" and "sony playstation" have to
/// land on the same string for any of the matching below to work, and the
/// libretro names are full of punctuation Kartend's collection names are not
/// ("Non-Redump - Sony - PlayStation", "Sega - Mega Drive - Genesis").
QString normalise(const QString &raw) {
  QString out;
  out.reserve(raw.size());
  bool pendingSpace = false;
  for (const QChar c : raw) {
    if (c.isLetterOrNumber()) {
      if (pendingSpace && !out.isEmpty()) {
        out.append(QLatin1Char(' '));
      }
      pendingSpace = false;
      out.append(c.toLower());
    } else {
      pendingSpace = true;
    }
  }
  return out;
}

QStringList tokens(const QString &normalised) {
  return normalised.isEmpty() ? QStringList()
                              : normalised.split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

/// Drop parenthesised qualifiers. libretro carries DAT SCOPE in brackets, not
/// system identity: "IBM - PC and Compatibles (Digital) (Steam) (Hentai)" is
/// the IBM PC catalogued three ways over, and there are twenty such variants
/// of that one machine.
///
/// Matching against the raw name made every one of those qualifiers a
/// matchable word, which is how a collection named "Steam" landed on that
/// exact entry — the only place the word appears in the whole set (field
/// report 2026-08-22). A storefront is not a machine, and nothing should
/// attach an adult-content DAT to a library because its name happened to
/// appear inside a bracket.
///
/// Qualifiers are stripped for MATCHING only. The full name stays the stored
/// identity and the filename, and the tie-break below still runs on it, so
/// the LEAST-qualified variant is what a bare "PC" resolves to.
QString withoutQualifiers(const QString &systemName) {
  QString out;
  out.reserve(systemName.size());
  int depth = 0;
  for (const QChar c : systemName) {
    if (c == QLatin1Char('(') || c == QLatin1Char('[')) {
      ++depth;
    } else if (c == QLatin1Char(')') || c == QLatin1Char(']')) {
      depth = qMax(0, depth - 1);
    } else if (depth == 0) {
      out.append(c);
    }
  }
  return out;
}

/// The part of a libretro name after the manufacturer — "Nintendo - Game Boy"
/// -> "Game Boy". Collections are far more often named for the system than
/// for who made it, so this is the form most likely to match. Returns the
/// whole name when there is no separator.
QString withoutManufacturer(const QString &systemName) {
  const int sep = systemName.lastIndexOf(QStringLiteral(" - "));
  return sep < 0 ? systemName : systemName.mid(sep + 3);
}

/// Turn a pack directory name into something for the picker: `dot-art` ->
/// "Dot Art", `flatui` -> "Flatui". Cosmetic only — `id` stays authoritative.
QString titleise(const QString &id) {
  QString out = id;
  out.replace(QLatin1Char('-'), QLatin1Char(' '));
  out.replace(QLatin1Char('_'), QLatin1Char(' '));
  if (!out.isEmpty()) {
    out[0] = out[0].toUpper();
  }
  for (int i = 1; i < out.size(); ++i) {
    if (out[i - 1] == QLatin1Char(' ')) {
      out[i] = out[i].toUpper();
    }
  }
  return out;
}

/// The system names in a pack's `png/` directory, as a set for cheap
/// membership tests. A name qualifies when both `<name>.png` and
/// `<name>-content.png` are present — see the header.
QSet<QString> systemSetForPackDir(const QString &pngDir) {
  QSet<QString> plain;
  QSet<QString> content;
  QDir dir(pngDir);
  if (!dir.exists()) {
    return {};
  }
  const QStringList entries =
      dir.entryList({QStringLiteral("*.png")}, QDir::Files | QDir::NoSymLinks, QDir::Name);
  int inspected = 0;
  for (const QString &entry : entries) {
    if (++inspected > kMaxPackEntriesInspected) {
      break;
    }
    if (!entry.endsWith(kPngSuffix, Qt::CaseInsensitive)) {
      continue;
    }
    QString base = entry.left(entry.size() - kPngSuffix.size());
    if (base.endsWith(kContentSuffix, Qt::CaseInsensitive)) {
      content.insert(base.left(base.size() - kContentSuffix.size()));
    } else {
      plain.insert(std::move(base));
    }
  }
  return plain.intersect(content);
}

} // namespace

QString packsRoot(const QString &assetsDirectory) {
  if (assetsDirectory.trimmed().isEmpty()) {
    return {};
  }
  return QDir::cleanPath(QDir(assetsDirectory).filePath(QStringLiteral("xmb")));
}

QList<Pack> discoverPacks(const QString &assetsDirectory) {
  QList<Pack> packs;
  const QString root = packsRoot(assetsDirectory);
  if (root.isEmpty()) {
    return packs;
  }
  QDir rootDir(root);
  if (!rootDir.exists()) {
    return packs;
  }
  const QStringList packDirs =
      rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name);
  int inspected = 0;
  for (const QString &packId : packDirs) {
    if (++inspected > kMaxPacksInspected) {
      break;
    }
    if (!PathUtils::isSafePathComponent(packId)) {
      continue;
    }
    const int count = systemSetForPackDir(rootDir.filePath(packId + QStringLiteral("/png"))).size();
    if (count == 0) {
      // Not an icon pack — an assets subdirectory that holds fonts, shaders or
      // nothing we can use. Offering it would put a dead entry in the picker.
      continue;
    }
    Pack pack;
    pack.id = packId;
    pack.displayName = titleise(packId);
    pack.systemCount = count;
    if (consolePacks().contains(packId)) {
      pack.subject = SystemIconSubject::Console;
      pack.subjectKnown = true;
    } else if (controllerPacks().contains(packId)) {
      pack.subject = SystemIconSubject::Controller;
      pack.subjectKnown = true;
    }
    packs.append(pack);
  }
  std::sort(packs.begin(), packs.end(), [](const Pack &a, const Pack &b) {
    return a.displayName.compare(b.displayName, Qt::CaseInsensitive) < 0;
  });
  return packs;
}

QStringList discoverSystems(const QString &assetsDirectory, const QString &packId) {
  const QString root = packsRoot(assetsDirectory);
  if (root.isEmpty() || !PathUtils::isSafePathComponent(packId)) {
    return {};
  }
  const QSet<QString> systems =
      systemSetForPackDir(QDir(root).filePath(packId + QStringLiteral("/png")));
  QStringList out(systems.cbegin(), systems.cend());
  std::sort(out.begin(), out.end(), [](const QString &a, const QString &b) {
    return a.compare(b, Qt::CaseInsensitive) < 0;
  });
  return out;
}

QString defaultPackFor(SystemIconSubject subject, const QList<Pack> &packs) {
  // Content lives in every pack, so it has no pack family of its own — it
  // rides on the controller order, which is led by the silhouette packs and
  // has the broadest system coverage on a normal install.
  const QStringList &preferred =
      subject == SystemIconSubject::Console ? consolePacks() : controllerPacks();
  for (const QString &candidate : preferred) {
    const auto it = std::find_if(packs.cbegin(), packs.cend(),
                                 [&candidate](const Pack &p) { return p.id == candidate; });
    if (it != packs.cend()) {
      return it->id;
    }
  }
  return {};
}

QString resolvePack(SystemIconSubject subject, const QString &packOverride,
                    const QList<Pack> &packs) {
  const QString trimmed = packOverride.trimmed();
  if (trimmed.isEmpty()) {
    return defaultPackFor(subject, packs);
  }
  // Content rides in every pack, so an override can always satisfy it.
  if (subject == SystemIconSubject::Content) {
    return trimmed;
  }
  const auto chosen = std::find_if(packs.cbegin(), packs.cend(),
                                   [&trimmed](const Pack &p) { return p.id == trimmed; });
  // Unknown to us, or already the right kind — the user's pick stands.
  if (chosen == packs.cend() || !chosen->subjectKnown || chosen->subject == subject) {
    return trimmed;
  }
  // Conflict. The subject is the requirement and the set is the preference, so
  // the subject wins — see the header for why honouring the set here means
  // silently refusing what was asked for. Falls back to the override when no
  // pack of the requested kind is installed, since a wrong-subject icon still
  // beats a blank row.
  const QString bySubject = defaultPackFor(subject, packs);
  return bySubject.isEmpty() ? trimmed : bySubject;
}

QString iconPath(const QString &assetsDirectory, const QString &packId, const QString &systemName,
                 SystemIconSubject subject) {
  const QString root = packsRoot(assetsDirectory);
  if (root.isEmpty() || systemName.trimmed().isEmpty()) {
    return {};
  }
  // Both become path components and both arrive from the collection config,
  // which a hand-edited INI or an imported .kart can set. Reject anything that
  // is not a plain component before it reaches the filesystem (cf.
  // Kartend-9guwj, third-party values becoming read paths).
  if (!PathUtils::isSafePathComponent(packId) || !PathUtils::isSafePathComponent(systemName)) {
    return {};
  }
  QString fileBase = systemName;
  if (subject == SystemIconSubject::Content) {
    fileBase += kContentSuffix;
  }
  const QString path =
      QDir(root).filePath(packId + QStringLiteral("/png/") + fileBase + kPngSuffix);
  return QFileInfo::exists(path) ? QDir::cleanPath(path) : QString();
}

QString autodetectSystem(const QString &collectionName, const QStringList &systems,
                         const QStringList &extraAliases) {
  const QString wanted = normalise(collectionName);
  if (wanted.isEmpty() || systems.isEmpty()) {
    return {};
  }

  // Build the set of spellings that stand for this collection: its own name,
  // whatever the caller resolved elsewhere, and the built-in expansion for
  // each — so "SNES" gets to compete as "super nintendo entertainment system"
  // without the caller having to know that.
  QStringList spellings;
  spellings.append(wanted);
  for (const QString &alias : extraAliases) {
    const QString normalised = normalise(alias);
    if (!normalised.isEmpty() && !spellings.contains(normalised)) {
      spellings.append(normalised);
    }
  }
  // LONGEST ALIAS WINS, and consumes the words it matched.
  //
  // Field report 2026-08-22: a collection called "Super Famicom - Super
  // Nintendo Entertainment System" resolved to the NES. Probing every token
  // independently let the word "famicom" fire its own alias (-> the NES) from
  // inside "Super Famicom", and that expansion then matched the NES entry
  // EXACTLY while the real answer only matched by containment — so the wrong
  // system outscored the right one.
  //
  // Walking the words left to right and taking the longest alias at each
  // position fixes it: "super famicom" is matched as one thing and its words
  // are consumed, so "famicom" never gets to speak for itself. A collection
  // called "My SNES Games" still reaches the expansion, because the walk tries
  // every position rather than only the whole string.
  constexpr int kMaxAliasWords = 3;
  const int directSpellings = spellings.size();
  for (int i = 0; i < directSpellings; ++i) {
    const QStringList words = tokens(spellings.at(i));
    for (int w = 0; w < words.size();) {
      int matchedWords = 0;
      QString expanded;
      for (int n = qMin(kMaxAliasWords, static_cast<int>(words.size()) - w); n >= 1; --n) {
        // Aliases are keyed unspaced ("superfamicom"), so a multi-word run and
        // the same thing typed as one word land on the same entry.
        expanded = builtinAliases().value(QStringList(words.mid(w, n)).join(QString()));
        if (!expanded.isEmpty()) {
          matchedWords = n;
          break;
        }
      }
      if (matchedWords == 0) {
        ++w;
        continue;
      }
      if (!spellings.contains(expanded)) {
        spellings.append(expanded);
      }
      w += matchedWords;
    }
  }

  // Score every system; keep the best and remember whether it was tied.
  //
  //   4  a spelling equals the system name, or the part after the manufacturer
  //   3  a spelling is contained in either form
  //   2  every word of a spelling appears in the system name
  //
  // Ties are resolved toward the SHORTEST system name, which is what makes
  // "Nintendo - Game Boy" win over "Nintendo - Game Boy Advance" for a
  // collection called "Game Boy": both contain every word, but the shorter one
  // spends none of its name on something the collection did not ask for. A
  // tie that survives even that is reported as no match.
  QString best;
  int bestScore = 0;
  int bestLength = 0;
  bool tied = false;

  for (const QString &system : systems) {
    // Scored on the QUALIFIER-STRIPPED name — see withoutQualifiers. The
    // tie-break below still measures the FULL name, so among variants that
    // score alike the plainest one wins: "PC" reaches "IBM - PC and
    // Compatibles" rather than one of its nineteen DAT-scoped siblings.
    const QString base = withoutQualifiers(system);
    const QString full = normalise(base);
    const QString shortForm = normalise(withoutManufacturer(base));
    if (full.isEmpty()) {
      continue;
    }
    int score = 0;
    for (const QString &spelling : std::as_const(spellings)) {
      if (spelling == full || spelling == shortForm) {
        score = std::max(score, 4);
        continue;
      }
      if (full.contains(spelling) || shortForm.contains(spelling)) {
        score = std::max(score, 3);
        continue;
      }
      const QStringList wantedTokens = tokens(spelling);
      if (wantedTokens.isEmpty()) {
        continue;
      }
      const QStringList haveTokens = tokens(full);
      const bool allPresent =
          std::all_of(wantedTokens.cbegin(), wantedTokens.cend(),
                      [&haveTokens](const QString &t) { return haveTokens.contains(t); });
      if (allPresent) {
        score = std::max(score, 2);
      }
    }
    if (score == 0) {
      continue;
    }
    // Measured on the FULL name, NOT the stripped one the scoring used.
    // Stripping collapses every DAT variant of a machine to the same length,
    // so measuring that would make all twenty IBM PC entries tie and the
    // whole match dissolve into "ambiguous". The full length is what ranks
    // the plain entry above its qualified siblings.
    const int length = normalise(system).size();
    if (score > bestScore || (score == bestScore && length < bestLength)) {
      best = system;
      bestScore = score;
      bestLength = length;
      tied = false;
    } else if (score == bestScore && length == bestLength && system != best) {
      tied = true;
    }
  }

  // An ambiguous guess is worse than none: the row it lands on is wrong and
  // the user has no reason to look. The picker beside this is one click.
  return tied ? QString() : best;
}

} // namespace RetroArchIcons
