#!/usr/bin/env bash
# Start a Kartend AppImage on this system and check that it really works.
#
#   packaging/appimage/smoke-test.sh [options] Kartend-<version>-x86_64.AppImage
#
# Needs no display and no Qt on the system: the windows go to Qt's offscreen
# platform, and a throwaway home directory keeps the run away from any real
# Kartend configuration. build-appimage.sh runs this on every AppImage it
# makes; CI runs it again in clean containers of other distributions.
#
# What is checked:
#
#   1. The AppImage file itself runs: `--version` prints the version, and a
#      full headless start (main window, library database, settings) ends
#      with exit status 0. Where FUSE is not usable (containers), the
#      AppImage unpacks itself to a temporary directory first.
#   2. On this system, every library and Qt plugin inside the AppImage finds
#      all of its dependencies. That is what tells whether the X11, Wayland,
#      SQLite, TLS and FFmpeg plugins can load here, without a display.
#   3. The start used the AppImage's own Qt plugins: the offscreen platform
#      and the SQLite driver were loaded from inside it, nothing was loaded
#      from a Qt on this system, and a QT_PLUGIN_PATH pointing elsewhere is
#      ignored.
#   4. With --probe: the probe that build-appimage.sh writes next to the
#      AppImage is run inside the unpacked AppImage, against its Qt. It
#      creates a SQLite database, asks Qt for a working TLS backend, checks
#      the artwork image formats, and plays the two clips in probe/ (H.264
#      with AAC, VP9 with Opus) through FFmpeg: video frames have to arrive
#      and the audio has to decode, in software if the machine has no GPU.
#   5. With --x11 or --wayland: the same start against a real display server
#      (Xvfb, or Weston's headless backend), which proves that platform
#      plugin connects.
#
# With --check-extract-cache present|absent, only one other check runs, and
# none of the above: whether `--appimage-extract-and-run`, the documented
# start without FUSE, leaves its unpacked copy (an appimage_extracted_*
# directory) in TMPDIR after the app exits. The AppImage is started once with
# `--version`, offscreen, with a new empty TMPDIR of its own, a throwaway home
# and NO_CLEANUP unset. The start has to exit 0 and print exactly the version
# line "kartend X.Y.Z"; then that TMPDIR must hold such a directory (present)
# or no appimage_extracted_* entry at all, not even a file or symlink (absent).
#
# Exit status: 0 when every check passed, 1 otherwise.
set -euo pipefail

KARTEND_APPIMAGE_PROG="smoke-test"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=packaging/appimage/common.sh
. "$HERE/common.sh"

usage() {
  cat <<'EOF'
Usage: smoke-test.sh [options] APPIMAGE

  --expect-version X.Y.Z   fail unless `--version` reports this version
  --probe FILE             also run this kartend-appimage-probe inside the AppImage
  --x11                    also start it under Xvfb (needs xvfb-run)
  --wayland                also start it under Weston's headless backend (needs weston)
  --run-ms N               how long the started app runs before it quits itself
                           (default: 4000)
  -h, --help               show this help

Extract-cache mode (runs only this check, not the ones above):

  --check-extract-cache present|absent
                           start `APPIMAGE --appimage-extract-and-run --version`
                           offscreen, with a new empty TMPDIR, a throwaway home
                           and NO_CLEANUP unset. The start must exit 0 and print
                           exactly one line, "kartend X.Y.Z". Then that TMPDIR
                           must still hold an unpacked appimage_extracted_*
                           directory (present; a file or symlink of that name
                           does not count), or must hold no appimage_extracted_*
                           entry of any type (absent). --expect-version applies
                           to this start too and must equal X.Y.Z exactly.
  --extract-timeout N      whole seconds that start may take, 1 to 999999,
                           without a leading 0 (default: 120); only together
                           with --check-extract-cache
EOF
}

