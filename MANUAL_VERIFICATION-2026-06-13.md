# Manual verification — DAT manager KDE window + No-Intro download (2026-06-13)

Branch `dat-manager-v2-2026-06-12`. Epic Kartend-m6qsb, issues .16 (download)
and .17 (KDE-style window). Both are runtime-gated GUI/network work; the
testable cores (HTML parsing, pack extraction) are unit-pinned (NoIntroParse,
NoIntroDownloader suites green). The checks below need the running app.

## 1. KDE-style window (Kartend-m6qsb.17)
- File → DAT Audit… opens a window with a left nav rail (Audit / DAT Library /
  Download), a context header (icon + bold title) that tracks the selected
  page, and Breeze-native styling consistent with Settings.
- Audit page: all prior behavior intact — profile combo + New/Edit/Duplicate/
  Rename/Delete, DAT + scan-folder lists, Detect structure banner, Run/Cancel +
  progress, results table + View filter, Fix…, the three exports.
- Resize the window and drag the nav splitter; close and reopen → geometry +
  splitter position restored (QSettings "kartend"/"ui-state"/DatManagerWindow).
- Launch from a collection's settings (Audit collection…) → opens on the Audit
  page seeded/derived for that collection; linked-profile DAT/scan lists are
  locked with the hint (unchanged from before).

## 2. DAT Library page (Kartend-m6qsb.5 surface)
- DAT Library page shows the configured library folder (or "No library folder
  set.") and a "Scan & review proposals…" button that opens the review (same
  confirm-only flow as before). Button disabled when no controller hook (tests/
  standalone) — won't happen in the real app.

## 3. No-Intro download (Kartend-m6qsb.16)
Network + anti-bot-guarded; verify against the live site:
- Download page → enter a DAT-o-MATIC system id (e.g. 64) OR paste a daily
  URL like `…?page=download&op=daily&s=64&combo=…` → **Load**. Pack date +
  set checkboxes + DAT-type appear.
- **Download & import** → progress shows Requesting/Preparing/Downloading; the
  pack downloads, DATs extract into the library, status shows "Imported N DAT
  file(s)…", and the library review opens with proposals.
- If no library folder is set, the first download prompts for one and saves it.
- **Cancel** mid-download aborts promptly (no hung worker, partial zip in a
  temp dir is discarded — nothing lands in the library).
- Error paths surface clearly (don't hang / don't crash): bad system id;
  unreachable site; if the site changes its form or rate-limits, the status
  shows the "unexpected page / try again" message rather than silent failure.
- Sunshine: the download + extract run off the UI thread; confirm they don't
  hitch the live desktop encode. The startup library scan is unchanged.

## 4. Optional association in the library review (Kartend-m6qsb.18)
- After a download/import (or Scan & review), each proposal row's dropdown
  offers **(No collection — keep in library)** and **Add to new collection…**
  ahead of the matched collections; the best match is preselected.
- Pick **(No collection)** + **Apply selected** → the row clears, nothing is
  attached, the DAT stays in the library, and a later scan re-proposes it (it
  is NOT dismissed). Headless-pinned in test_datlibraryreviewdialog.
- Pick **Add to new collection…** + **Apply** → CreateCollectionDialog opens
  (intro names the catalogue) with a **Parent Collection** picker ('(none)' +
  existing collections); on accept a new collection is created with the DAT
  attached, nested under the chosen parent (inherits its layout/sidebar) or
  top-level for '(none)', and appears in the main UI; cancel leaves the row.
  (The normal Settings → Add collection dialog is unchanged — no parent row.)
- Pick a collection + **Apply** → attaches as before.

## 5. Re-scrape after rename (Kartend-m6qsb.27)
- Open the audit for a collection that has wrong-named files; **Fix… → Rename
  → Apply**. After the re-audit, a prompt offers "N file(s) were renamed…
  Re-scrape this collection now?". **Yes** opens the scraper scoped to that
  collection (FillMissing picks up the renamed, now-metadata-less items); **No**
  skips. The prompt only appears for a collection-linked audit with renames.
- Note: the collection should be rescanned (filesystem watcher, or manually) so
  the renamed paths are in the items DB before scraping — verify the renamed
  files actually get scraped.

## Notes / scope
- The download mimics the site's daily form flow (verified 2026-06-13:
  GET daily → POST sets+Request → 302 → GET manager → POST token → zip). It is
  inherently brittle: if DAT-o-MATIC changes its pages it will stop working and
  report an error — re-capture the flow and update NoIntroParse. The brittle
  parsing is isolated + unit-tested so a fixture refresh is the only change.
- v1 takes a numeric system id / URL (no built-in id↔name list). A future
  enhancement could fetch the systems list to offer names.
