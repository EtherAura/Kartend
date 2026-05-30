# Logging & Diagnostics

Kartend uses Qt's logging-category system so verbose tracing can be
toggled at runtime — no rebuild, no restart of anything other than
Kartend itself. Categories are scoped (`kartend.scanflow`,
`kartend.searchdiag`, etc.) so you can enable just the noise you care
about.

This page is the answer to "I have a weird scrolling glitch / artwork
isn't loading / search feels slow — how do I see what Kartend is
actually doing?"

> **Where to find this** — Logs go to **stderr**. Toggle with the
> `KARTEND_LOG_RULES` or standard `QT_LOGGING_RULES` environment
> variable. A handful of legacy `KARTEND_*_DIAG` variables remain for
> backwards compatibility.

## Reading logs

Kartend writes to standard error. To capture:

```bash
kartend 2>&1 | tee ~/kartend.log
```

The default verbosity is mostly silent — only `kartend.scanflow`
warnings (collection load lifecycle) emit by default. Anything more
detailed is opt-in.

Logs are unstructured single-line text. There's no log file format
guarantee — entries can change between releases. Don't grep against
specific phrasings if you can help it; categories and field structure
are more stable.

## Logging categories

| Category | Default | Purpose |
|----------|---------|---------|
| `kartend.scanflow` | warning-on | High-level lifecycle markers for collection scan + load |
| `kartend.searchdiag` | off | Search / filter pipeline timing and decisions |
| `kartend.perftrace` | off | Per-operation timing samples (frames, scrolls, paints) |
| `kartend.mainwindow` | warning-on | Window lifecycle, collection switches |
| `kartend.mousemanager` | warning-on | Mouse events, click / hold behaviors |
| `kartend.interactionmanager` | warning-on | Keyboard, selection, context menu handling |
| `kartend.keyboardmanager` | warning-on | Key input, repeat, alphabetic jumps |
| `kartend.navigationmanager` | warning-on | Navigation stack, subcollection traversal |
| `kartend.databasemanager` | warning-on | Database queries, item lookups |
| `kartend.querymanager` | warning-on | Worker-thread SQL queries |
| `kartend.cachemanager` | warning-on | Pixmap cache hits / misses / evictions |
| `kartend.eventmanager` | warning-on | Event filter routing |
| `kartend.animationmanager` | warning-on | Scroll / glide animation lifecycle |
| `kartend.selectionrestoremanager` | warning-on | Selection persistence |

"warning-on" means the category emits at the `qWarning` level by
default; `qDebug` and `qInfo` messages within the same category are
gated behind explicit enabling.

The list isn't exhaustive — new modules pick their own category names.
Easiest way to discover them: enable everything (`kartend.*=true`)
and grep the output.

## Enabling categories

Two ways: a Kartend-specific environment variable, or Qt's standard
one.

### `KARTEND_LOG_RULES`