EXPECT_VERSION=""
PROBE=""
TEST_X11=0
TEST_WAYLAND=0
RUN_MS=4000
APPIMAGE=""
CHECK_EXTRACT_CACHE=""
CHECK_EXTRACT_CACHE_GIVEN=0
EXTRACT_TIMEOUT=120
EXTRACT_TIMEOUT_GIVEN=0

while (($# > 0)); do
  case "$1" in
    --expect-version | --run-ms | --probe | --check-extract-cache | --extract-timeout)
      (($# >= 2)) || kartend_appimage_die "$1 needs a value"
      case "$1" in
        --expect-version) EXPECT_VERSION=$2 ;;
        --run-ms) RUN_MS=$2 ;;
        --probe) PROBE=$2 ;;
        --check-extract-cache)
          CHECK_EXTRACT_CACHE=$2
          CHECK_EXTRACT_CACHE_GIVEN=1
          ;;
        --extract-timeout)
          EXTRACT_TIMEOUT=$2
          EXTRACT_TIMEOUT_GIVEN=1
          ;;
      esac
      shift 2
      ;;
    --x11)
      TEST_X11=1
      shift
      ;;
    --wayland)
      TEST_WAYLAND=1
      shift
      ;;
    -h | --help)
      usage
      exit 0
      ;;
    -*)
      usage >&2
      kartend_appimage_die "unknown option: $1"
      ;;
    *)
      [ -z "$APPIMAGE" ] || kartend_appimage_die "more than one AppImage given"
      APPIMAGE=$1
      shift
      ;;
  esac
done

note() { kartend_appimage_note "$@"; }
die() { kartend_appimage_die "$@"; }

# The extract-cache options are checked before anything starts, so that a bad
# one never falls through to the other checks. The timeout is compared as
# text, never in Bash arithmetic, which would read a leading 0 as octal.
if ((CHECK_EXTRACT_CACHE_GIVEN)); then
  case "$CHECK_EXTRACT_CACHE" in
    present | absent) ;;
    *) die "--check-extract-cache needs 'present' or 'absent', not '$CHECK_EXTRACT_CACHE'" ;;
  esac
  if ! [[ "$EXTRACT_TIMEOUT" =~ ^[1-9][0-9]{0,5}$ ]]; then
    die "--extract-timeout needs whole seconds from 1 to 999999, written without a leading 0, not '$EXTRACT_TIMEOUT'"
  fi
elif ((EXTRACT_TIMEOUT_GIVEN)); then
  die "--extract-timeout applies only to --check-extract-cache present|absent"
fi

[ -n "$APPIMAGE" ] || {
  usage >&2
  die "no AppImage given"
}
[ -f "$APPIMAGE" ] || die "$APPIMAGE does not exist"
if ! [[ "$RUN_MS" =~ ^[0-9]+$ ]] || ((RUN_MS < 500)); then
  die "--run-ms needs a number of at least 500"
fi
APPIMAGE="$(readlink -f "$APPIMAGE")"
[ -x "$APPIMAGE" ] || die "$APPIMAGE is not executable (chmod +x it first)"
if [ -n "$PROBE" ]; then
  [ -f "$PROBE" ] || die "the probe $PROBE does not exist"
  PROBE="$(readlink -f "$PROBE")"
fi

