# Developer Debugging Guide

This doc collects the recipes that come up when diagnosing a bug in
Kartend — logging filters, sanitizer triage, debuggers, and the
Qt-specific patterns that tend to bite.

## Q_LOGGING_CATEGORY filters

Every manager and most subsystems declare a `Q_LOGGING_CATEGORY` so
output can be enabled per-area without flooding stderr. Most categories
default to `QtWarningMsg` — warnings and above are always on, debug
and info levels are off unless you enable them via `QT_LOGGING_RULES`.

```bash
# Enable everything for one category (rebuilds aren't required).
QT_LOGGING_RULES="kartend.scrollmanager.debug=true" kartend

# Multiple categories, semicolon-separated:
QT_LOGGING_RULES="kartend.scrollmanager.debug=true;kartend.artworkmanager.debug=true" kartend

# Mute one category (e.g. silence the noisy scrape-timings during scrape
# debugging by enabling everything else):
QT_LOGGING_RULES="kartend.*=true;kartend.scrape.timings=false" kartend
```

### High-value categories by symptom

| Symptom | Category | Notes |
|---|---|---|
| Scrolling jank, missed paints | `kartend.scrollmanager`, `kartend.artworkmanager` | Pair with `KARTEND_PERF_TRACE=1` for `applyResultsToUi` timing. |
| Search returns wrong items | `kartend.search.diag`, `kartend.scrape.flow` | Trace what filter actually went to the worker. |
| Database scan stuck / slow | `kartend.databasemanager`, `kartend.querymanager`, `kartend.scan.flow` | Look for `PROGRESS_REPORT_INTERVAL` logs and watch for `commit` cadence. |
| Scraper failing silently | `kartend.scrape`, `kartend.scrape.timings`, `kartend.scraper.http` | `lcScraperHttp` logs the SSL config on first request — empty CA bundle is a common Docker pitfall. |
| Selection restore wrong / lost | `kartend.selectionmanager`, `kartend.selectionrestoremanager` | Both default to `QtWarningMsg`. |
| Attract mode misbehaving | `kartend.attractmanager`, `kartend.interactionmanager` | Attract suppresses itself on selection changes — turn both on to see the dance. |
| Settings load / save issues | `kartend.settingsmanager` | Includes the keychain code path. |

The complete list of categories: `git grep "Q_LOGGING_CATEGORY(lc"` in
`src/`.

### Performance trace env var

`KARTEND_PERF_TRACE=1` enables a small set of `lcPerfTrace` lines in
hot paths (notably `ArtworkManager::applyResultsToUi`). The trace
includes per-batch `totalMs / applied / skipped / processed / size`
numbers so you can see whether a stall comes from large batches or many
small ones in quick succession.

## Sanitizer triage

The build script's `--sanitize` flag enables ASan+UBSan (Debug only).
A separate `--tsan`-equivalent path is available via the CI workflow
(`KARTEND_ENABLE_TSAN=ON`); the two sanitizer runtimes can't coexist.

```bash
.scripts/build.sh --sanitize --tests --run-tests   # ASan + UBSan
```

### Reading ASan output

A typical ASan report has three blocks:

1. The error header (`heap-use-after-free`, `stack-buffer-overflow`,
   etc.) plus the address that was accessed wrong.
2. A "READ/WRITE of size N at 0x…" stack — the bad access. Look for
   the topmost frame in `src/` (skip Qt internals via the
   `__interceptor_*` lines).
3. The "freed by thread T1 here" or "previously allocated by thread"
   stack — where the memory's lifetime ended.

If the bottom stack ends in `~QObject` and the top stack is in a slot
or lambda invocation, you have a Qt lifetime bug — the QObject was
deleted while a queued signal was still in flight. The fix pattern is
usually adding a `QObject*` context as the connect()'s 3rd arg so the
connection auto-disconnects on destruction (see Kartend's hard rule on
this), or using `QPointer<T>` to guard.

`.lsan_suppressions.txt` covers known shutdown leaks (intentional
`std::quick_exit(0)` exits skip many destructors). If a new leak
appears in CI, *first* check whether it's the same pattern; if so, add
to the suppression file with a comment explaining why.

### Reading TSan output

TSan reports a *race* — two unsynchronised accesses to the same memory
where at least one is a write. The output prints both call stacks.

Typical Qt patterns and fixes:

| Pattern | Fix |
|---|---|
| Worker thread writes a member, main thread reads it | Route the access through a signal/slot pair (worker emits, main thread slot caches) or a `std::atomic`. |
| Two queued signals fire in different orders than expected | Bump the receiving manager's generation counter on each request and drop stale completions (see `CachedCountsService`). |
| Static / global accessed from multiple threads | Wrap in `std::call_once` for init, or move to a thread-local. |

