# Manual verification — 2026-06-15 (DAT check-for-updates)

Runtime-gated checks for Kartend-m6qsb.31 — the "Check for updates" UI that
completes .26 (provenance store + capture shipped 2026-06-14). The selection
core (`outdatedAmong`) is unit-tested; the network + UI below need a live run.

## .31 — Check for updates + one-click re-download
Prereq: download at least one DAT via DAT Audit → Download (No-Intro and/or
Redump) so `dat_library_provenance` has rows.

- On the **DAT Library** page a **"Check for updates…"** button appears once a
  library folder is set (hidden otherwise).
- Click it with no provenance rows → info dialog "No downloaded catalogues to
  check yet…" (only Download-page DATs are tracked).
- With provenance rows → confirm prompt "Check your N downloaded catalogue(s)…
  re-download any with a newer version? This may download data." **No** aborts.
- **Yes** → button shows "Checking…" and disables; off-thread it asks each
  source its current revision (No-Intro: daily pack date, cheap; Redump:
  downloads the small DAT to read its header version), and re-downloads any that
  differ. On finish:
  - none outdated → "All N downloaded catalogue(s) are up to date."
  - some outdated → "Updated M catalogue(s) to the latest version." (+ "K could
    not be re-downloaded." if any failed), then the library review opens showing
    the refreshed catalogues.
- After an update, the provenance row's `version` should advance to the new
  revision (re-running the check immediately reports up-to-date). Verify via
  `SELECT canonical_path, version FROM dat_library_provenance;`.
- Caveat to confirm: No-Intro re-download uses the daily form's
  **default-checked sets** (the originally-selected sets aren't stored in
  provenance) — confirm the re-downloaded pack contains the expected sets.
- Closing the window mid-check cancels cleanly (no crash; worker stops).
