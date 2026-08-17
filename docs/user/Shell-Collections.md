# Shell Collections

A **shell collection** is a collection that has no media directory and
no launcher of its own — it exists purely to *group* other collections
under a named heading. Think of it as a folder for your collections,
the same way a [Collection](Collections.md) is a folder for items.

The name comes from the shape: a shell is a frame with no contents of
its own. You give it children, and the children carry the launchers
and media.

## When to use one

Shell collections shine when you have a library that splits naturally
into a few high-level categories, each containing several "real"
collections:

```
Video (shell)
├── Films               (mpv)
├── TV Shows            (mpv, --no-resume-playback)
└── Documentaries       (mpv)

Audio (shell)
├── Albums              (mpv --no-video)
├── Audiobooks          (mpv --no-video --save-position-on-quit)
└── Podcasts            (mpv --no-video)

Reference (shell)
├── Manuals             (xdg-open, .pdf)
├── Cheat Sheets        (xdg-open, .pdf)
└── Reports             (xdg-open, .pdf)
```

Each shell renders at the root of the sidebar tree as a tile that opens
its children on `Enter`. The shell itself never tries to scan a media
directory or hand anything to a launcher — everything happens one level
down, in the leaf collections it contains.

## When *not* to use one

If your goal is just to filter the visible set of collections by a tag
like genre or category, the **collection type** field on each
collection plus the [type filter](Search-Sort-Filter.md#type-filter)
is usually a better fit — it preserves a flat sidebar and lets a
single collection appear in multiple categorical views.

Reach for a shell when you want the *navigation* to mirror the
hierarchy: when a user opens "Video" they see Films / TV Shows /
Documentaries as tiles, not the union of every video in the library.

## Creating one

1. Open the **Settings Dialog** with `Ctrl + ,` and click **Add
   Collection**.
2. On the **Basic** tab, give it a name (e.g. `Video`).
3. Skip the **Paths & Extensions** and **Launcher** tabs entirely —
   leave everything blank.
4. Drag your existing collections under it in the tree on the left, or
   set their **Parent Collection** field to the shell's name.

That's it. Kartend's validator emits a soft warning ("no media
directory specified") on a collection without media, but it suppresses
that warning the moment the collection has at least one child — the
[validator](Configuration-Reference.md) treats parent-only collections
as legal first-class citizens.

> **Where to find this** — Settings Dialog → tabs **Basic**,
> **Sidebar**, **Appearance**, **Colors**.

## INI form

A shell collection looks like any other in `kartend.cfg` — the absence
of `mediaDirectory` and `launcherPath` is what makes it a shell:

```ini
[Video]
name=Video
type=Video
collectionIcon=~/Pictures/icons/video.png

[Video > Films]
name=Films
mediaDirectory=~/Videos/Films
launcherPath=/usr/bin/mpv
launchParameters=--fullscreen
extensions=mkv,mp4,avi,webm

[Video > TV Shows]
name=TV Shows
mediaDirectory=~/Videos/TV
launcherPath=/usr/bin/mpv
launchParameters=--fullscreen --no-resume-playback
extensions=mkv,mp4,avi
```

Subcollections record their parent in the `[Parent > Child]` section
header itself — there's no separate parent-pointer key. Renaming or
reordering collections in the Settings Dialog rewrites the headers
automatically.

## What a shell collection *can* still have

Even without media, a shell collection still owns the per-collection
appearance fields. Useful ones in this context:

| Field | What it does |
|-------|--------------|
| **Type** | Tags the shell for the [type filter](Search-Sort-Filter.md#type-filter). Children inherit nothing — set their own type if you want them to appear under the same filter. |
| **Collection Icon** | The image painted on the shell's tile (icon column of List view, tile in Grid). Children paint their own icons inside the shell once you open it. |
| **Header Logo** | Logo at the top of the shell's grid (visible when you've drilled in). |
| **Background** | Per-collection background color / image / video — the shell's grid honours this even though it contains no media. |
| **Grid Width / Tile Size / Spacing** | Layout knobs that govern how the *children's* tiles render when the shell is open. |

What a shell collection *cannot* have:

- A media directory — by definition. Adding one promotes it from shell
  to a leaf collection, and items would start appearing alongside the
  child tiles. Kartend permits this but it's almost never what you
  want.
- A launcher — there's no item-level launch path through a shell.
  `Enter` on a shell tile opens its children, not a launcher.
- A working [Show All Subcollection Items](Configuration-Reference.md)
  toggle — the toggle exists on every collection, but on a shell it
  has nothing to flatten upward into.

## Nesting shells

Shells can contain other shells. The depth limit is whatever the
[parent-cycle check](Collections.md#hierarchies-parents-and-subcollections)
permits (currently `MAX_HIERARCHY_DEPTH` = 32 levels). In practice,
two levels covers almost every library:

```
Library (shell, root)
├── Video (shell)
│   ├── Films
│   └── TV Shows
├── Audio (shell)
│   ├── Albums
│   └── Audiobooks
└── Reference (shell)
    ├── Manuals
    └── Cheat Sheets
```

A single top-level wrapper shell like `Library` above is more useful
than it might first appear. The tile area always shows whichever
collection is currently active, so wrapping every category under one
root shell gives you a tile-based home view: the wrapper *becomes* the
canonical landing surface, with each category showing up as a tile you
can navigate into and `Back` out of.

Set the wrapper as your `startupCollection` (Settings → General, or
INI: `[General] startupCollection=Library`) and Kartend boots straight
into the home view.

If you'd rather not maintain a wrapper collection at all, the same
landing experience is available built-in: enable **Settings → General
→ Use Home View** (`[General] useHomeView=true`). Kartend boots into a
synthetic tile grid containing one tile per root collection, and `Back`
from any root-level collection returns there. The wrapper-shell pattern
remains useful when you want a real collection at the top with its own
appearance overrides (background, header logo, layout knobs); the
built-in Home view is preferable when you want zero collection
bookkeeping.

## Alias parents and shells

A shell collection can be the [alias parent](Collections.md#linked-parents-alias-parents)
of a collection that already lives elsewhere — useful when one
collection naturally belongs in two categories. Example: a `Concerts`
collection lives under `Video`, but you alias-parent it under `Audio`
too so it shows up in both shells without duplicating the media.

The aliasing is one-way: changes to the aliased collection (rename,
reparent, delete) reflect in both places, and removing the alias
parent only severs the link, not the original.

## Quick reference

| Question | Answer |
|----------|--------|
| Do shells appear in search results? | The shell *itself* doesn't (it has no items), but its children's items do via [recursive subcollection search](Search-Sort-Filter.md). |
| Do shells appear in launch history? | No — there's nothing to launch. |
| Do shells appear in the Statistics dialog? | The aggregate row sums their descendants. The shell row itself shows zero plays. |
| Do shells survive `.kart` export? | Yes — exporting a shell exports its descendants too. See [Backup & Migration](Backup-and-Migration.md). |
| Can I make a shell the **startup collection**? | Yes — `startupCollection=Video` is valid; Kartend opens that shell on launch and you navigate down with `Enter`. |
