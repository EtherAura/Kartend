# DAT Audit

The **DAT Audit** window checks your files against one or more **DAT
catalogues** — community-maintained lists that map a file's content hash to its
canonical name — and reports what you have, what's misnamed, what's unrecognised,
and what's missing. It's the verification companion to the
[Scraper](Scraper.md): the scraper *fetches metadata*, the audit *verifies your
files*.

DAT catalogues exist for many curated media sets (reference archives, audio
collections, video libraries, and more). If you have a `.dat` / `.xml` catalogue
for your collection, the audit tells you at a glance how complete and how
correctly-named it is.

Open it from **File → DAT Audit…**, or from a collection's settings page
(**Audit collection…**) to start pre-aimed at that collection. The window has a
sidebar with three sections — **Audit**, **DAT Library**, and **Download** —
matching the rest of the app's look.

## Running an audit

1. **Add DAT files** — click *Add DAT…* and pick one or more `.dat` / `.xml`
   catalogues. Hover an entry to see what the catalogue calls itself and how
   many entries it carries. A catalogue shipped as a single `.dat` zipped inside
   a `.zip` is read transparently — point Kartend straight at the `.zip` (an
   archive tool such as `unzip`/`7z` must be on your PATH).
2. **Add scan folders** — click *Add folder…* and pick the folder(s) holding the
   files to check.
3. Click **Run audit**. Hashing runs in the background with a live progress
   bar; click **Cancel** to stop.

When it finishes, the summary line shows the totals and the table lists every
file and every catalogue entry with a status:

| Status | What it means | What to do |
|--------|---------------|------------|
| **Have** | Correct file, correct name | Nothing |
| **Wrong name** | Right content, wrong filename | Rename to the canonical name |
| **Wrong content** | Right name, wrong/old content | Re-acquire a good copy |
| **Duplicate** | A second copy of something you already have | Optional cleanup |
| **Unknown** | Not in any catalogue | Identify or set aside |
| **Corrupt** | The file could not be read or extracted | Re-acquire it |
| **Missing** | In the catalogue, not in your folder | Acquire it |

Use the **Show** filter to focus on one status (e.g. just *Missing*, or just
*Wrong name*).

## Profiles

A **profile** saves an audit setup — DAT files, scan folders, and the options
below — so a re-check is one click. Manage them with the *Profile* row at the
top of the window (New / Edit / Duplicate / Rename / Delete). The profile
editor also holds:

- **1G1R region priority** — when a catalogue lists the same title for several
  regions, count only your preferred region toward completeness.
- **Ignore globs** — filename patterns (one per line, e.g. `*.txt`) excluded
  from the scan.
- **Fix output mode** — see *Fixing problems* below.

## Linking a collection

A profile can be **linked to a collection** (the *Linked collection* picker in
the profile editor). For a linked profile, the collection's own settings are
the single source of truth: the DAT list and scan folder are taken live from
the collection's configuration page, and the editors in the audit window lock
with a hint saying so. Edit the DAT list on the collection's settings page;
the audit picks the change up automatically.

After a completed audit of a linked (saved) profile, the results persist: the
collection's settings page shows **Last audited** with the present / missing
counts, without re-running anything.

## The DAT library

Instead of attaching catalogues one by one, point Kartend at a **DAT library**
— a folder where you keep your catalogue files. Set it on the **DAT Library**
page (**Scan & review proposals…**).

