#ifndef DATAUDITREGION_H
#define DATAUDITREGION_H

#include <QString>
#include <QStringList>

/// No-Intro / Redump-style 1G1R helpers. Those catalogues encode a game's
/// region + revision in parenthetical tags on the title (e.g.
/// "Sonic the Hedgehog (USA) (Rev 1)") with no explicit parent/clone column,
/// so "one game, one ROM" means: group a title's region variants by base name
/// and keep one per game by a user-chosen region priority. These pure string
/// heuristics are the core of that grouping — exposed for the audit runner and
/// for direct unit testing.
namespace DatAudit::Region {

/// The title with every trailing " (...)" / " [...]" tag group stripped, then
/// trimmed — the key that collapses the region/revision variants of one game.
/// "Sonic (USA) (Rev 1)" -> "Sonic". A title with no tags is returned trimmed.
[[nodiscard]] QString baseGameName(const QString &gameName);

/// The canonical No-Intro region token detected from the title's parenthetical
/// tags ("USA", "Europe", "Japan", "World", ...), or an empty string when none
/// is recognised. Handles full names ("(Japan)"), the legacy single letters
/// ("(J)" / "(U)" / "(E)" / "(W)"), and multi-region tags ("(Japan, USA)" ->
/// "Japan", first listed token wins). Case-insensitive.
[[nodiscard]] QString detectRegion(const QString &gameName);

/// Rank of `region` within the user's priority list (0 = most preferred).
/// Case-insensitive. A region absent from the list ranks after every listed
/// one; an empty region ranks last of all, so an untagged variant never beats
/// a tagged, listed one when picking the keeper for a game.
[[nodiscard]] int regionRank(const QString &region, const QStringList &priority);

/// The canonical region tokens detectRegion() can return, in a sensible
/// default priority order. Seeds the profile editor's region picker so its
/// vocabulary always matches detectRegion() / regionRank().
[[nodiscard]] QStringList knownRegions();

/// The disc/track qualifier embedded in a title, normalised to lowercase
/// ("disc 1", "side a"), or an empty string when there is none. "Disk N" is
/// folded to "disc n". This is what keeps the discs of a multi-disc release
/// from collapsing into one game under 1G1R.
[[nodiscard]] QString discQualifier(const QString &gameName);

/// The 1G1R grouping key: baseGameName() plus the discQualifier(), so a game's
/// region variants share a key while its separate discs do not. Use this (not
/// baseGameName alone) to group catalogue entries for the completeness collapse.
[[nodiscard]] QString groupKey(const QString &gameName);

} // namespace DatAudit::Region

#endif // DATAUDITREGION_H
