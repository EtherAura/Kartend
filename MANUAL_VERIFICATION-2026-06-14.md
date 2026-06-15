# Manual verification — 2026-06-14 (DAT P4 backlog)

Runtime-gated checks for the P4 DAT-manager work done 2026-06-14. The engine /
parsing / persistence changes are all covered by unit tests; these are the
GUI-surfaced bits that need eyes on a running app.

## .15 — per-DAT completeness breakdown (tooltip)
- Build an audit profile whose catalogue is fed by **two or more** DAT files,
  run the audit, then hover the **completeness bar**. The tooltip should list
  each DAT as `Name.dat: N / total present` under the overall line. With a
  single DAT, only the overall line shows (no per-DAT breakdown).

## .12 — subfolder-per-item layout banner
- Point a profile's scan root at a folder where each item is its own subfolder
  holding multiple files (e.g. `Item A/{part1,part2}`, `Item B/{…}`). The
  layout banner should read **"Subfolder-per-item layout detected — subfolders
  will be scanned."** Apply → the audit still scans recursively and classifies
  by content hash (functionally identical to Nested; the label is the change).

## .14 — fix dialog: profile seeding + per-item subfolders
- Open an audit whose profile has **fix mode = managed output** with a
  managed-output root set. **Fix…** → the "Copy present files into a clean
  sorted folder" box should be **pre-checked** and the folder **pre-filled**
  from the profile (previously always blank).
- Tick **"…into a subfolder per item"** (enabled only when the relocate box is
  on). Apply → each present file is copied to
  `<root>/<game name>/<canonical name>`; multi-file sets for one game land in
  the same folder. Untick → files land flat in `<root>/<canonical name>`.
- Confirm a game name with an illegal char (e.g. a ':') becomes a safe folder
  name (':' → '_'); a row with no game name still copies flat under the root.

## .10 — live filesystem watching of the DAT library
- With a DAT-library folder configured (Settings → scraper, or via DAT Audit →
  Library), **drop a new .dat into that folder** while the app is running. After
  ~1.5s (debounce) a status-bar hint should appear: "N new DAT catalogue(s)
  match your collections…" — without any manual rescan.
- **Doesn't nag:** an unrelated change in the folder (e.g. touch a non-matching
  file, or re-trigger after the hint already showed) must NOT re-pop the same
  hint. Only a genuinely new/changed matching catalogue re-announces.
- Copying **several** DATs at once should coalesce into a single hint (debounce),
  not one per file.
- **Follows the folder:** change the library path (DAT Audit → Library → set a
  new folder); dropping a DAT into the *new* folder is now detected and the old
  one is no longer watched.
- Known limitation: watching covers the root + one level of subfolders; DATs
  added deep in a nested tree are picked up by the next startup / manual rescan.

## .26 — provenance capture (foundation; check-for-updates UI pending)
- Download a DAT via DAT Audit → Download (No-Intro or Redump). A row should be
  written to the `dat_library_provenance` table (schema v21) in the app DB:
  `source` = nointro/redump, `system_id`/`slug` set, `version` = the No-Intro
  pack date or the Redump DAT header version. Verify by opening the app sqlite
  DB and `SELECT * FROM dat_library_provenance;` after a download.
- Note: this is the foundation only. The user-facing "Check for updates"
  detection + one-click re-download (which consumes this provenance) is the
  remaining Layer 3 of Kartend-m6qsb.26 — not yet wired to a button.
