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
#      ignored. That start also has to run as the process named "kartend"
#      (/proc/<pid>/comm), which is what process lists and the X11 window
#      class show; process-name-check.sh does that check.
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
EOF
}

EXPECT_VERSION=""
PROBE=""
TEST_X11=0
TEST_WAYLAND=0
RUN_MS=4000
APPIMAGE=""

while (($# > 0)); do
  case "$1" in
    --expect-version | --run-ms | --probe)
      (($# >= 2)) || kartend_appimage_die "$1 needs a value"
      case "$1" in
        --expect-version) EXPECT_VERSION=$2 ;;
        --run-ms) RUN_MS=$2 ;;
        --probe) PROBE=$2 ;;
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

# The same start, through offscreen-start.sh and its process-name-check.sh,
# also proves the name the app runs under: AppRun execs usr/bin/kartend, and
# that process's Linux name (/proc/<pid>/comm) must be exactly "kartend", not
# "AppRun.wrapped". It is read once Qt's plugin-loader log shows the app
# itself is running (offscreen-start.sh fixes the log format for that); the
# app still quits by itself after RUN_MS, or is ended by a 180 s timeout.
name_ok=1
"${app_env[@]}" bash "$HERE/offscreen-start.sh" "$ROOT" "$WORK/foreign-plugins" "$RUN_MS" \
  "$WORK/offscreen.log" "$WORK/offscreen.status" || name_ok=0
status=""
if [ -f "$WORK/offscreen.status" ]; then
  IFS= read -r status <"$WORK/offscreen.status" || status=""
fi
if ((name_ok)); then
  pass "headless start (offscreen): the app runs under the process name kartend"
else
  fail "headless start (offscreen): the app does not run under the process name kartend, or that could not be checked (see above)"
fi
if [ "$status" = "0" ]; then
  check_plugins "$WORK/offscreen.log" "headless start (offscreen)" libqoffscreen.so libqsqlite.so
else
  show_log "$WORK/offscreen.log"
  fail "headless start of the unpacked app ended with an error (exit status ${status:-unknown}, 124 means it hung)"
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