# --- Only the extract-cache check, on request ------------------------------------------
# `--appimage-extract-and-run` unpacks the AppImage to
# $TMPDIR/appimage_extracted_<hash> and runs it from there. This looks at what
# one real start leaves behind, in a TMPDIR that nothing else writes to.
if ((CHECK_EXTRACT_CACHE_GIVEN)); then
  if [ -n "$PROBE" ] || ((TEST_X11)) || ((TEST_WAYLAND)); then
    die "--check-extract-cache runs only the extract-cache check; leave out --probe, --x11 and --wayland"
  fi
  for tool in find timeout; do
    command -v "$tool" >/dev/null 2>&1 || die "'$tool' is not installed"
  done

  # Everything this mode creates is under CACHE_WORK, and only CACHE_WORK is
  # removed on exit, also when a check failed. Directories get write
  # permission back first, since an unpacked copy may contain read-only ones.
  # Neither find nor rm follows a symlink, so a link the start left behind is
  # removed itself and whatever it points to stays untouched.
  CACHE_WORK="$(mktemp -d "${TMPDIR:-/tmp}/kartend-extract-cache.XXXXXX")"
  cleanup_cache_work() {
    find "$CACHE_WORK" -type d ! -perm -0700 -exec chmod u+rwx {} \; 2>/dev/null || true
    rm -rf "$CACHE_WORK"
  }
  trap cleanup_cache_work EXIT
  mkdir -p "$CACHE_WORK/tmp" "$CACHE_WORK/home" "$CACHE_WORK/runtime"
  chmod 0700 "$CACHE_WORK/tmp" "$CACHE_WORK/runtime"

  # NO_CLEANUP asks the runtime to keep the unpacked copy, so it must not
  # leak in from this session; nor may another temporary directory.
  status=0
  env -u NO_CLEANUP -u APPIMAGE_EXTRACT_AND_RUN -u TMP -u TEMP \
    -u QT_QPA_PLATFORM -u QT_QPA_PLATFORMTHEME -u QT_STYLE_OVERRIDE -u QT_DEBUG_PLUGINS \
    -u QT_LOGGING_RULES -u LD_LIBRARY_PATH -u LD_PRELOAD -u DISPLAY -u WAYLAND_DISPLAY \
    "HOME=$CACHE_WORK/home" \
    "XDG_CONFIG_HOME=$CACHE_WORK/home/.config" \
    "XDG_DATA_HOME=$CACHE_WORK/home/.local/share" \
    "XDG_CACHE_HOME=$CACHE_WORK/home/.cache" \
    "XDG_STATE_HOME=$CACHE_WORK/home/.local/state" \
    "XDG_RUNTIME_DIR=$CACHE_WORK/runtime" \
    "TMPDIR=$CACHE_WORK/tmp" \
    QT_QPA_PLATFORM=offscreen \
    timeout -k 5 "$EXTRACT_TIMEOUT" "$APPIMAGE" --appimage-extract-and-run --version \
    </dev/null >"$CACHE_WORK/version.out" 2>"$CACHE_WORK/version.err" || status=$?
  version_out="$(cat "$CACHE_WORK/version.out")"

  # Every top-level appimage_extracted_* entry, whatever its type: find's %y
  # names the entry itself (d, f, l, ...) and never follows a symlink, so a
  # link counts as a link, also when it dangles or points at a directory.
  # find writes its list to a file first and that list is read only when find
  # succeeded: a TMPDIR that could not be listed is a failure of its own, never
  # an empty cache, even if find printed some entries before it failed.
  leftovers=""
  extracted_dirs=""
  scan_ok=1
  find -P "$CACHE_WORK/tmp" -mindepth 1 -maxdepth 1 -name 'appimage_extracted_*' -printf '%y/%f\0' \
    >"$CACHE_WORK/scan.list" 2>"$CACHE_WORK/scan.err" || scan_ok=0
  if ((!scan_ok)); then
    : >"$CACHE_WORK/scan.list"
  fi
  while IFS= read -r -d '' entry; do
    entry_type=${entry%%/*}
    entry_name=${entry#*/}
    case "$entry_type" in
      d)
        entry_kind="directory"
        extracted_dirs+="${extracted_dirs:+, }$entry_name"
        ;;
      f) entry_kind="regular file" ;;
      l) entry_kind="symlink" ;;
      *) entry_kind="special file" ;;
    esac
    leftovers+="${leftovers:+, }$entry_name ($entry_kind)"
  done <"$CACHE_WORK/scan.list"

  # QCommandLineParser::addVersionOption() prints applicationName(), a space
  # and applicationVersion() on stdout: "kartend X.Y.Z", APP_NAME and the
  # VERSION triple that the top-level CMakeLists.txt enforces. Nothing else
  # may be on stdout, and --expect-version has to equal the whole version.
  version_re='^kartend ([0-9]+\.[0-9]+\.[0-9]+)$'
  cache_fail() { printf '%s: FAIL: %s\n' "$KARTEND_APPIMAGE_PROG" "$*" >&2; }
  started=0
  if [ "$status" -ne 0 ]; then
    tail -n 40 "$CACHE_WORK/version.err" >&2
    if [ "$status" -eq 124 ] || [ "$status" -eq 137 ]; then
      cache_fail "--appimage-extract-and-run --version did not finish within ${EXTRACT_TIMEOUT} s"
    else
      cache_fail "--appimage-extract-and-run --version exited with status $status"
    fi
  elif [[ "$version_out" == *$'\n'* ]] || ! [[ "$version_out" =~ $version_re ]]; then
    cache_fail "--appimage-extract-and-run --version printed '$version_out'," \
      "not the single version line 'kartend X.Y.Z'"
  elif [ -n "$EXPECT_VERSION" ] && [ "${BASH_REMATCH[1]}" != "$EXPECT_VERSION" ]; then
    cache_fail "--appimage-extract-and-run --version reported version ${BASH_REMATCH[1]}," \
      "expected exactly $EXPECT_VERSION"
  else
    printf '%s: ok: --appimage-extract-and-run --version: %s\n' "$KARTEND_APPIMAGE_PROG" "$version_out" >&2
    started=1
  fi

  if ((!started)); then
    if ((scan_ok)); then
      note "the extract cache was not judged, because the start failed (left in TMPDIR: ${leftovers:-nothing})"
    else
      note "the extract cache was not judged, because the start failed (TMPDIR could not be listed either)"
    fi
    note "FAILED: $APPIMAGE"
    exit 1
  fi
  if ((!scan_ok)); then
    tail -n 20 "$CACHE_WORK/scan.err" >&2
    cache_fail "could not list the TMPDIR of the start after the app exited, so whether an" \
      "appimage_extracted_* entry stayed is unknown"
    note "the extract cache was not judged, because the scan failed"
    note "FAILED: $APPIMAGE"
    exit 1
  fi
  if [ "$CHECK_EXTRACT_CACHE" = absent ] && [ -n "$leftovers" ]; then
    cache_fail "an appimage_extracted_* entry stayed after the app exited: TMPDIR still holds $leftovers" \
      "(NO_CLEANUP was not set; absent expects no such entry of any type)"
  elif [ "$CHECK_EXTRACT_CACHE" = present ] && [ -z "$extracted_dirs" ]; then
    cache_fail "TMPDIR holds no unpacked appimage_extracted_* directory after the app exited" \
      "(entries found: ${leftovers:-none}), but present expects the unpacked copy to stay"
  else
    printf '%s: ok: extract cache %s after exit (%s)\n' "$KARTEND_APPIMAGE_PROG" \
      "$CHECK_EXTRACT_CACHE" "${leftovers:-no appimage_extracted_* entry}" >&2
    note "passed: $APPIMAGE"
    exit 0
  fi
  note "FAILED: $APPIMAGE"
  exit 1
