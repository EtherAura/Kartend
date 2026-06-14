# Manual verification — DAT manager v2 (branch `dat-manager-v2-2026-06-12`)

Epic Kartend-m6qsb. All unit-testable behavior is pinned (suites green, 216
tests); the items below are the runtime-gated GUI/concurrency flows Claude
cannot validate headlessly.

## 1. Collection link is live (Kartend-m6qsb.2)
- Link a collection to a profile (DAT Audit → profile editor → Linked
  collection), save.
- In the collection's settings page, add/remove a DAT file and change the
  media dir. Reopen the audit for that collection → DAT list + scan folder
  reflect the settings page (including unsaved edits), and both lists are
  locked with the "managed by the linked collection" hint.
- Unlink in the profile editor → lists become editable again.

## 2. Persisted results (Kartend-m6qsb.8)
- Run an audit on a linked, saved profile. Close the audit window.
- Collection settings → "Last audited" shows a relative time plus
  "X present · Y missing". Delete one audited file, re-run, reopen settings →
  counts shift.
- Cancelled audits must NOT update the label (cancel mid-run, check).

## 3. Layout detection (Kartend-m6qsb.6)
- Open the audit for a collection whose folder is one-archive-per-item → a
  banner suggests "Archive-per-item layout detected" (tooltip shows the
  sampled evidence). Apply → banner gone; reopen → no banner (confirmed).
- "Detect structure" button re-probes on demand.
- Confirm Flat on a flat folder with a stray subfolder → Run audit → files in
  the subfolder are not scanned (totals shrink accordingly).

## 4. Archive-aware audit (Kartend-m6qsb.7)
- With archive-per-item confirmed, Run audit on a zip set whose catalogue is
  attached → members classify (Have/WrongName/Missing) instead of ~100%
  Unknown + Missing.
- Progress bar shows real counts (not indeterminate) once hashing starts;
  Cancel aborts promptly mid-extraction.
- Second run is much faster (member hash cache; verify after touching one
  archive only that one re-extracts).
- Fix… on archive-member rows: actions report "source missing" and touch
  nothing (container fixing is backlog Kartend-m6qsb.14).

## 5. DAT library (Kartend-m6qsb.5)
- DAT Audit → Library… → Browse to a folder of catalogues, Rescan → catalogues
  whose header name resembles a collection name appear with a candidate
  picker; already-attached ones do not.
- Attach selected → collection settings shows the DAT path; a linked profile's
  audit list shows it immediately.
- Dismiss selected → Rescan shows nothing for it; `touch` the file → proposes
  again.
- Restart the app with the library configured and a new matching DAT present →
  ~5 s after the window paints, status bar shows the "%n new DAT catalogue(s)"
  hint. Verify the startup scan does not hitch the Sunshine encode.

## 6. Regression sweeps
- New Profile → save with "(none)" collection → must save cleanly (this used
  to fail silently: null-QString bind bug, fixed in Kartend-m6qsb.1).
- Rename a collection (settings) → its linked profile follows (was orphaned
  before the migrateCollectionUuid fix).
- DAT list tooltips show "name (version) — N entries" after a DAT has been
  used at least once.