Kartend's wrapper. Same syntax as `QT_LOGGING_RULES` but in a
Kartend-only namespace (so Qt-internal categories aren't affected).

```bash
KARTEND_LOG_RULES="kartend.*=true" kartend
```

Multiple rules separated by `;`:

```bash
KARTEND_LOG_RULES="kartend.scanflow.debug=true;kartend.searchdiag.debug=true" kartend
```

Each rule:

```
<category>.<level>=<true|false>
```

Levels: `debug`, `info`, `warning`, `critical`. Wildcards: `kartend.*`,
`kartend.search*`, etc.

Common one-liners:

| Goal | Rule |
|------|------|
| Enable all kartend logging | `kartend.*=true` |
| Enable only search diagnostics | `kartend.searchdiag.debug=true` |
| Enable scan + perf traces | `kartend.scanflow.debug=true;kartend.perftrace.debug=true` |
| Mute everything (clean stderr) | `kartend.*=false` |

### `QT_LOGGING_RULES`

Standard Qt's variable. Works for Kartend categories *and* Qt-internal
categories (which can be useful for, e.g., debugging `qt.qpa.*`
platform plugin issues).

```bash
QT_LOGGING_RULES="kartend.*=true" kartend
```

If both `KARTEND_LOG_RULES` and `QT_LOGGING_RULES` are set, both apply
(Qt's order). Specifically, `KARTEND_LOG_RULES` is bridged into
`QT_LOGGING_RULES` at startup, so you can think of them as additive.

### Persistent rules

For repeatable runs, drop the rules into a Qt logging config file:

```ini
# ~/.config/QtProject/qtlogging.ini
[Rules]
kartend.searchdiag.debug=true
kartend.perftrace.debug=true
```

Kartend (and any other Qt app) reads this on launch.

## Legacy environment variables

Pre-categorization, Kartend used direct env vars to flip diagnostic
modes. The legacy variables are still bridged at startup for backward
compatibility:

| Legacy env var | Equivalent rule |
|----------------|-----------------|
| `KARTEND_SCAN_DIAG=1` | `kartend.scanflow.debug=true` |
| `KARTEND_SEARCH_DIAG=1` | `kartend.searchdiag.debug=true` |
| `KARTEND_PERF_TRACE=1` | `kartend.perftrace.debug=true` |
| `KARTEND_RANGE_DIAG=1` | `kartend.querymanager.debug=true` (range fetch) |

Prefer the rule syntax — it composes better and surfaces every
category, not just the four legacy ones.

## Other diagnostic environment variables

Beyond logging, a few env vars affect runtime behavior:

| Variable | Purpose |
|----------|---------|
| `KARTEND_SMOKE_TEST_EXIT_MS` | Auto-exit after the specified milliseconds. Used by CI smoke tests; set this when you want a timed Kartend run for benchmarks. |
| `QT_QPA_PLATFORM` | Qt platform plugin. `wayland` / `xcb` / `offscreen` (for headless). |
| `QT_STYLE_OVERRIDE` | Force a Qt style (`fusion`, `breeze`, etc.). |
| `QT_MEDIA_BACKEND` | Override Qt Multimedia's backend (`gstreamer`, `ffmpeg`). Useful if [video previews](Video-Previews.md) misbehave. |
| `QT_LOGGING_RULES` | Standard Qt rules. |
| `KARTEND_LOG_RULES` | Kartend rules (same syntax, Kartend categories only). |
| `XDG_CONFIG_HOME` / `XDG_DATA_HOME` / `XDG_CACHE_HOME` | Override config / data / cache directories. See [File Locations](File-Locations.md). |
| `HOME` | Standard Linux. Used as the base for `~` expansion in paths. |
| `DESTDIR` | Honored by `cmake --install` and by Kartend's `--install` flag for staged installs. |
| `ASAN_OPTIONS`, `UBSAN_OPTIONS`, `LSAN_OPTIONS` | Sanitizer behavior; only relevant if you built with `-DKARTEND_ENABLE_SANITIZERS=ON`. |

## Recipes

### Diagnose slow first-launch scan

```bash
KARTEND_LOG_RULES="kartend.scanflow.debug=true;kartend.databasemanager.debug=true" \
  kartend 2>&1 | tee scan.log
```

Then grep:

```bash
grep -E "scan|item count|elapsed" scan.log
```

The `scanflow` category narrates each phase of a collection load
(enumerate → metadata fetch → artwork match → render). `databasemanager`
adds SQL timing.

### Investigate why search is laggy

```bash
KARTEND_LOG_RULES="kartend.searchdiag.debug=true" kartend
```

Type a search; the log shows the debounce timing, the filter pipeline
phases, and the row counts at each stage.

### Trace artwork loading

```bash
KARTEND_LOG_RULES="kartend.cachemanager.debug=true" kartend 2>&1 | grep -E "miss|loaded|evict"
```

Cache misses indicate first-time decode; evictions indicate the
in-memory pool is full (consider raising `pixmapCacheSizeMB`).

### Capture per-frame timing

```bash
KARTEND_LOG_RULES="kartend.perftrace.debug=true" kartend 2>perf.log
```

`perftrace` emits `[op_name] elapsed_ms=<n>` lines for instrumented
operations. Useful to spot regressions; less useful for first-time
performance investigation since most operations are not instrumented
unless you've added instrumentation in the relevant manager.

### Debug video preview codec issues

```bash
QT_LOGGING_RULES="qt.multimedia.*=true" kartend 2>&1 | grep -i codec
```

Qt Multimedia's own logging tells you which backend (GStreamer / FFmpeg)
loaded and which decoders are available. Pair with `gst-inspect-1.0
| grep <codec>` outside Kartend to confirm system codec install.

### Reproduce a CI failure locally

```bash
QT_QPA_PLATFORM=offscreen \
LSAN_OPTIONS="suppressions=$(pwd)/tests/suppressions/lsan.txt" \
ASAN_OPTIONS="detect_leaks=1:halt_on_error=1" \
KARTEND_LOG_RULES="kartend.*=true" \
  ./build/sanitize/kartend
```

Mirrors the CI sanitizer job. Useful for chasing intermittent leaks.

## Reading qDebug output

Qt's logging messages have a convention:

```
qWarning: kartend.scanflow: scan started for collection "Films" (uuid=...)
qDebug: kartend.scanflow: enumerated 412 candidate files in 18ms
qWarning: kartend.databasemanager: SQL retry 1/3 after timeout
qCritical: kartend.launchmanager: launcher path no longer exists: /usr/bin/old-player
```

- **Severity** prefixes (`qWarning`, `qDebug`, etc.) reflect Qt's
  level system.
- The category name follows directly.
- The message body is freeform.

Kartend doesn't promise message stability — message text can change
between releases. If you need to react to log lines programmatically,
prefer parsing categories and severity, not message text.

## Crash dumps

Crash dumps aren't a Kartend feature — they come from your system.
Most modern Linux distros use `systemd-coredump`:

```bash
coredumpctl list kartend             # any recent crashes?
coredumpctl info <pid>               # details
coredumpctl debug <pid>              # gdb session
```

For Arch / Fedora / Debian / Ubuntu / Gentoo with systemd this works
out of the box. On non-systemd setups you'll need to configure
`core.pattern` and ulimit yourself.

To get a useful core, build with debug info — `.scripts/build.sh
--debug` or `.scripts/build.sh --relwithdebinfo` (release with
symbols). See [building.md](../dev/building.md).

## Where to next

- [Troubleshooting](Troubleshooting.md) — symptom-based fixes
- [Building → Sanitizers](../dev/building.md) — running with ASan / UBSan
  for memory issues
- [Testing](../dev/testing.md) — CI logging conventions
- [File Locations](File-Locations.md) — where logs / config live

## For developers

- Logging categories: each module declares its own with
  `Q_LOGGING_CATEGORY(...)` near the top of its `.cpp`. Search for
  these to discover the full list.
- Bridging legacy env vars: in `MainWindow::applyLegacyEnvVars` or a
  similar bootstrap helper — see `main.cpp`.
- Adding a new category: pick a `kartend.<module>` name (one per
  feature module, plural messages OK), declare with `Q_LOGGING_CATEGORY`,
  use `qCDebug(category)` / `qCWarning(category)` rather than raw
  `qDebug` / `qWarning` (the maintenance build's "raw qLogging guard"
  flags raw uses in `src/`).
- The maintenance build's logging guard:
  `.scripts/build.sh --maintenance --format-check` errors out if any
  raw `qDebug()` or `qWarning()` site appears under `src/`. Convert to
  `qCDebug(category)` etc.
- Performance instrumentation: `qCDebug(perftrace)
  << "name elapsed_ms=" << qint64;` is the convention.