fi

for tool in ldd find timeout grep; do
  command -v "$tool" >/dev/null 2>&1 || die "'$tool' is not installed"
done

WORK="$(mktemp -d "${TMPDIR:-/tmp}/kartend-smoke.XXXXXX")"
WESTON_PID=""
cleanup() {
  if [ -n "$WESTON_PID" ]; then
    kill "$WESTON_PID" 2>/dev/null || true
  fi
  rm -rf "$WORK"
}
trap cleanup EXIT

# A clean, private environment for every run of the app: its own home and
# XDG directories, and nothing from this session that would steer Qt.
mkdir -p "$WORK/home" "$WORK/runtime"
chmod 0700 "$WORK/runtime"
app_env=(
  env
  -u QT_QPA_PLATFORM -u QT_QPA_PLATFORMTHEME -u QT_STYLE_OVERRIDE -u QT_DEBUG_PLUGINS
  -u QT_LOGGING_RULES -u LD_LIBRARY_PATH -u LD_PRELOAD -u DISPLAY -u WAYLAND_DISPLAY
  "HOME=$WORK/home"
  "XDG_CONFIG_HOME=$WORK/home/.config"
  "XDG_DATA_HOME=$WORK/home/.local/share"
  "XDG_CACHE_HOME=$WORK/home/.cache"
  "XDG_STATE_HOME=$WORK/home/.local/state"
  "XDG_RUNTIME_DIR=$WORK/runtime"
)

