# Sanitizer suppressions policy

Two suppression files at the repo root:

- [tests/suppressions/tsan.txt](../../tests/suppressions/tsan.txt) — ThreadSanitizer
- [tests/suppressions/lsan.txt](../../tests/suppressions/lsan.txt) — LeakSanitizer

These get loaded automatically by the sanitizer-build configurations
(via `TSAN_OPTIONS=halt_on_error=1:suppressions=…` and equivalent for
LSan). Anything matching a suppression line is silently filtered out
of the report.

> Suppressions are **always a workaround**, not a fix. They hide a
> sanitizer report; the underlying issue is still there. Add them
> sparingly, with a clear comment naming the third party / Qt
> internal involved, and aim to remove them when the upstream
> changes.

## When to add a suppression

Add one only when **all** of:

1. The diagnostic is a **false positive** or comes from **third-
   party / Qt internal** code we can't fix.
2. You've spent at least 15 minutes confirming this — `addr2line`
   the stack, read the Qt source, check whether a small lock or
   memory-ordering change in our code would resolve it.
3. The bug is in our code and we plan to fix it, but the fix is
   non-trivial and the rest of CI shouldn't be blocked by it. In
   this case the suppression entry is a **temporary brace** with a
   linked issue ticket and a removal plan.

If it's our bug and the fix is small — **fix it**, don't suppress
it. Suppressed bugs rot. The CI run that shipped your suppression
isn't the run that'll find the next variant of the same bug.

## Suppression syntax

### TSan ([tests/suppressions/tsan.txt](../../tests/suppressions/tsan.txt))

```
# Comment explaining the cause and why it's safe to suppress.
race:<symbol-substring>
called_from_lib:<library-name>
deadlock:<symbol-substring>
mutex:<symbol-substring>
thread:<symbol-substring>
signal:<symbol-substring>
```

`race:` matches *any frame* in the race-pair stack. Most entries
use this.

`called_from_lib:` matches when the call originates from the named
shared library — used for opaque-library cases (glib, pulseaudio)
where the upstream uses sync primitives TSan can't observe.

### LSan ([tests/suppressions/lsan.txt](../../tests/suppressions/lsan.txt))

```
# Comment explaining the leak and why it's intentional.
leak:<symbol-substring>
```

LSan suppressions match any frame in the leaked allocation's stack.

## What every entry must include

Every suppression entry is preceded by **at least three lines** of
comment:

```
# Component or context (which subsystem)
# What the diagnostic looks like and why it's safe to suppress
# (link to upstream issue / commit if applicable)
race:QtPrivate::QSlotObject
```

The current
[tests/suppressions/tsan.txt](../../tests/suppressions/tsan.txt) and
[tests/suppressions/lsan.txt](../../tests/suppressions/lsan.txt) follow this
pattern — copy the style.

## Review criteria

Code review for a PR adding a suppression should check:

- [ ] Does the comment name the third party / Qt internal?
- [ ] Is there a linked upstream bug / commit when the issue is
      known to be in third-party code?
- [ ] If the issue is in our code with a "fix planned" justification,
      does the comment carry a bd issue id or `TODO: remove after X`?
- [ ] Does the suppression match the *narrowest* signature that
      catches the diagnostic? Don't suppress `race:Qt` — suppress
      `race:QtPrivate::QSlotObject` or the equivalent.
- [ ] Have we tried the obvious fix first (a `QMutexLocker`, a
      `std::atomic_thread_fence`, a copy-then-emit pattern)?

A suppression PR is fine to small. Most additions are 5–15 lines
total; reviewers should be able to read every word.

## Periodic cleanup

A useful semi-regular pass: pick one suppression, comment it out
locally, run the sanitizer build, see whether it still fires. Many
suppressions outlive the upstream issue they targeted — Qt and
glib both fix things eventually. If a suppression no longer
catches anything, delete it (with the same care as adding one —
note in the PR description that you confirmed it's no longer
needed).

The repo doesn't have a scheduled cleanup task today, but a
sensible cadence is "every major Qt version bump and / or every
Ubuntu LTS jump." Both are events likely to invalidate upstream-
internal suppressions.

## Common patterns currently in the files

