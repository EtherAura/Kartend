# Test & install helpers
#
# ctest runner (refuses to run in the repo root) and cmake --install
# with sudo/doas auto-elevation.
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

run_ctest() {
  local dir="$1"
  # Refuse to run ctest in the repo root: ctest scans PWD for tests if it
  # can't locate CTestTestfile.cmake and leaks a `Testing/` scratch dir
  # alongside the source tree. The build dir always has CTestTestfile.cmake
  # next to the per-test binaries; that's the only place ctest belongs.
  # repo_root is build.sh's root_dir global: run_ctest now lives in
  # lib/test.sh, so BASH_SOURCE here would point at the lib dir, not the
  # repo root. root_dir is always set by build.sh before any mode runs.
  local repo_root="$root_dir"
  local abs_dir
  abs_dir="$(cd "$dir" 2>/dev/null && pwd)" || {
    echo "run_ctest: build dir not found: $dir" >&2
    return 1
  }
  if [[ "$abs_dir" == "$repo_root" ]]; then
    echo "run_ctest: refusing to run ctest in the repo root ($abs_dir)." >&2
    echo "ctest must run against a configured build dir (e.g. build/ninja-release)." >&2
    return 1
  fi
  if [[ ! -f "$abs_dir/CTestTestfile.cmake" ]]; then
    echo "run_ctest: $abs_dir does not look like a build dir (no CTestTestfile.cmake)." >&2
    echo "Did you mean a sibling under build/?" >&2
    return 1
  fi
  # Exclude benchmarks (LABELS "benchmark") to match every CI ctest invocation
  # and the "skipped by default" contract in docs/dev/testing.md — a CMake
  # LABELS property does NOT exclude a test from a bare run, so it must be done
  # here. Run benchmarks explicitly with `ctest -L benchmark` in the build dir.
  #
  # -j matches the staged CI parallelism rollout (Kartend-pnlot.1); the harness
  # isolates per-test HOME so parallel runs are safe. build_jobs is build.sh's
  # --jobs=N global (defaults to nproc), always set before any mode runs — same
  # guarantee as root_dir above. Known flakes under -j + host load: the
  # DatabaseManager and QueryManagerCrossCollectionCount integration tests —
  # re-run one isolated before assuming a regression.
  ctest --test-dir "$dir" --output-on-failure -LE benchmark -j "$build_jobs"
}

# Run `cmake --install` and transparently elevate with sudo when the install
# prefix isn't writable by the current user (e.g. /usr/local). DESTDIR is honored.
do_cmake_install() {
  local cmake_exe="$1"; shift
  local bdir="$1"; shift
  local prefix
  prefix="$("$cmake_exe" -L -N "$bdir" 2>/dev/null | awk -F= '/^CMAKE_INSTALL_PREFIX:/{print $2; exit}')"
  local target="${DESTDIR:-}${prefix:-/usr/local}"
  # Walk up to the first existing ancestor; that's what we need write access to.
  local probe="$target"
  while [ -n "$probe" ] && [ ! -e "$probe" ]; do
    probe="$(dirname "$probe")"
  done
  if [ -n "$probe" ] && [ -w "$probe" ]; then
    "$cmake_exe" --install "$bdir"
  elif command -v sudo >/dev/null 2>&1; then
    # Clear the in-progress step line on the user's TTY so the password prompt
    # doesn't collide with the progress indicator. Prime sudo so the prompt
    # appears even when stderr is captured by the run_step harness.
    if [ -w /dev/tty ]; then printf '\r\033[2K' >/dev/tty || true; fi
    # shellcheck disable=SC2024
    sudo -v </dev/tty >/dev/tty 2>/dev/tty || true
    sudo -E "$cmake_exe" --install "$bdir"
  elif command -v doas >/dev/null 2>&1; then
    if [ -w /dev/tty ]; then printf '\r\033[2K' >/dev/tty || true; fi
    # doas doesn't have a generic env-preserve flag; pass DESTDIR explicitly if set.
    if [ -n "${DESTDIR:-}" ]; then
      doas env "DESTDIR=$DESTDIR" "$cmake_exe" --install "$bdir" </dev/tty
    else
      doas "$cmake_exe" --install "$bdir" </dev/tty
    fi
  else
    "$cmake_exe" --install "$bdir"
  fi
}