# show_log FILE [LINES]: the end of a run's log without Qt's plugin-loader
# chatter. grep exits 1 when every line was chatter; that must not end the
# script (pipefail) just before the message that says what failed.
show_log() {
  { grep -v -E 'qt\.core\.(plugin|library)' "$1" || true; } | tail -n "${2:-40}" >&2
}

FAILED=0
fail() {
  printf '%s: FAIL: %s\n' "$KARTEND_APPIMAGE_PROG" "$*" >&2
  FAILED=1
}
pass() { printf '%s: ok: %s\n' "$KARTEND_APPIMAGE_PROG" "$*" >&2; }

# Mount the AppImage when FUSE works here, unpack-and-run when it does not.
RUN_MODE=()
if [ -n "${APPIMAGE_EXTRACT_AND_RUN:-}" ]; then
  RUN_MODE=("APPIMAGE_EXTRACT_AND_RUN=1")
  note "FUSE not used (APPIMAGE_EXTRACT_AND_RUN is set): the AppImage unpacks itself to run"
elif [ -c /dev/fuse ] && [ -r /dev/fuse ] && [ -w /dev/fuse ] &&
  { command -v fusermount3 >/dev/null 2>&1 || command -v fusermount >/dev/null 2>&1; }; then
  note "FUSE is available: the AppImage mounts itself to run"
else
  RUN_MODE=("APPIMAGE_EXTRACT_AND_RUN=1")
  note "no usable FUSE here: the AppImage unpacks itself to run"
fi

# --- 1. the AppImage file itself ---------------------------------------------------
version_out="$("${app_env[@]}" "${RUN_MODE[@]}" timeout 120 "$APPIMAGE" --version 2>"$WORK/version.err")" || {
  cat "$WORK/version.err" >&2
  fail "--version exited with an error"
  version_out=""
}
if [ -n "$version_out" ]; then
  if ! grep -qi 'kartend' <<<"$version_out"; then
    fail "--version printed '$version_out', which is not Kartend's version line"
  elif [ -n "$EXPECT_VERSION" ] && ! grep -qF -- "$EXPECT_VERSION" <<<"$version_out"; then
    fail "--version printed '$version_out', expected version $EXPECT_VERSION"
  else
    pass "--version: $version_out"
  fi
fi

status=0
"${app_env[@]}" "${RUN_MODE[@]}" QT_QPA_PLATFORM=offscreen "KARTEND_SMOKE_TEST_EXIT_MS=$RUN_MS" \
  timeout 180 "$APPIMAGE" >"$WORK/start.log" 2>&1 || status=$?
if [ "$status" -eq 0 ]; then
  pass "headless start of the AppImage file: ran ${RUN_MS} ms, exit status 0"
else
  tail -n 40 "$WORK/start.log" >&2
  fail "headless start of the AppImage file ended with exit status $status (124 means it hung)"
fi

# --- 2. dependencies of everything inside ---------------------------------------------
(cd "$WORK" && "$APPIMAGE" --appimage-extract >/dev/null 2>"$WORK/extract.err") || {
  cat "$WORK/extract.err" >&2
  die "the AppImage could not unpack itself"
}
ROOT="$WORK/squashfs-root"
[ -x "$ROOT/AppRun" ] || die "the unpacked AppImage has no executable AppRun"