- At startup, Kartend quietly checks the library for new or updated
  catalogues. When one looks like it belongs to one of your collections (its
  header name resembles the collection's name), a status-bar hint appears.
- The **Library review** lists each proposed match with a per-row picker. Each
  picker offers, in addition to the candidate collections:
  - **(No collection — keep in library)** — leave the catalogue in the library
    folder, unattached. It isn't dismissed, so a later scan can propose it again.
  - **Add to new collection…** — create a brand-new collection (you choose the
    name and content folder) with this catalogue already attached.
- **Apply selected** carries out the chosen action for the selected rows;
  **Dismiss selected** stops Kartend from asking about that catalogue again —
  until the file itself is updated, which counts as a new revision worth asking
  about.
- Nothing is ever attached automatically, and **associating is optional** —
  you can import catalogues into the library without attaching them to anything.

## Downloading catalogues

The **Download** page fetches catalogue packs from an online service and
imports them straight into your DAT library. Pick a **Source** at the top:

- **No-Intro** — enter the service's **system id** (or paste a daily-download
  URL) and click **Load**; Kartend shows the available pack date and the sets
  you can include, each with its real name. Tick the sets you want.
- **Redump** — pick a system by name from the dropdown (the list loads the
  first time you choose this source; **Refresh list** reloads it).

Then click **Download & import**: the pack downloads in the background (progress
bar + **Cancel**), and every catalogue inside it is extracted into your DAT
library. When the import finishes, the library review opens so you can attach
the new catalogues to your collections — or leave them in the library, or spin
up a new collection for one. Importing never forces you to associate anything.

If you haven't set a library folder yet, Kartend asks for one the first time you
download. Downloads run one at a time and report clearly if the service is
unreachable or returns something unexpected.

### Importing from any source

Not every cataloguer offers an in-app download. For those — and for packs you
already have — the **DAT Library** page has **Import DAT zip…** and **Import DAT
folder…**: point Kartend at a downloaded DAT archive, a folder of DATs, or a
single `.dat`, and it copies them into the library and runs the match review,
exactly like a download. Any Logiqx-style catalogue works.

## Folder structure detection

Folders are organised in different ways — all files directly in one folder,
spread across subfolders, or one archive per item. The audit can detect which
shape a scan folder has: click **Detect structure**, or let the automatic
probe run when you open the audit for a collection. The result appears as a
banner suggestion; **Apply** confirms it, and only a confirmed layout changes
how the audit scans:

- **Flat** — subfolders are skipped (they're noise, e.g. artwork or notes).
- **Nested** — everything is scanned recursively (the default behaviour).
- **Archive-per-item** — the audit opens each archive and checks the files
  *inside* it against the catalogue. Without this, an archived set would
  report nearly everything as *Unknown* and *Missing*, because the archive's
  outer bytes never match the catalogue's content hashes.
- **Subfolder-per-item** — each item lives in its own subfolder as a multi-file
  set (e.g. multi-part masters or disc-image pairs). Scanned recursively like
  *Nested*; the distinct label just reflects the shape the probe found.

The first audit of a large archive set extracts and hashes every member, which
takes a while — the progress bar shows real counts, and the results are cached
so the next audit only re-reads archives that changed.

## Fixing problems

**Fix…** opens the fix dialog over the current results:

- **Rename** wrong-named files to their canonical names, in place.
- **Relocate** present entries into a clean managed-output folder (copies;
  originals untouched).
- **Quarantine** unknown files into a side folder (moved, never deleted).

Every action is previewed first and undoable afterwards. Files inside
archives are reported but not modified — fixing rewrites loose files only.

When a fix **renames** files in a collection-linked audit, Kartend offers to
**re-scrape** that collection afterwards: a canonical filename matches metadata
far more reliably than a wrong one, so renamed items are exactly the ones worth
re-fetching. It's a prompt, not automatic — say no to skip it.

## Exporting results

- **Export CSV** — the full table as a spreadsheet, one row per entry with a
  Status column you can sort/filter.
- **Export fixdat** — a DAT file containing *only the missing entries*. Hand it
  to any DAT tool to fetch exactly the gaps.
- **Export miss list** — a plain-text list of the missing titles.

## Notes

- Auditing matches on **content hash**, so a correctly-downloaded but
  wrongly-named file is still recognised (and flagged *Wrong name*).
- A first audit hashes every file, which can take a while for large media; the
  progress bar shows how far along it is, and unchanged files are skipped on
  re-audits.
- The DAT files themselves are never reported, even if they sit inside a scan
  folder.