`halt_on_error=1` in `TSAN_OPTIONS` makes the first race fail the
suite — keep it on for CI but consider turning it off locally when
you're debugging multiple races at once.

### UBSan reports

Most UBSan hits in this codebase are signed-overflow or null-pointer
arithmetic. The report tells you the exact source line. Fix at the
arithmetic site, not at the surrounding logic.

## Attaching a debugger

GDB or LLDB both work; LLDB tends to be friendlier with Qt's
`Q_DECLARE_METATYPE` types because LLDB pretty-printers come pre-loaded
with Qt support on most distros.

```bash
# GDB to a running Kartend
gdb -p $(pgrep -f kartend)

# LLDB equivalent
lldb -p $(pgrep -f kartend)

# Or launch under the debugger from scratch
.scripts/build.sh --debug --tests
gdb --args build/ninja-debug/kartend
```

For coredumps, `coredumpctl gdb <PID>` is the simplest path — it streams
the core to GDB without materialising the full coredump file. **Don't
write the coredump to `/tmp`**; Kartend coredumps decompress to ~2 GB
and will fill a tmpfs.

### Useful Qt-aware breakpoints

```
# Break on every QObject destruction (verbose; only use with a narrow
# rerun)
b QObject::~QObject

# Break on signal emission for a specific class (substitute the moc-
# generated index, see moc_xxx.cpp output)
b QMetaMethod::invoke

# Break on QObject::deleteLater scheduling
b QObject::deleteLater
```

## Valgrind / heaptrack

Valgrind is too slow for the full app on this codebase, but it's useful
for targeted unit tests:

```bash
.scripts/build.sh --debug --tests
valgrind --leak-check=full --show-leak-kinds=definite \
  build/ninja-debug/tests/test_pathutils
```

Heaptrack gives a more practical view of overall heap usage:

```bash
heaptrack build/ninja-debug/kartend
heaptrack_gui heaptrack.kartend.<pid>.zst
```

Heaptrack overhead is ~2× — usable interactively, unlike Valgrind's
~20×.

## Qt-specific crash patterns

- **QObject deletion order**: a child QObject can be deleted by Qt's
  parenting machinery before its non-QObject owner. If you store a raw
  pointer to a child, switch to `QPointer<T>` so dereferences are
  guarded. The codebase's recent timer-field migration is an example.
- **Signal/slot type mismatch**: with the new connection syntax
  (function pointer), type mismatches are compile errors. If you see a
  runtime warning *"No such signal …"*, you're on the old string-based
  syntax — port it.
- **moveToThread + child QObject**: a QObject moved to a worker thread
  must not have a parent on the main thread. The codebase's
  DatabaseManager / QueryManager pair handles this by parenting
  QueryManager to nullptr and managing its lifetime explicitly.
- **Direct/Queued connection surprise**: `Qt::AutoConnection` is direct
  when sender and receiver are on the same thread, queued otherwise.
  If a slot starts firing on an unexpected thread, check the connect
  type — same-thread refactors can flip it silently. The codebase
  prefers explicit `Qt::DirectConnection` / `Qt::QueuedConnection`
  annotations on cross-thread or cross-subsystem boundaries (see
  `QueryManager::QueryManager`).

## The QTimer::singleShot "why" rule

Every `QTimer::singleShot` call in this codebase must be preceded by a
comment explaining *why* the delay exists. `--maintenance` enforces this
via `.scripts/check-singleshot-comments.py` and CI fails the PR if a
fresh `singleShot` lands without one.

A good "why" comment names the specific concern the delay addresses:

```cpp
// Defer to the next event-loop tick so the current paint completes
// before we mutate widget geometry — without this, the resize observed
// during paint races with the layout pass and the artwork briefly
// renders at the wrong size.
QTimer::singleShot(0, this, [this]() { applyNewArtworkSize(); });
```

Things to **avoid** in a "why":

- "Delay update" (says nothing).
- "Work around a Qt bug" without naming the bug or the bug-report URL.
- Restating the delay constant (`50ms because that's the timer`).

If you can't name a concrete concern, you probably don't need the
singleShot — call the function directly.

## See also

- [Architecture](architecture.md) — module layout, signal flow,
  ownership model
- [Building](building.md) — build modes including `--sanitize` and
  PGO
- [Testing](testing.md) — test harness layout
- [CONTRIBUTING.md](../CONTRIBUTING.md) — submission protocol, code
  style, pre-commit hook, the quarterly lint review
