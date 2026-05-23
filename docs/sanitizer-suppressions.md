# Sanitizer suppressions policy

Two suppression files at the repo root:

- [.tsan_suppressions.txt](../.tsan_suppressions.txt) — ThreadSanitizer
- [.lsan_suppressions.txt](../.lsan_suppressions.txt) — LeakSanitizer

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

### TSan ([.tsan_suppressions.txt](../.tsan_suppressions.txt))

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

### LSan ([.lsan_suppressions.txt](../.lsan_suppressions.txt))

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
[.tsan_suppressions.txt](../.tsan_suppressions.txt) and
[.lsan_suppressions.txt](../.lsan_suppressions.txt) follow this
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

| Pattern | Why |
|---------|-----|
| `race:QtPrivate::QSlotObject` | Qt's `BlockingQueuedConnection` heap-allocates slot objects; sync is inside Qt's internal `QMetaObject` mutex which TSan can't observe |
| `called_from_lib:libglib-2.0.so` | glib uses an eventfd wakeup pattern with internal mutexes TSan doesn't see |
| `race:libpulsecommon` | PulseAudio's threaded-ml thread uses its own sync primitives |
| `leak:QThreadPool::QThreadPool` | CacheManager's worker pool fallback path when long I/O ignores cancel — covered by a 2s timeout, fix tracked separately |

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
| TSan suppressions | [.tsan_suppressions.txt](../.tsan_suppressions.txt) |
| LSan suppressions | [.lsan_suppressions.txt](../.lsan_suppressions.txt) |
| Sanitizer build options | [CMakeLists.txt](../CMakeLists.txt) (`KARTEND_ENABLE_TSAN`, `KARTEND_ENABLE_ASAN`, `KARTEND_ENABLE_UBSAN`) |
| CI sanitizer run | `.github/workflows/build.yml` (thread-sanitizer job) |
| Local sanitizer run | [ci-local.md](ci-local.md) — `.scripts/ci-local.sh docker:tsan` |
