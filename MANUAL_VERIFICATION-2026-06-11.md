# Manual verification checklist — audit-fixes-2026-06-10

Runtime-gated items from the 2026-06-10/11 audit wave that only a human at
the live app can verify. Intentionally **untracked** — work through it, then
delete. Each item lists the bd issue for the full context.

Build to test: `build/ninja-debug/kartend` from the branch tip (working tree
includes the four uncommitted refactors + kt39d).

## Input / launch

- [ ] **Gamepad hot-unplug (Kartend-tjww5, SDL2 backend)** — pair a BT
  controller, navigate, power it off, power it back on. Input must resume
  within the idle poll interval. Also: hold a d-pad direction and kill the
  pad mid-hold — scrolling must stop, not run away.
- [ ] **Launch debounce (Kartend-l06g6)** — double-tap Enter / gamepad
  Confirm fast on an item: exactly one child process. ~~Press Enter with no
  selection: nothing launches.~~ ✅ no-selection half verified 2026-06-11.
- [ ] **Archive launch UX (Kartend-mkcak / ijglg)** — launch a large
  archive-backed item on a cold cache: UI stays responsive, "Extracting
  Archive" overlay shows, app remains interactive. (Cancel binding is a
  noted follow-up — overlay shows but cancel input may not be wired.)
- [ ] **Play stats on failed launcher (Kartend-yu1e5)** — point a
  collection's launcher at a non-executable path, launch: error dialog, and
  the item's play count must NOT increment.

## Search / scroll

- [x] **Search-clear race (Kartend-8uoe1)** — ✅ verified 2026-06-11
  (rapid type+Escape ×10 on 7k-item collection, no snap-back).
- [x] **Scroll smoothness (Kartend-urrpp)** — ✅ verified 2026-06-11
  (fast out-and-back scroll, no re-materialization hitches).
- [x] **Cover flow on large collections (Kartend-x7bn8)** — ✅ verified
  2026-06-11 on 7,091-item collection — AFTER fixing regression
  Kartend-6x8tn (center cards permanently blank on normal collections;
  cache-only resolve lost its per-chunk-rebuild self-heal). Fix adds a
  pending-card retry pass + targeted prewarm; artwork fills within ~1 s.

## Settings / credentials

- [ ] **Keychain demotion banner (Kartend-ztc64)** — simulate a keyring
  outage (e.g. `kill` kwalletd / lock the wallet), save a ScreenScraper
  password: warning banner appears on both credential panels with the
  reason; restart the app — banner persists; restore the keyring, save any
  setting — banner clears and the INI no longer holds the plaintext value.

## Shutdown / stability (spot checks)

- [ ] Quit immediately after launching an item — play count + history row
  survive next start (Kartend-juvb7).
- [ ] Quit during a large scan — clean exit, no multi-second hang, no
  abort (Kartend-t2my8 / kfnv7 / 8mx2q).
- [ ] Startup video + splash both enabled — video plays on top, splash
  follows after skip/end (Kartend-1ha73 overlay registration).
