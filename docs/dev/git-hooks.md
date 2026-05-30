# Git hooks

Contributor-installed shell hooks that gate commits and pushes
before they hit CI. Lives in
[.scripts/git-hooks/](../../.scripts/git-hooks/); install with
`.scripts/git-hooks/install.sh`.

The hooks are **opt-in** — they aren't auto-installed at clone time
because `.git/hooks/` is per-checkout state, not tracked content.
Running the installer is a one-time setup step (it's also idempotent,
so re-running is harmless).

## Installation

```bash
.scripts/git-hooks/install.sh
```

Output looks like:

```
  pre-commit: symlinked to /path/to/.git/hooks/pre-commit
  pre-push:   symlinked to /path/to/.git/hooks/pre-push
Done. 2 hook(s) active in /path/to/.git/hooks
```

The installer:

- Symlinks each script in `.scripts/git-hooks/` (except itself and
  README) into the active hooks directory.
- Honors `git config core.hooksPath` so it coexists with **beads**
  (`bd hooks install` writes to `.beads/hooks/`). When a hook file
  already carries the `# --- BEGIN BEADS INTEGRATION ---` markers,
  the installer appends a `# --- BEGIN KARTEND PROJECT HOOK v1.0.0 ---`
  section after them rather than overwriting.
- Won't clobber a plain file already at the destination — skip with
  a message rather than risk destroying handcrafted hooks. Remove or
  merge by hand if you want the project version.

## Bypassing (rare)

```bash
git commit --no-verify   # skip pre-commit
git push --no-verify     # skip pre-push
```

Used sparingly. If a hook is wrong, please fix the hook instead.

## `pre-commit`

[.scripts/git-hooks/pre-commit](../../.scripts/git-hooks/pre-commit).
Runs on every `git commit`. Two checks, both fast (under a second
combined):

### 1. `clang-format` on staged `.cpp` / `.h`

Walks the index for staged `Added`/`Copied`/`Modified` files
matching `*.cpp` or `*.h`. Each is run through
`clang-format --style=file --dry-run --Werror`. **Drift fails the
commit.**

If a file has drift, the fix is:

```bash
.scripts/build.sh --maintenance --format-apply
git add <files>
git commit
```

The hook **does not auto-fix** — it just refuses, so you stay
deliberate about what's in the index.

If `clang-format` isn't on PATH the check is skipped with a yellow
warning rather than blocking the commit. The repo expects
`clang-format` v19 — the kartend-ci image symlinks
`clang-format-19` → `clang-format`, and the
[clang-format v19 pin](https://github.com/EtherAura/Kartend) feedback
captures why.

### 2. Version consistency

When `VERSION` exists, compares it against:

- `packaging/PKGBUILD`'s `pkgver=` line
- The `packaging/kartend-<version>.ebuild` filename
- The most-recent `<release version=…>` in
  `packaging/io.github.EtherAura.Kartend.metainfo.xml`

Any mismatch fails the commit. The fix is `.scripts/bump-version.sh
<NEW>` which updates all four in lockstep.

This catches the common "bumped PKGBUILD but forgot metainfo" (or
vice versa) regression.

## `pre-push`

[.scripts/git-hooks/pre-push](../../.scripts/git-hooks/pre-push). Runs
on every `git push`. Three Python lint scripts in
[.scripts/](../../.scripts/), all pure-Python with no Docker / Qt
dependency. Total runtime well under a second.

### 1. `check-layering.py`

Enforces the architectural layering rule:

```
api → chrome → modules → ui → core
```

The foundation layers (`src/utils/`, `src/chrome/`) **must not**
reach upward into `src/modules/`, `src/ui/`, or `src/core/`. The
script walks `#include` directives and fails on any upward
reference.

Common failure modes and fixes are in
[docs/layering.md](layering.md) (when written) — the short version
is: if a util needs to know about a manager, you're really hiding a
manager inside a util. Lift the data needed up to the call site as
a parameter, or push the helper down into the manager.

### 2. `check-singleshot-comments.py`

Every `QTimer::singleShot(...)` call must carry a preceding **`// why`
comment** explaining the deferral. Background:
`QTimer::singleShot(0, ...)` is occasionally used as a
"schedule-for-the-next-event-loop-iteration" trick, but the readers
of that code six months from now have no idea whether the deferral is
load-bearing or accidental. The convention forces a one-liner
explanation right above each call.

### 3. `check-test-mapping.py`

Every `src/modules/<feature>/` directory must have a matching
`tests/modules/<feature>/` (with at least one `test_*.cpp`). Catches
the common "added a manager, didn't add a test" regression early.

The check is structural — it doesn't enforce coverage or even that
the tests actually exercise the manager. It just makes sure the
**directory exists**. Skeleton tests are fine; the gate is "I
thought about testing this."

## Adding a new hook

1. Drop a new executable script in `.scripts/git-hooks/` named after
   the standard git hook stage (`pre-rebase`, `prepare-commit-msg`,
   etc. — see `git help hooks` for the full list).
2. Make sure it `cd`s to the repo root (`cd "$(git rev-parse
   --show-toplevel)"`) so it runs identically regardless of where
   the user invoked git.
3. Re-run `.scripts/git-hooks/install.sh`. Existing contributors
   pick up the new hook when they next re-run the installer; CI
   doesn't run these hooks, so CI behavior is unaffected.

Don't add slow checks to `pre-commit` (sub-second budget) or
medium-slow checks to `pre-push` (couple-of-second budget) — those
gates fire often. Long-running checks belong in `.scripts/ci-local.sh`
or CI itself.

## Related code

| Concern | File |
|---------|------|
| Installer (with beads coexistence) | [.scripts/git-hooks/install.sh](../../.scripts/git-hooks/install.sh) |
| Pre-commit (clang-format + version) | [.scripts/git-hooks/pre-commit](../../.scripts/git-hooks/pre-commit) |
| Pre-push (three Python lints) | [.scripts/git-hooks/pre-push](../../.scripts/git-hooks/pre-push) |
| Layering lint | [.scripts/check-layering.py](../../.scripts/check-layering.py) |
| `singleShot` comment lint | [.scripts/check-singleshot-comments.py](../../.scripts/check-singleshot-comments.py) |
| Test-mapping lint | [.scripts/check-test-mapping.py](../../.scripts/check-test-mapping.py) |
| Version bumper | [.scripts/bump-version.sh](../../.scripts/bump-version.sh) |