| Pattern | Why | bd issue |
|---------|-----|----------|
| `race:QtPrivate::QSlotObject` | Qt's `BlockingQueuedConnection` heap-allocates slot objects; sync is inside Qt's internal `QMetaObject` mutex which TSan can't observe | (tracked in `tests/suppressions/tsan.txt`) |
| `called_from_lib:libglib-2.0.so` | glib uses an eventfd wakeup pattern with internal mutexes TSan doesn't see | (tracked in `tests/suppressions/tsan.txt`) |
| `race:libpulsecommon` | PulseAudio's threaded-ml thread uses its own sync primitives | (tracked in `tests/suppressions/tsan.txt`) |
| `leak:QThreadPool::QThreadPool` | CacheDiskStorage's worker pool fallback path when long I/O ignores cancel — covered by a bounded timeout | Kartend-pa5a |
| `leak:ScanWorkController`, `leak:ScanService::ScanService` | ScanWorkController abandons its pool at exit; `~QThreadPool()` would block DatabaseManager teardown | Kartend-dw0j |
| `leak:gst_device_monitor_new` + 3 GStreamer entries | Qt6 GStreamer multimedia plugin caches; upstream-Qt issue. Re-evaluate when CI bumps Qt to >= 6.10 | Kartend-l52j |

## TSan suppression audit (Kartend-3hjs.2, 2026-05-27)

`tests/suppressions/tsan.txt` groups its 26 entries into three logical
clusters, each tracked by a closed-as-suppressed bd:

