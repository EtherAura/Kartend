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
    # Never treat a symlink as a prunable build dir (Kartend-pnlot.9): the
    # `current` convenience link resolves into a real build dir, so the
    # marker check below would match through it.
    [ -L "$d" ] && continue
    [ "$(basename "$d")" = "$keep_basename" ] && continue
    # Prune ONLY dirs this script created, identified by the marker file
    # write_build_marker drops on prepare. A bare CMakeCache.txt used to be
    # treated as an equivalent signal, but that deleted build dirs the
    # script never owned — hand-configured cmake dirs, IDE trees, and the
    # ci-local.sh docker builds (build/Release-clang, build/TSan) all carry
    # a CMakeCache.txt with no marker. Anything without the marker is left
    # alone now; it is not ours to delete.
    if [ -f "$d/$build_marker_file" ]; then
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
  # If the sweep above removed the dir the `current` convenience symlink
  # (Kartend-pnlot.9) pointed at — a mode switch prunes the previous mode's
  # dir — drop the link rather than leave it dangling. A successful build
  # repoints it via publish_current_build_dir right after this step.
  if [ -L "$build_root/current" ] && [ ! -e "$build_root/current" ]; then
    rm -f -- "$build_root/current"
  fi
}

# Post-success epilogue (Kartend-pnlot.9): refresh the stable build/current
# convenience symlink and print the exact ctest invocation for the dir this
# run just wrote. The per-mode dir names (ninja-release, ninja-maintenance,
# ...) force developers to track which dir is fresh — and ctest run in a
# stale sibling passes silently against old binaries. Relative link target
# so a moved checkout stays valid; ln -sfn repoints atomically. Call only
# after a successful build (alongside the final step_final).
publish_current_build_dir() {
  local dir="$1"
  ln -sfn "$(basename "$dir")" "$root_dir/build/current"
  if $build_tests; then
    step_final "Run tests: ctest --test-dir ${dir#"$root_dir/"} --output-on-failure -LE benchmark -j $build_jobs (build/current also points there)"
  fi
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