checked=0
: >"$WORK/missing.txt"
while IFS= read -r -d '' file; do
  [ "$(head -c 4 "$file" 2>/dev/null | od -An -tx1 | tr -d ' \n')" = "7f454c46" ] || continue
  checked=$((checked + 1))
  { env -u LD_LIBRARY_PATH -u LD_PRELOAD ldd "$file" 2>/dev/null || true; } |
    awk -v f="${file#"$ROOT"/}" '$2 == "=>" && $3 == "not" { print $1 " (needed by " f ")" }' >>"$WORK/missing.txt"
done < <(find "$ROOT" -type f -print0)
if [ -s "$WORK/missing.txt" ]; then
  sort -u "$WORK/missing.txt" >&2
  fail "libraries missing on this system (list above); the AppImage expects them from the system"
else
  pass "all $checked binaries inside find their libraries on this system"
fi

# --- 3. the app's own Qt plugins are the ones that load ---------------------------------
# run_unpacked LOG ENV...: start the unpacked app with plugin loading logged.
# QT_PLUGIN_PATH is set to a directory outside the AppImage on purpose: a
# session that sets it (some desktops do) must not pull in foreign plugins.
mkdir -p "$WORK/foreign-plugins/platforms"
run_unpacked() {
  local log=$1
  shift
  local status=0
  "${app_env[@]}" QT_DEBUG_PLUGINS=1 "QT_PLUGIN_PATH=$WORK/foreign-plugins" \
    "KARTEND_SMOKE_TEST_EXIT_MS=$RUN_MS" "$@" timeout 180 "$ROOT/AppRun" >"$log" 2>&1 || status=$?
  return "$status"
}

# loaded_plugins LOG: paths of the plugins Qt reported as loaded.
loaded_plugins() {
  { grep -F 'loaded library' "$1" || true; } | { grep -oE '"/[^"]+"' || true; } | tr -d '"' | sort -u
}

