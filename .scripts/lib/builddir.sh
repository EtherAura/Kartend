# Build-directory management
#
# Generator tagging, per-mode build-dir naming, pruning stale dirs,
# marker files, and (re)preparing the active build dir.
#
# Sourced by .scripts/build.sh; relies on globals build.sh defines
# (colors, paths, progress/failure state). Not executed directly.
#
# These functions read and write build.sh globals; when shellcheck lints this
# file in isolation it can't see those cross-file assignments, so SC2154
# (referenced but not assigned) and SC2034 (assigned but unused) would
# false-positive. build.sh's `set -u` is the runtime backstop for a genuinely
# unset global. All other checks stay active.
# shellcheck shell=bash
# shellcheck disable=SC2154,SC2034

generator_tag() {
  if [ "${#generator_args[@]}" -ge 2 ] && [ "${generator_args[1]}" = "Ninja" ]; then
    echo "ninja"
    return
  fi
  echo "make"
}

build_dir_for_mode() {
  local mode="$1"
  echo "$root_dir/build/$(generator_tag)-$mode"
}

# Build-directory pruning (keep only one)
prune_other_builds() {
  local build_root="$1" keep_basename="$2"
  shopt -s nullglob
  for d in "$build_root"/*; do
    [ -d "$d" ] || continue
    [ "$(basename "$d")" = "$keep_basename" ] && continue
    # Prune dirs created by this script (.kartend-build-dir marker) AND
    # any dir that looks like a CMake build dir (CMakeCache.txt) so
    # hand-named legacy build folders don't accumulate (Kartend-bkq3).
    # Conservative: dirs without either signal are left alone — could be
    # the user's own scratch space.
    if [ -f "$d/$build_marker_file" ] || [ -f "$d/CMakeCache.txt" ]; then
      rm -rf -- "$d"
    fi
  done
  # Sweep stray top-level log files dropped by aborted runs or
  # out-of-tree CMake invocations. Anything matching *.log here is build
  # detritus — the active build's log lives under <build_dir>/logs/.
  for f in "$build_root"/*.log; do
    [ -f "$f" ] || continue
    rm -f -- "$f"
  done
  shopt -u nullglob
}

write_build_marker() {
  local dir="$1" mode="$2"
  local gen
  if [ "$(generator_tag)" = "ninja" ]; then
    gen="Ninja"
  else
    gen="Unix Makefiles"
  fi
  {
    echo "mode=${mode}"
    echo "generator=${gen}"
    echo "dir_tag=$(generator_tag)"
    echo "timestamp=${TS_HUMAN}"
  } >"$dir/$build_marker_file"
}

maybe_prepare_build_dir() {
  local dir="$1" logs="$2" mode="$3"
  if $incremental_build; then
    mkdir -p "$dir" "$logs"
    write_build_marker "$dir" "$mode"
    return 0
  fi
  rm -rf "$dir"
  mkdir -p "$dir" "$logs"
  write_build_marker "$dir" "$mode"
}