- **[Kartend-feqz](https://github.com/EtherAura/Kartend)** — Qt internal
  event-queue mutex (10 entries: QSlotObject, deleteLater /
  sendPostedEvents, QMetaCallEvent + the QArrayDataPointer derefs that
  fall out, QMetaType::create, invokeMethod, and the QNAM →
  main-thread QByteArray cluster). Qt synchronises queued post→consume
  through its event-queue QMutex but the fast path is futex/atomic —
  not TSan-interceptable. Re-evaluate on the Qt bump that exposes
  QMutex atomics as TSan-visible.
- **[Kartend-8fh5](https://github.com/EtherAura/Kartend)** — Qt thread
  lifecycle (13 entries: QueryManager + shared_ptr control block
  teardown, process-exit ApplicationManager QWaitCondition tear-down,
  QtTest qRun watchdog, Qt COW (QArrayData::allocate /
  reallocateUnaligned), CacheManager + ArtworkInfo destructor races,
  QNAM first-use thread spin-up). QThread::wait /
  QFuture::waitForFinished provably synchronise but libQt6Core's
  stripped frames hide the happens-before edge. QtConcurrent worker
  reuse amplifies the issue. Re-evaluate on a Qt bump with TSan-visible
  QThread::wait or a build toggle for QtConcurrent pool no-reuse.
- **[Kartend-d4x4](https://github.com/EtherAura/Kartend)** — Third-party
  library sync primitives (3 entries: glib / pulseaudio / gstreamer).
  These libraries use their own sync primitives TSan can't observe.
  Re-evaluate on third-party version bumps or when Qt Multimedia
  switches off the GStreamer + PulseAudio backends.
- **Kartend-t9u0o (Group D + D2–D4, 2026-06-10/11)** — module-scoped
  entries that re-enabled the 19 QSKIP'd concurrency tests (DatAudit
  fan-out, parallel scan, BatchScrapeRunner dispatch, HTTP client,
  ROM hasher and friends) under TSan. `called_from_lib:libQt6Core/
  libQt6Concurrent/libQt6Network` mutes interceptor events whose caller
  is inside stripped, uninstrumented Qt (plain loads/stores there are
  invisible anyway), while Kartend worker bodies — including Qt template
  code inlined into Kartend TUs — stay fully instrumented. D2–D4 anchor
  the inline-kernel hand-off shapes (`NonPromiseTaskResolver`,
  `startMapped`, `QMetaTypeForType`, `~QFutureInterface`) plus
  binary-scoped `mutex:` entries for Qt 6.4's confusing
  `__tsan_mutex_*` annotations; round 5 added the ctor-qualified
  `PendingHash::PendingHash` (a bare name would substring-match kernel
  template args and mute genuine DatAudit races) and `race:strdup`
  (pool-thread heap recycling, all frames stripped libc). Re-evaluate
  on a Qt bump or when CI's Qt ships TSan-instrumented.
- **Kartend-ie89x (Group E, 2026-06-11)** — first-TSan-exposure entries
  from the Kartend-6x8tn / Kartend-0eeuk test waves (4 entries, all the
  Group A/B futex-invisible-edge classes reached through new code):
  `QReadWriteLock`'s atomic write-lock fast path vs its contended
  `pthread_mutex_unlock` fallback ("unlock of an unlocked mutex" in
  `DirectoryCache::processQueuedDirectories`); the `schedulePrewarm` →
  `QThreadPool::start` std::function hand-off; the `~QFutureInterface`
  teardown family (watcher destruction vs `reportAndMoveResult`); and
  `ArtworkWizardDialog::renderCurrent`'s `QtConcurrent::run` hand-off
  (QString COW capture copy / task first-read / task free — enumerated
  exhaustively with `halt_on_error=0`: 15 reports, 3 shapes, one call
  site). Methodology notes: `halt_on_error=1` in the CI leg shows only
  the FIRST unsuppressed report per binary, so triage one report per
  run converges slowly — run the binary in the kartend-ci container
  with `halt_on_error=0` to enumerate every remaining shape at once. A
  test-side `waitForDone()` drain does NOT fix these: on an
  already-idle pool it returns through the futex fast path without a
  condvar wait, creating no TSan-visible edge. Re-evaluate with Groups
  A/B on a Qt bump. Group letter D is reserved by the in-flight
  Kartend-t9u0o branch.

Same code-review-only methodology as the LSan audit applied — the
`--sanitize --tests` build needs Kartend-hx6l fixed before LSan/TSan
can be re-run locally to verify each entry still fires.

## LSan suppression audit (Kartend-3hjs.1, 2026-05-27)

Each entry in `tests/suppressions/lsan.txt` now carries:
- A clear comment explaining the leak path + why it's intentional.
- A `Closed: Kartend-<id>` citation referencing the bd that documented
  the rationale + closed-as-suppressed.
- A `Re-evaluate when …` note where applicable (GStreamer entries; the
  others have an explicit "remove if X becomes possible" trigger).

The 2026-05-27 audit was **code-review only** — the `--sanitize --tests`
build at the time failed to link several test exes because of a CMake
layering issue where the then-data-layer `settingsdialogcontroller.cpp`
called into `kartend_ui` symbols (`ErrorPresentation::showError`) that the
test link command didn't include. The `ErrorPresentation` default impl has
since moved to `kartend_utils` (Kartend-hx6l), and the dialog orchestration
itself now lives at the ui layer as
`src/ui/controllers/settingsdialogcontroller/` (Kartend-q8p29). Once the
sanitizer build is re-verified, re-run LSan and confirm each suppression
entry still fires (else delete and remove the bd ID).

## Don't

- **Don't blanket-suppress a library.** `race:libQt5Core` would
  hide every Qt-internal race plus any of our races whose stacks
  happen to include a Qt frame. Suppress the narrowest viable
  symbol.
- **Don't suppress your own bug.** Fix the lock, the ordering, the
  capture-by-reference. The cost of suppressing is N future
  debugging hours when the same class of bug surfaces elsewhere.
- **Don't suppress to make CI green for one PR.** The next PR
  inherits the suppression and now everyone has to remember why
  it's there.

## Related code

| Concern | File |
|---------|------|
| TSan suppressions | [tests/suppressions/tsan.txt](../../tests/suppressions/tsan.txt) |
| LSan suppressions | [tests/suppressions/lsan.txt](../../tests/suppressions/lsan.txt) |
| Sanitizer build options | [CMakeLists.txt](../../CMakeLists.txt) (`KARTEND_ENABLE_TSAN`, `KARTEND_ENABLE_ASAN`, `KARTEND_ENABLE_UBSAN`) |
| CI sanitizer run | `.github/workflows/build.yml` (thread-sanitizer job) |
| Local sanitizer run | [ci-local.md](ci-local.md) — `.scripts/ci-local.sh docker:tsan` |
