#ifndef RETROARCHICONS_H
#define RETROARCHICONS_H

#include <QList>
#include <QString>
#include <QStringList>

#include "collectiontypes.h"

/// Reads the system icons out of a local RetroArch installation's assets
/// tree (Kartend-1kkk2), so a game collection can carry a small console /
/// controller / cartridge glyph beside its name in the navigation sidebar.
///
/// **Local only, and read-only.** Nothing here downloads: if RetroArch is
/// installed its icons are already on disk, they match the version the user
/// actually has, and they cost no network. The remote
/// `libretro/retroarch-assets` fetch — and wiring libretro in as a *scraper*
/// source for entity art — is a separate piece of work (Kartend-ygcn2); this
/// module deliberately does not touch the scraper or `cfg.collectionIcon`.
///
/// # Layout
///
/// Icon packs live at `<assets>/xmb/<pack>/png/`, one PNG per system named
/// after the libretro database it belongs to:
///
///     Nintendo - Super Nintendo Entertainment System.png          the system
///     Nintendo - Super Nintendo Entertainment System-content.png  its media
///
/// The same directory also holds RetroArch's own menu chrome (`add.png`,
/// `battery-full.png`, …), which must not be offered as a system. The
/// `-content` sibling is what separates them: every system icon has one, no
/// menu glyph does. That test is a property of how the packs are BUILT, so it
/// keeps working as packs gain systems — unlike a name-shaped guess, which
/// would have to decide whether `2048.png` is a menu entry or the game.
///
/// # Subject
///
/// What a system icon DEPICTS is a property of the pack, not a variant within
/// it: `monochrome` draws controllers for every system, `systematic` draws
/// consoles for every system. So asking for a console rather than a controller
/// is a choice of pack (Subject::Console / Subject::Controller), while
/// Subject::Content is orthogonal — it is the `-content` sibling, and every
/// pack ships one.
namespace RetroArchIcons {

/// One icon pack found under `<assets>/xmb/`.
struct Pack {
  /// Directory name — `monochrome`, `systematic`, … Also the value stored in
  /// the collection config, so it is validated as a path component on the way
  /// back in.
  QString id;
  /// Human-readable form of `id` for the picker ("Dot Art", "Flat UI").
  QString displayName;
  /// What this pack's system icons depict. Never Content — that subject is
  /// the per-file `-content` sibling, which every pack has.
  SystemIconSubject subject = SystemIconSubject::Controller;
  /// False when `id` is not in the curated subject table. Such a pack is
  /// still selectable (it is on disk and its icons render fine), it just
  /// cannot be picked automatically for a subject, because nothing here knows
  /// what it draws. Enumeration is always from disk — only the classification
  /// is curated — so a RetroArch update that adds a pack shows up without a
  /// Kartend change, merely unclassified.
  bool subjectKnown = false;
  /// How many systems the pack covers. Packs differ enormously here (~55 to
  /// ~362 on a current install), which is why the picker shows it: choosing a
  /// sparse pack is the difference between a glyph and a blank.
  int systemCount = 0;
};

/// `<assets>/xmb` — where the packs live. Empty when @p assetsDirectory is.
[[nodiscard]] QString packsRoot(const QString &assetsDirectory);

/// Enumerate the icon packs present on disk, sorted by display name.
/// Empty when the assets tree is missing or holds no pack with any system.
[[nodiscard]] QList<Pack> discoverPacks(const QString &assetsDirectory);

/// The system names @p packId covers ("Nintendo - Game Boy", …), sorted.
/// These are file base names with the extension dropped, and they are what a
/// collection stores — matching libretro's database naming, which is also
/// what RetroArch playlists use.
[[nodiscard]] QStringList discoverSystems(const QString &assetsDirectory, const QString &packId);

/// The pack to use for @p subject when the collection has no explicit
/// override, chosen from @p packs by a curated preference order (see
/// kConsolePacks / kControllerPacks in the .cpp). Returns empty when none of
/// the preferred packs is installed.
[[nodiscard]] QString defaultPackFor(SystemIconSubject subject, const QList<Pack> &packs);

/// THE resolution rule — which pack actually gets used, given a subject and
/// the collection's optional pack override. Every call site goes through this
/// so the rule cannot drift between the renderer, the settings page and the
/// bulk-apply path.
///
/// **The subject wins over a contradicting set** (user, 2026-08-22, reporting
/// "still showing controller icons, but i selected console/monochrome"):
///
///   * No override            → the curated pack for the subject.
///   * Override that AGREES   → the override, honoured as chosen.
///   * Override that CONFLICTS (asking for consoles from a controller set, or
///     the reverse) → the curated pack for the SUBJECT instead.
///
/// A set is chosen for its LOOK; the subject says what the icon has to be.
/// Since a pack holds exactly one icon per system, a controller set simply has
/// no console to hand back — so honouring the set there means silently
/// refusing the subject, which is what was reported. Note the styles line up
/// anyway: `monochrome` (silhouette controllers) and `automatic` (outline
/// consoles) are the same visual family, so asking for consoles in a
/// monochrome style lands somewhere sensible rather than nowhere.
///
/// An override whose subject is UNKNOWN never conflicts — nothing here knows
/// what an unclassified pack draws, so the user's explicit pick stands.
/// Subject::Content never conflicts either: every pack ships the `-content`
/// sibling, so any set can satisfy it.
[[nodiscard]] QString resolvePack(SystemIconSubject subject, const QString &packOverride,
                                  const QList<Pack> &packs);

/// Absolute path to @p systemName's icon, or empty when the pack does not
/// cover that system. @p subject selects the file variant: Content resolves
/// the `-content` sibling, Console and Controller both resolve the plain
/// system icon (they differ in which PACK is chosen, not which file).
///
/// Both @p packId and @p systemName arrive from the collection config and
/// become path components, so both are rejected unless they are safe ones —
/// a hand-edited or imported config cannot walk this out of the assets tree.
[[nodiscard]] QString iconPath(const QString &assetsDirectory, const QString &packId,
                               const QString &systemName, SystemIconSubject subject);

/// Best-guess system name for a collection called @p collectionName, drawn
/// from @p systems (as returned by discoverSystems). Returns empty when
/// nothing matches well enough OR when two candidates tie — an ambiguous
/// guess is worse than none, because the picker beside it already lets the
/// user say what they meant.
///
/// @p extraAliases are additional shorthand spellings for the collection, and
/// exist so the caller can hand over the alias list ScreenScraper already
/// resolved for the same collection (`snes`, `megadrive`, `psx`, …). Those
/// are exactly the tags that no amount of word matching recovers — "SNES"
/// shares not one word with "Super Nintendo Entertainment System". A small
/// built-in table covers the common ones when no catalog is available.
[[nodiscard]] QString autodetectSystem(const QString &collectionName, const QStringList &systems,
                                       const QStringList &extraAliases = QStringList());

} // namespace RetroArchIcons

#endif // RETROARCHICONS_H
