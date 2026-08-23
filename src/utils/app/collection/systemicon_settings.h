#ifndef KARTEND_UTILS_APP_COLLECTION_SYSTEMICON_SETTINGS_H
#define KARTEND_UTILS_APP_COLLECTION_SYSTEMICON_SETTINGS_H

// Kartend-1kkk2: per-collection state for the small RetroArch system glyph
// drawn beside a collection's name in the navigation sidebar. Its own leaf
// cluster with its own INI I/O pair, like CollectionTreeSettings and
// SidebarAppearance — accessed as `cfg.systemIcon.enabled` etc.

#include <QHash>
#include <QString>

#include "collectiontypes.h"

/// The RetroArch-sourced system glyph for one collection.
///
/// **Deliberately not part of CollectionTreeSettings**, though it renders in
/// the same panel (user decision 2026-08-22: "an additional option set").
/// Everything in that struct governs the ROW ARTWORK — the scraped wheel or
/// logo, its size, style and tint. This is a different mark from a different
/// source with a different job: a small fixed glyph that says *what machine
/// this is*, drawn beside the name (see `placement`) in every TreeIconDisplay
/// mode that shows one, including the TextOnly default. Folding it in would
/// have tied it to display modes it is meant to be independent of, and the
/// default is TextOnly, so it would have been invisible out of the box.
///
/// It also does not touch `cfg.collectionIcon`. That slot is owned by the
/// scraper (see EntityScrapeCoordinator::applyEntityArtToConfig) and holds
/// per-collection art the user may have chosen by hand; a locally-resolved
/// RetroArch path has no business overwriting it.
///
/// Nothing here stores a resolved PATH. The glyph is resolved at paint time
/// from (assets directory, pack, system, subject) so that moving, updating or
/// re-theming a RetroArch install is picked up without a rescan, and so a
/// config shared between machines carries the system's IDENTITY rather than
/// one machine's filesystem layout.
struct SystemIconSettings {
  /// Draw the glyph for this collection. OFF by default — the sidebar is
  /// deliberately quiet (treeIconDisplay defaults TextOnly for the same
  /// reason), and a machine with no RetroArch installed has nothing to draw,
  /// so this turns itself on when a collection is created with a system that
  /// actually resolved rather than arriving unannounced.
  bool enabled = false;
  /// The libretro system name, matching the icon's file base name and
  /// RetroArch's own playlist naming ("Nintendo - Game Boy Advance").
  /// Empty means no glyph, whatever `enabled` says.
  ///
  /// Stored as the system's IDENTITY, not as a path — see the struct note.
  /// Becomes a path component at resolve time, so RetroArchIcons::iconPath
  /// validates it before it reaches the filesystem.
  QString systemName;
  /// True when `systemName` was written by DETECTION rather than chosen by
  /// the user (user report 2026-08-23: "cant seem to get the snes icon").
  ///
  /// Without this, "already has a system" was read as "the user picked it",
  /// so a bulk apply refused to touch it — and a wrong guess from an older,
  /// buggier matcher was preserved forever. A collection named "Super Famicom
  /// - Super Nintendo Entertainment System" that once resolved to the NES
  /// could never be corrected by re-running detection, which is the obvious
  /// way to try.
  ///
  /// With it, re-running detection REFRESHES its own past guesses and still
  /// never overrides a hand-picked system. Set true by the create dialog's
  /// autodetect, by the Detect button, and by the bulk apply; set false the
  /// moment the user picks a system themselves.
  bool systemAutoDetected = false;
  /// What the glyph depicts. Console and Controller select different PACKS
  /// of the same per-system file; Content selects the `-content` sibling
  /// (cartridge, disc, tape) within whichever pack is in play.
  SystemIconSubject subject = SystemIconSubject::Controller;
  /// Icon pack to source from. EMPTY means "whichever pack suits `subject`"
  /// (RetroArchIcons::defaultPackFor), which is the setting most users should
  /// leave alone.
  ///
  /// An explicit pack is honoured STRICTLY: if it does not cover this
  /// system, the row simply gets no glyph. Packs vary from ~55 to ~362
  /// systems, so a silent fallback to a pack that does cover it would quietly
  /// mix two art styles down a column — and having asked for one pack by
  /// name, a blank is the honest answer. The curated default has no such
  /// problem because it walks its own preference order.
  QString packOverride;
  /// How the glyph is INKED (user request 2026-08-23: "there should be a new
  /// system icon option that would apply light dark or tinted, like the other
  /// icon options"). Reuses the row artwork's vocabulary deliberately — it is
  /// the same question about the same panel, and a second enum saying the same
  /// four things would only invite the two to drift.
  ///
  /// Applies to BOTH sources this slot can draw: a RetroArch system icon and
  /// the collection's own artwork. Before this existed the two were inked by
  /// different hardcoded rules, so a manufacturer logo and the console icons
  /// beneath it did not match — which is what prompted the request.
  ///
  /// Normal leaves art in its own colours, EXCEPT for a source that is a flat
  /// silhouette: RetroArch's monochrome and automatic sets are
  /// white-on-transparent and would be invisible on a light theme, so those
  /// are always inked to the label colour whatever the style says.
  TreeIconStyle style = TreeIconStyle::Normal;
  /// Draw the collection's OWN artwork when it names no system (user request
  /// 2026-08-23: "want to be able to clear/override nav bar icons
  /// individually").
  ///
  /// This is what gives a manufacturer shell its company logo. It is a
  /// separate switch rather than an automatic fallback so that "no system"
  /// can also mean *no icon at all* — with the fallback implicit there was no
  /// way to say that, and a row whose logo merely repeats its own name had no
  /// way to turn the icon off (user: "in some cases manufacturer logo+text too
  /// is redundant").
  bool useCollectionArtwork = false;
  /// Which side of the name the glyph sits on, and whether it hugs the name
  /// or pins to the panel edge (user request 2026-08-22). Defaults to
  /// BeforeName — the placement this shipped with earlier the same day.
  SystemIconPlacement placement = SystemIconPlacement::BeforeName;
  /// Glyph height in px. Small by intent — this sits beside the name at
  /// roughly text height, and is NOT `treeIconSize` (which sizes the row
  /// artwork and reaches 512). Clamped by the persistence layer.
  int iconSize = 16;

  static constexpr int kMinIconSize = 8;
  /// Modest ceiling on purpose: past about this the glyph stops reading as a
  /// mark beside a name and starts being the row's artwork, which is what
  /// treeIconSize is for.
  static constexpr int kMaxIconSize = 64;

  bool operator==(const SystemIconSettings &other) const = default;
};

// Fingerprint hash for the settings hot-reload diff baseline (Kartend-lc58a) —
// must hash exactly the fields operator== compares (see gridlayoutpreferences.h).
inline size_t qHash(const SystemIconSettings &key, size_t seed = 0) {
  return qHashMulti(seed, key.enabled, key.systemName, static_cast<int>(key.subject),
                    key.systemAutoDetected, key.packOverride, static_cast<int>(key.placement),
                    static_cast<int>(key.style), key.useCollectionArtwork, key.iconSize);
}

#endif // KARTEND_UTILS_APP_COLLECTION_SYSTEMICON_SETTINGS_H