# check_plugins LOG LABEL NAME...: every NAME must have been loaded, and
# everything that was loaded must come from inside the unpacked AppImage.
check_plugins() {
  local log=$1 label=$2 name path ok=1
  shift 2
  loaded_plugins "$log" >"$WORK/loaded.txt"
  for name in "$@"; do
    if ! grep -qE "^$ROOT/usr/plugins/.*/$name\$" "$WORK/loaded.txt"; then
      fail "$label: the AppImage's own $name was not loaded"
      ok=0
    fi
  done
  while IFS= read -r path; do
    case "$path" in
      "$ROOT"/*) ;;
      *)
        fail "$label: a plugin from outside the AppImage was loaded: $path"
        ok=0
        ;;
    esac
  done <"$WORK/loaded.txt"
  if grep -qF "$WORK/foreign-plugins" "$log"; then
    fail "$label: Qt searched the QT_PLUGIN_PATH of the session; the AppImage must ignore it"
    ok=0
  fi
  for bad in "Could not load the Qt platform plugin" "Could not find the Qt platform plugin" \
    "driver not loaded" "No QtMultimedia backends found"; do
    if grep -qF "$bad" "$log"; then
      fail "$label: the log says '$bad'"
      ok=0
    fi
  done
  if ((ok)); then
    pass "$label: loaded from inside the AppImage: $(sed "s|^$ROOT/usr/plugins/||" "$WORK/loaded.txt" | tr '\n' ' ')"
  fi
}

if run_unpacked "$WORK/offscreen.log" QT_QPA_PLATFORM=offscreen; then
  check_plugins "$WORK/offscreen.log" "headless start (offscreen)" libqoffscreen.so libqsqlite.so
else
  show_log "$WORK/offscreen.log"
  fail "headless start of the unpacked app ended with an error"
fi

# --- 4. the probe ------------------------------------------------------------------
if [ -n "$PROBE" ]; then
  # Next to kartend, so the bundle's qt.conf and libraries apply to it.
  install -m 0755 "$PROBE" "$ROOT/usr/bin/kartend-appimage-probe"
  media_args=()
  for clip in "$HERE"/probe/*.mp4 "$HERE"/probe/*.webm; do
    [ -f "$clip" ] && media_args+=(--media "$clip")
  done
  ((${#media_args[@]} > 0)) || die "the test clips are missing from $HERE/probe"
  status=0
  "${app_env[@]}" QT_QPA_PLATFORM=offscreen QT_DEBUG_PLUGINS=1 \
    timeout 180 "$ROOT/usr/bin/kartend-appimage-probe" "${media_args[@]}" \
    >"$WORK/probe.out" 2>"$WORK/probe.log" || status=$?
  sed "s/^/$KARTEND_APPIMAGE_PROG: probe: /" "$WORK/probe.out" >&2
  if [ "$status" -ne 0 ]; then
    show_log "$WORK/probe.log" 30
    fail "the probe ended with exit status $status"
  else
    check_plugins "$WORK/probe.log" "probe" libqoffscreen.so libqsqlite.so libqopensslbackend.so \
      libffmpegmediaplugin.so libqwebp.so
  fi
  rm -f "$ROOT/usr/bin/kartend-appimage-probe"
else
  note "no --probe given: TLS, the image formats and media playback were not exercised"
fi

# --- 5. real display servers, on request ---------------------------------------------
if ((TEST_X11)); then
  command -v xvfb-run >/dev/null 2>&1 || die "--x11 needs xvfb-run"
  if run_unpacked "$WORK/x11.log" QT_QPA_PLATFORM=xcb \
    xvfb-run --auto-servernum --server-args="-screen 0 1280x800x24"; then
    check_plugins "$WORK/x11.log" "start under X11 (Xvfb)" libqxcb.so libqsqlite.so
  else
    show_log "$WORK/x11.log"
    fail "start under X11 (Xvfb) ended with an error"
  fi
fi

if ((TEST_WAYLAND)); then
  command -v weston >/dev/null 2>&1 || die "--wayland needs weston"
  socket="kartend-smoke"
  # Newer Weston names the backend "headless", older ones "headless-backend.so".
  for backend in headless headless-backend.so; do
    XDG_RUNTIME_DIR="$WORK/runtime" weston --backend="$backend" --socket="$socket" \
      --width=1280 --height=800 >"$WORK/weston-$backend.log" 2>&1 &
    WESTON_PID=$!
    for _ in $(seq 1 50); do
      [ -S "$WORK/runtime/$socket" ] && break
      kill -0 "$WESTON_PID" 2>/dev/null || break
      sleep 0.2
    done
    [ -S "$WORK/runtime/$socket" ] && break
    kill "$WESTON_PID" 2>/dev/null || true
    WESTON_PID=""
  done
  if [ -z "$WESTON_PID" ]; then
    cat "$WORK"/weston-*.log >&2
    fail "Weston's headless backend did not start"
  elif run_unpacked "$WORK/wayland.log" QT_QPA_PLATFORM=wayland "WAYLAND_DISPLAY=$socket"; then
    check_plugins "$WORK/wayland.log" "start under Wayland (Weston headless)" libxdg-shell.so libqsqlite.so
    grep -qE "^$ROOT/usr/plugins/platforms/libqwayland-(generic|egl)\.so\$" "$WORK/loaded.txt" ||
      fail "start under Wayland: no Wayland platform plugin of the AppImage was loaded"
  else
    show_log "$WORK/wayland.log"
    fail "start under Wayland (Weston headless) ended with an error"
  fi
fi

if ((FAILED)); then
  note "FAILED: $APPIMAGE"
  exit 1
fi
note "passed: $APPIMAGE"
