#!/usr/bin/env bash
# shellcheck source-path=SCRIPTDIR
set -euo pipefail
# build.sh is a thin entrypoint: it parses arguments, validates mode
# combinations, and orchestrates each build mode. The reusable helper
# functions live in sourced modules under .scripts/lib/.
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
# shellcheck source=lib/clang-format-version.sh
source "$script_dir/lib/clang-format-version.sh"
# shellcheck source=lib/ui.sh
source "$script_dir/lib/ui.sh"
# shellcheck source=lib/steps.sh
source "$script_dir/lib/steps.sh"
# shellcheck source=lib/builddir.sh
source "$script_dir/lib/builddir.sh"
# shellcheck source=lib/quality.sh
source "$script_dir/lib/quality.sh"
# shellcheck source=lib/test.sh
source "$script_dir/lib/test.sh"

# build.sh targets Linux: it relies on GNU coreutils (stat -c, nproc) and
# bash login shells. Fail fast with a clear pointer rather than emitting
# cryptic errors on macOS/BSD/Windows (Kartend audit DX-15f).
if [ "$(uname -s)" != "Linux" ]; then
  echo "build.sh supports Linux only (it relies on GNU coreutils: stat -c, nproc)." >&2
  echo "For macOS/Windows, see the Manual Build section in docs/dev/building.md." >&2
  exit 1
fi

# Parse args
debug_build=false
relwithdebinfo_build=false
sanitize_build=false
maintenance_build=false
apply_fixes=false
apply_fixes_dirty_ok=false
format_check=false
format_apply=false
pgo_generate=false
pgo_use=false
pgo_build=false
keep_builds=false
use_ccache=true
# Repeatable --cmake-arg=<ARG> collector; folded into extra_cmake_args below
# so the args reach every mode's configure invocation (incl. both PGO passes).
user_cmake_args=()
build_tests=false
run_tests=false
coverage_build=false
install_after_build=false
generator_preference="auto"  # auto|ninja|make
incremental_build=true
make_archive=false
make_reports=false
force_clang=false
uninstall_only=false
install_prefix=""
build_jobs=""
# --analysis gates the advisory static-analysis trio in --maintenance mode:
#   full (default)  clang-tidy + IWYU + cppcheck (local runs, nightly CI sweep)
#   tidy            clang-tidy only (CI pull_request runs pair this with
#                   KARTEND_TIDY_ONLY_FILES for a diff-scoped pass)
#   off             none of the three (CI push-to-main runs an enforcing
#                   curated-check clang-tidy gate in the workflow instead;
#                   the full advisory sweep runs nightly)
analysis_mode="full"
for arg in "$@"; do
  case "$arg" in
    -h|--help) usage; exit 0 ;;
    --debug)       debug_build=true ;;
    --relwithdebinfo) relwithdebinfo_build=true ;;
    --sanitize|--sanitizers) sanitize_build=true ;;
    --maintenance) maintenance_build=true ;;
    --apply-fixes) apply_fixes=true ;;
    --apply-fixes-dirty-ok) apply_fixes_dirty_ok=true ;;
    --format-check) format_check=true ;;
    --format-apply) format_apply=true ;;
    --tests)       build_tests=true ;;
    --run-tests)   run_tests=true ;;
    --coverage)    coverage_build=true; build_tests=true; debug_build=true ;;
    --install)     install_after_build=true ;;
    --uninstall)   uninstall_only=true ;;
    --prefix=*)    install_prefix="${arg#--prefix=}" ;;
    --jobs=*)      build_jobs="${arg#--jobs=}" ;;
    --ninja)       generator_preference="ninja" ;;
    --make)        generator_preference="make" ;;
    --incremental) incremental_build=true ;;
    --clean)       incremental_build=false ;;
    --archive)     make_archive=true ;;
    --reports)     make_reports=true ;;
    --pgo-generate) pgo_generate=true ;;
    --pgo-use)     pgo_use=true ;;
    --pgo)         pgo_build=true ;;
    --keep-builds) keep_builds=true ;;
    --cmake-arg=*) user_cmake_args+=("${arg#--cmake-arg=}") ;;
    --no-ccache)   use_ccache=false ;;
    --clang)       force_clang=true ;;
    --analysis=*)  analysis_mode="${arg#--analysis=}" ;;
    *)
      printf 'Error: unknown option: %s\n' "$arg" >&2
      usage >&2
      exit 2
      ;;
  esac
done

# --jobs=N validation; default to nproc.
if [ -n "$build_jobs" ]; then
  if ! [[ "$build_jobs" =~ ^[1-9][0-9]*$ ]]; then
    echo "Error: --jobs requires a positive integer (got '$build_jobs')." >&2
    exit 2
  fi
else
  build_jobs="$(nproc)"
fi
if ($debug_build && $sanitize_build) || ($maintenance_build && $sanitize_build); then
  echo "Error: --sanitize is mutually exclusive with --debug/--maintenance."
  exit 1
fi
if $debug_build && $maintenance_build; then
  echo "Error: --debug and --maintenance are mutually exclusive."
  exit 1
fi
if $relwithdebinfo_build && ($debug_build || $sanitize_build || $maintenance_build); then
  echo "Error: --relwithdebinfo is mutually exclusive with --debug/--sanitize/--maintenance."
  exit 1
fi
if $relwithdebinfo_build && ($pgo_build || $pgo_generate || $pgo_use); then
  echo "Error: --relwithdebinfo is mutually exclusive with PGO options."
  exit 1
fi
if $pgo_generate && $pgo_use; then
  echo "Error: --pgo-generate and --pgo-use are mutually exclusive."
  exit 1
fi
if ($pgo_generate || $pgo_use) && $maintenance_build; then
  echo "Error: PGO options are not supported in maintenance mode."
  exit 1
fi
if $pgo_build && ($debug_build || $maintenance_build || $pgo_generate || $pgo_use); then
  echo "Error: --pgo is mutually exclusive with other build options."
  exit 1
fi
if $sanitize_build && ($pgo_build || $pgo_generate || $pgo_use); then
  echo "Error: --sanitize is mutually exclusive with PGO options."
  exit 1
fi
if $apply_fixes && ! $maintenance_build; then
  echo "Error: --apply-fixes is only supported with --maintenance."
  exit 1
fi
if $apply_fixes_dirty_ok && ! ($apply_fixes || $format_apply); then
  echo "Error: --apply-fixes-dirty-ok only applies with --apply-fixes or --format-apply."
  exit 1
fi
# Kartend-rqvp7: surface the override to the quality-lib guard, which reads
# KARTEND_APPLY_FIXES_DIRTY_OK (so it works whether the guard runs in-process
# or in a subshell).
if $apply_fixes_dirty_ok; then
  export KARTEND_APPLY_FIXES_DIRTY_OK=1
fi
if $format_apply && ! $maintenance_build; then
  echo "Error: --format-apply is only supported with --maintenance."
  exit 1
fi
if $format_check && ! $maintenance_build; then
  echo "Error: --format-check is only supported with --maintenance."
  exit 1
fi
case "$analysis_mode" in
  full|tidy|off) ;;
  *)
    echo "Error: --analysis must be one of full|tidy|off (got '$analysis_mode')." >&2
    exit 2
    ;;
esac
if [ "$analysis_mode" != "full" ] && ! $maintenance_build; then
  echo "Error: --analysis is only supported with --maintenance."
  exit 1
fi

if $run_tests && ! $build_tests; then
  echo "Error: --run-tests requires --tests (KARTEND_BUILD_TESTS=ON)."
  exit 1
fi

# Kartend-z4ev0: KARTEND_TIDY_ONLY_FILES (optional env) scopes the
# --maintenance clang-tidy pass to a newline-delimited list of .cpp paths
# (see do_clang_tidy in lib/quality.sh). Validate it up front so a typo'd
# path fails in seconds instead of after the ~10-min maintenance build.
if [ -n "${KARTEND_TIDY_ONLY_FILES:-}" ]; then
  if ! $maintenance_build; then
    echo "Error: KARTEND_TIDY_ONLY_FILES is only honored with --maintenance." >&2
    exit 1
  fi
  if [ "$analysis_mode" = "off" ]; then
    echo "Error: KARTEND_TIDY_ONLY_FILES is set but --analysis=off skips clang-tidy entirely; drop one of the two." >&2
    exit 1
  fi
  if [ ! -f "$KARTEND_TIDY_ONLY_FILES" ]; then
    echo "Error: KARTEND_TIDY_ONLY_FILES points to a missing file: $KARTEND_TIDY_ONLY_FILES" >&2
    exit 1
  fi
fi

# Paths
root_dir="$(cd "$script_dir/.." && pwd)"
cd "$root_dir"
mkdir -p build
parent_dir="$(dirname "$root_dir")"
backups_dir="$parent_dir/.backups"
# Created on demand: the reports assembly mkdir -p's $reports_dir itself, and
# each --archive staging step mkdir -p's $backups_dir with a REAL error.
# Eager creation here used to break bind-mounted environments (e.g.
# nektos/act) where the parent directory of the repo is owned by root and the
# script runs as the runner user — and the old swallowed `|| true` meant the
# failure surfaced much later as a confusing tar error (Kartend-oizkj).
reports_dir="$backups_dir/reports"

setup_colors

# Optional build accelerators
ccache_available=false
if $use_ccache && command -v ccache >/dev/null 2>&1; then
  ccache_available=true
fi

# Common cmake args injected after every cmake_args= declaration: --prefix
# plus any repeatable --cmake-arg=<ARG> passthroughs. Injection lands after
# the mode's base declaration (so a user -D overrides those, last-one-wins)
# but before the flags derived from other CLI options (--tests, --coverage,
# --no-ccache, sanitizers), which therefore still win over a conflicting
# --cmake-arg. Add future cross-cutting args here.
extra_cmake_args=()
if [ -n "$install_prefix" ]; then
  extra_cmake_args+=(-DCMAKE_INSTALL_PREFIX="$install_prefix")
fi
if [ ${#user_cmake_args[@]} -gt 0 ]; then
  extra_cmake_args+=("${user_cmake_args[@]}")
fi

# --uninstall: standalone path. Pick the most recent build dir under build/
# that has an install_manifest.txt and run the uninstall target. Auto-elevate
# with sudo/doas if needed.
if $uninstall_only; then
  shopt -s nullglob
  candidates=()
  for d in "$root_dir"/build/*/; do
    [ -f "$d/install_manifest.txt" ] && candidates+=("$d")
  done
  shopt -u nullglob
  if [ "${#candidates[@]}" -eq 0 ]; then
    echo "Error: no build dir with install_manifest.txt under $root_dir/build/." >&2
    echo "Hint: run --install first to produce a manifest." >&2
    exit 1
  fi
  # Newest by mtime of install_manifest.txt.
  uninstall_dir=""
  newest_ts=0
  for d in "${candidates[@]}"; do
    ts="$(stat -c '%Y' "$d/install_manifest.txt" 2>/dev/null || echo 0)"
    if [ "$ts" -gt "$newest_ts" ]; then
      newest_ts="$ts"
      uninstall_dir="${d%/}"
    fi
  done
  echo "Uninstalling from: ${uninstall_dir#"$root_dir/"}"
  manifest="$uninstall_dir/install_manifest.txt"
  # Probe whether any target file's parent directory needs elevation.
  needs_elevation=false
  while IFS= read -r f; do
    [ -z "$f" ] && continue
    target="${DESTDIR:-}$f"
    if [ -e "$target" ] || [ -L "$target" ]; then
      parent="$(dirname "$target")"
      if [ ! -w "$parent" ]; then
        needs_elevation=true
        break
      fi
    fi
  done <"$manifest"

  if $needs_elevation; then
    if command -v sudo >/dev/null 2>&1; then
      if [ -w /dev/tty ]; then printf '\r\033[2K' >/dev/tty || true; fi
      # SC2024: sudo's own stdin/stdout/stderr are what we want pointed at the
      # tty so the password prompt is visible. This is the priming step's
      # whole purpose.
      # shellcheck disable=SC2024
      sudo -v </dev/tty >/dev/tty 2>/dev/tty || true
      sudo -E cmake --build "$uninstall_dir" --target uninstall
    elif command -v doas >/dev/null 2>&1; then
      if [ -w /dev/tty ]; then printf '\r\033[2K' >/dev/tty || true; fi
      if [ -n "${DESTDIR:-}" ]; then
        doas env "DESTDIR=$DESTDIR" cmake --build "$uninstall_dir" --target uninstall </dev/tty
      else
        doas cmake --build "$uninstall_dir" --target uninstall </dev/tty
      fi
    else
      cmake --build "$uninstall_dir" --target uninstall
    fi
  else
    cmake --build "$uninstall_dir" --target uninstall
  fi
  exit 0
fi

# Output helpers
PROGRESS_TOTAL=0
PROGRESS_CUR=0
ALL_STEPS=()
NEXT_STEP_IDX=0
CURRENT_STEP_IDX=-1
CURRENT_STEP_DESC=""

# Failure state
FAILED=false
FAILED_RC=0
FAILED_DESC=""
FAILED_LOGFILE=""
FAILED_STEP_IDX=-1

# Soft-failure flags (non-fatal notices)
IWYU_FAILED=false
IWYU_SUGGESTED=false
CPPCHECK_WARNED=false
CLANG_FORMAT_ISSUES=false
# Kartend-gv2xq: set true when an enforcing --format-check requested the
# pinned clang-format but no matching binary was found — turns the formerly
# silent skip into a hard CI failure (see the fail-loud block at the end of
# the maintenance run).
CLANG_FORMAT_TOOL_MISSING=false
RAW_QLOG_WARNED=false
# Ran-vs-skipped record for the lint/static-analysis checks, printed as a summary
# at the end of --maintenance. The do_* helpers print "skipped: ..." and still
# return 0 when their tool is absent, so without this the gate looks green while
# actually running nothing.
MAINT_CHECK_SUMMARY=()

# Collect warnings to print at the end
COLLECTED_WARNINGS=""

# Kartend-ufjcq: nudge (non-fatally) when the project git hooks aren't installed.
# The hooks (clang-format + version-consistency) are opt-in, so a contributor who
# hasn't run .scripts/git-hooks/install.sh only learns about format/version drift
# from the slow maintenance-check CI job. Resolve the active hooks dir the same
# way install.sh does (honoring core.hooksPath, e.g. beads' .beads/hooks) and
# look for the project hook marker install.sh writes. Printed directly to stderr
# (not via warn(), whose COLLECTED_WARNINGS buffer is currently never flushed).
if command -v git >/dev/null 2>&1 && git -C "$root_dir" rev-parse --git-dir >/dev/null 2>&1; then
  _hooks_cfg="$(git -C "$root_dir" config --get core.hooksPath 2>/dev/null || true)"
  if [ -n "$_hooks_cfg" ]; then
    case "$_hooks_cfg" in
      /*) _hooks_dst="$_hooks_cfg" ;;
      *) _hooks_dst="$root_dir/$_hooks_cfg" ;;
    esac
  else
    _hooks_dst="$root_dir/.git/hooks"
  fi
  if ! grep -qs "KARTEND PROJECT HOOK" "$_hooks_dst/pre-commit" 2>/dev/null; then
    printf "%b\n" "${CYAN}[${YELLOW}WARN${CYAN}]${RESET} git hooks not installed — run .scripts/git-hooks/install.sh to catch clang-format / version issues before CI" >&2
  fi
  unset _hooks_cfg _hooks_dst
fi

# Track last-running logfile for improved exit diagnostics
LAST_RUN_LOG=""

trap on_exit EXIT

# Timestamp
TS_HUMAN="$(date '+%b-%d-%Y-%H%M%S')"

# Project metadata
target_name="kartend"
cmake_bin="cmake"
generator_args=()
if [ "$generator_preference" = "ninja" ]; then
  if command -v ninja >/dev/null 2>&1; then
    generator_args=(-G Ninja)
  else
    echo "Error: --ninja requested but ninja is not installed (or not in PATH)." >&2
    exit 1
  fi
elif [ "$generator_preference" = "make" ]; then
  generator_args=(-G "Unix Makefiles")
else
  # auto
  if command -v ninja >/dev/null 2>&1; then
    generator_args=(-G Ninja)
  fi
fi

build_marker_file=".kartend-build-dir"

########################################
# Maintenance build (CMake)
########################################
if $maintenance_build; then
  ALL_STEPS=(); NEXT_STEP_IDX=0; PROGRESS_CUR=0
  plan_step "Prepare build directory"
  plan_step "Python guardrail checks"
  plan_step "Configure"
  plan_step "Build"
  if $run_tests; then
    plan_step "Run tests"
  fi
  plan_step "Sanitize compile_commands"
  if $format_apply; then
    plan_step "clang-format (apply fixes)"
  fi
  if $format_check || ! $format_apply; then
    plan_step "clang-format check"
  fi
  if $apply_fixes; then
    plan_step "clang-tidy (apply fixes)"
    plan_step "Rebuild after clang-tidy fixes"
  fi
  if [ "$analysis_mode" != "off" ]; then
    plan_step "clang-tidy analysis"
  fi
  if [ "$analysis_mode" = "full" ]; then
    plan_step "IWYU analysis"
    plan_step "cppcheck analysis"
  fi
  plan_step "Heuristic duplicate checks"
  plan_step "Raw Qt logging guard"
  plan_step "Export symbols (nm)"
  if $make_reports; then
    plan_step "Assemble reports"
    plan_step "Remove linker map files"
  fi
  if $install_after_build; then
    plan_step "Install"
  fi
  if $make_archive; then
    plan_step "Stage files for archive"
    plan_step "Create archive"
    plan_step "Cleanup archive staging"
  fi
  if ! $keep_builds; then
    plan_step "Prune build directories"
  fi

  progress_clearline
  padded=$(printf "${CYAN}[*]${RESET} %-30s" "Building in MAINTENANCE mode")
  printf "%s${CYAN}[%02d${MAGENTA}/${CYAN}%02d]${RESET}\n" "$padded" 0 "$PROGRESS_TOTAL"
  build_type="maintenance"; build_dir="$(build_dir_for_mode "$build_type")"; logs_dir="$build_dir/logs"; QUIET=true

  prep_tmp_log="$(mktemp "$root_dir/build/${build_type}.prepare.XXXX.log")"
  run_step "Prepare build directory" "$prep_tmp_log" maybe_prepare_build_dir "$build_dir" "$logs_dir" "$build_type"
  mkdir -p "$logs_dir" && mv -f "$prep_tmp_log" "$logs_dir/prepare.log"

  # Python guardrail lints (do_python_guardrails, lib/quality.sh): the same
  # check-*.py set CI's script-lint job enforces. Build-independent and <1s,
  # so they run BEFORE Configure — a layering violation fails here in a
  # second instead of after the ~10-min maintenance build. run_quality_check
  # (not run_step) so a python3-less machine records "skipped" in the
  # ran-vs-skipped summary; the explicit exit below still makes real
  # findings fatal, matching CI.
  if ! run_quality_check "Python guardrail checks" "$logs_dir/python-guardrails.log" do_python_guardrails "$root_dir"; then
    err_msg "Python guardrail checks failed (layering / singleshot-comments / test-mapping / bd-id-leakage / required-checks). See ${logs_dir#"$root_dir/"}/python-guardrails.log"
    exit 1
  fi

  cmake_args=(
    -S "$root_dir"
    -B "$build_dir"
    "${generator_args[@]}"
    -DCMAKE_BUILD_TYPE=Release
    -DKARTEND_MAINTENANCE=ON
    # Portable flags: this is a lint gate, not a perf build. -march=native
    # under -Werror makes the gate depend on the host CPU — GitHub's runner
    # fleet grew AVX10.1 CPUs on 2026-07-18 and clang's promotion warning
    # (-Winvalid-feature-combination) broke every TU, while the same tree
    # passed locally. Every other CI compile job already sets this.
    -DKARTEND_PORTABLE_RELEASE=ON
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    -DCMAKE_C_COMPILER=clang
    -DCMAKE_CXX_COMPILER=clang++
    "-DBUILD_DATE=$TS_HUMAN"
    -DKARTEND_BUILD_TESTS=OFF
  )
  if [ ${#extra_cmake_args[@]} -gt 0 ]; then
    cmake_args+=("${extra_cmake_args[@]}")
  fi
  if $build_tests; then
    cmake_args+=(-DKARTEND_BUILD_TESTS=ON)
  fi
  if $coverage_build; then
    cmake_args+=(-DKARTEND_ENABLE_COVERAGE=ON)
  fi
  if $ccache_available; then
    cmake_args+=(
      -DCMAKE_C_COMPILER_LAUNCHER=ccache
      -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
    )
  fi
  if ! $use_ccache; then
    cmake_args+=(-DKARTEND_ENABLE_CCACHE=OFF)
  fi
  run_step "Configure" "$logs_dir/cmake_configure.log" "$cmake_bin" "${cmake_args[@]}"
  run_step "Build" "$logs_dir/cmake_build.log" "$cmake_bin" --build "$build_dir" -j"$build_jobs"
  if $run_tests; then
    run_step "Run tests" "$logs_dir/ctest.log" run_ctest "$build_dir"
  fi

  COMPDB_FILE="$build_dir/compile_commands.json"
  SANDBOX_COMPDB_DIR="$build_dir/cc-sanitized"
  run_step "Sanitize compile_commands" "$logs_dir/compdb_sanitize.log" bash <<EOF
$(declare -f sanitize_compdb)
mkdir -p "$SANDBOX_COMPDB_DIR"
sanitize_compdb "$COMPDB_FILE" "$SANDBOX_COMPDB_DIR/compile_commands.json"
EOF

  # Kartend-rqvp7: any in-place apply path (--format-apply or --apply-fixes)
  # rewrites tracked files. Surface the known-hazard warning in the script
  # itself (not just the docs) and refuse to run on a dirty tree so the diff
  # stays reviewable. Done once, up front, before either fixer runs — and as
  # a hard gate (not run_optional) so a dirty tree aborts before any rewrite.
  if $format_apply || $apply_fixes; then
    warn_apply_hazards
    if ! require_clean_tree_for_apply "$root_dir"; then
      err_msg "Aborting before in-place fixes: working tree is dirty. Commit/stash first, or pass --apply-fixes-dirty-ok."
      exit 1
    fi
  fi

  # clang-format operations
  if $format_apply; then
    run_optional "clang-format (apply fixes)" "$logs_dir/clang-format-apply.log" do_clang_format_apply "$root_dir/src" "$root_dir/tests"
  fi
  if $format_check || ! $format_apply; then
    # Kartend-gv2xq: when --format-check is set (CI), a missing pinned
    # clang-format makes do_clang_format return non-zero. run_quality_check
    # only warns on non-zero, so capture the rc here and fail hard below so
    # the gate can't pass while having run no formatter at all.
    CLANG_FORMAT_TOOL_MISSING=false
    if ! run_quality_check "clang-format check" "$logs_dir/clang-format.log" do_clang_format "$root_dir/src" "$root_dir/tests"; then
      if $format_check; then
        CLANG_FORMAT_TOOL_MISSING=true
      fi
    fi
    # Check for clang-format issues
    if [ -s "$logs_dir/clang-format.log" ]; then
      if grep -qE 'Format issue:|Found [0-9]+ files with formatting issues' "$logs_dir/clang-format.log"; then
        CLANG_FORMAT_ISSUES=true
      fi
    fi
  fi

  if $apply_fixes; then
    # Kartend-rqvp7: do_clang_tidy_apply_fixes derives its check set from
    # .clang-tidy (--config-file) so applied fixes match the project's
    # enabled-check policy and can't reintroduce style the config disables
    # (the old hardcoded list applied readability-braces-around-statements
    # and readability-implicit-bool-conversion, both OFF in .clang-tidy).
    # This $fix_checks list survives only as the fallback do_clang_tidy_apply_fixes
    # uses when no .clang-tidy is present; keep it free of config-disabled
    # checks so even the fallback honors the policy.
    fix_checks="modernize-use-nullptr,modernize-use-override"
    run_optional "clang-tidy (apply fixes)" "$logs_dir/clang-tidy-fixes.log" do_clang_tidy_apply_fixes "$COMPDB_FILE" "$root_dir/src" "$fix_checks"
    run_step "Rebuild after clang-tidy fixes" "$logs_dir/cmake_build_after_fixes.log" "$cmake_bin" --build "$build_dir" -j"$build_jobs"
  fi

  TIDY_PROMOTED_FAILED=false
  if [ "$analysis_mode" != "off" ]; then
    checks="-*,clang-analyzer-*,modernize-*,performance-*,readability-*,-readability-static-accessed-through-instance,google-*"
    # Kartend-z4ev0: do_clang_tidy honors KARTEND_TIDY_ONLY_FILES (validated at
    # startup) — CI sets it on pull_request events to tidy only the changed TUs;
    # unset (local runs, the nightly lint sweep) means the full src/ sweep.
    # CI push-to-main runs pass --analysis=off and skip this advisory pass in
    # favor of the workflow's enforcing curated-check clang-tidy gate.
    if ! run_quality_check "clang-tidy analysis" "$logs_dir/clang-tidy.log" do_clang_tidy "$COMPDB_FILE" "$root_dir/src" "$checks"; then
      # do_clang_tidy returns non-zero only when WarningsAsErrors-promoted
      # checks (.clang-tidy:WarningsAsErrors) trigger. The remaining advisory
      # warnings exit clang-tidy 0 so they don't fall into this branch.
      if grep -qE 'error:.*\[(bugprone|clang-analyzer)' "$logs_dir/clang-tidy.log"; then
        TIDY_PROMOTED_FAILED=true
      fi
    fi
  fi
  if [ "$analysis_mode" = "full" ]; then
    # IWYU is quality check; detect failure and suggestions for end-of-run notice
    if ! run_quality_check "IWYU analysis" "$logs_dir/iwyu.log" do_iwyu "$COMPDB_FILE" "$root_dir/src"; then
      IWYU_FAILED=true
    fi
    if [ -s "$logs_dir/iwyu.log" ]; then
      if grep -qE 'should add these lines:|should remove these lines:' "$logs_dir/iwyu.log"; then
        IWYU_SUGGESTED=true
      fi
    fi

    run_quality_check "cppcheck analysis" "$logs_dir/cppcheck.log" do_cppcheck "$root_dir/src" "$root_dir/tests"
    # Check for cppcheck warnings
    if [ -s "$logs_dir/cppcheck.log" ]; then
      if grep -qE ': (error|warning|style|performance|portability):' "$logs_dir/cppcheck.log"; then
        CPPCHECK_WARNED=true
      fi
    fi
  fi
  
  run_optional "Heuristic duplicate checks" "$logs_dir/dup-heuristics.log" bash -lc "grep -R --line-number -E \"QString .* = .*replace|completeBaseName|replace\\('_',' ')\" src || true"

  # Raw Qt logging guard: prefer qC* (categorized) over qDebug/qWarning/qInfo/qCritical.
  # Keep src/ free of raw logging sites so categories remain the single control point.
  run_quality_check "Raw Qt logging guard" "$logs_dir/raw-qlogging.log" bash -lc "grep -R --line-number -E '\\bq(Debug|Warning|Info|Critical)\\s*\\(\\s*\\)' src || true"
  if [ -s "$logs_dir/raw-qlogging.log" ]; then
    RAW_QLOG_WARNED=true
  fi
  
  run_optional "Export symbols (nm)" "$logs_dir/nm.log" do_export_symbols "$build_dir/$target_name" "$build_dir/nm-list.txt"

  if $make_reports; then
    run_step "Assemble reports" "$logs_dir/reports-assemble.log" assemble_reports
    run_optional "Remove linker map files" "$logs_dir/remove-map.log" bash -lc "rm -f \"$reports_dir/$target_name.map\" \"$reports_dir\"/$target_name-*.map"
  fi

  if $install_after_build; then
    run_step "Install" "$logs_dir/install.log" do_cmake_install "$cmake_bin" "$build_dir"
  fi

  abort_if_failed

  if $make_archive; then
    base="$(basename "$root_dir")"
    archive_name="${base}-${build_type}-${TS_HUMAN}"
    fname="${archive_name}.tar.gz"
    parent_dir="$(dirname "$root_dir")"
    archive_dir="${parent_dir}/${archive_name}"

    run_step "Stage files for archive" "$logs_dir/archive-stage.log" bash -lc "
      mkdir -p \"$backups_dir\" &&
      rm -rf \"$archive_dir\" &&
      mkdir -p \"$archive_dir\" &&
      cp -a \"$root_dir/src\" \"$archive_dir/\" &&
      cp -a \"$root_dir/CMakeLists.txt\" \"$archive_dir/\"
    "
    run_step "Create archive" "$logs_dir/archive-tar.log" tar -v -C "$parent_dir" -czf "${backups_dir}/${fname}" "$archive_name"
    run_optional "Cleanup archive staging" "$logs_dir/archive-clean.log" rm -rf "$archive_dir"
  fi
  if ! $keep_builds; then
    run_step "Prune build directories" "$logs_dir/prune-build-dirs.log" prune_other_builds "$root_dir/build" "$(basename "$build_dir")"
  fi

  step_final "Build completed successfully."
  publish_current_build_dir "$build_dir"
  if $make_archive; then
    step_final "Archive created: ${backups_dir}/${fname}"
  fi
  if [ "$IWYU_FAILED" = true ] || [ "$IWYU_SUGGESTED" = true ]; then
    rel_iwyu="${logs_dir#"$root_dir/"}/iwyu.log"
    step_final "Notice: IWYU reported suggestions. See ${rel_iwyu}"
  fi
  if [ -s "$logs_dir/clang-tidy.log" ]; then
    # Check for actual warnings (not just "Suppressed X warnings" lines)
    if grep -qE '^/.*\.(cpp|h):[0-9]+:[0-9]+: warning:' "$logs_dir/clang-tidy.log"; then
      rel_clang_tidy="${logs_dir#"$root_dir/"}/clang-tidy.log"
      step_final "Notice: clang-tidy reported warnings. See ${rel_clang_tidy}"
    fi
  fi

  if [ "$CPPCHECK_WARNED" = true ]; then
    rel_cppcheck="${logs_dir#"$root_dir/"}/cppcheck.log"
    step_final "Notice: cppcheck reported warnings. See ${rel_cppcheck}"
  fi
  if [ "$RAW_QLOG_WARNED" = true ]; then
    rel_raw_qlog="${logs_dir#"$root_dir/"}/raw-qlogging.log"
    step_final "Notice: raw qDebug/qWarning/qInfo/qCritical sites found in src/. Convert to qC*. See ${rel_raw_qlog}"
  fi
  if [ "$CLANG_FORMAT_ISSUES" = true ]; then
    rel_clang_format="${logs_dir#"$root_dir/"}/clang-format.log"
    step_final "Notice: clang-format reported formatting issues. See ${rel_clang_format}"
  fi
  if [ "${TIDY_PROMOTED_FAILED:-false}" = true ]; then
    rel_clang_tidy="${logs_dir#"$root_dir/"}/clang-tidy.log"
    step_final "Notice: clang-tidy promoted-error checks fired. See ${rel_clang_tidy}"
  fi

  # Ran-vs-skipped summary so a self-skipped tool (clang-format-19, clang-tidy,
  # cppcheck, iwyu absent) is visible instead of masquerading as a green gate.
  if [ "${#MAINT_CHECK_SUMMARY[@]}" -gt 0 ]; then
    _mc_ran=0
    _mc_skipped=0
    for _mc_entry in "${MAINT_CHECK_SUMMARY[@]}"; do
      if [ "${_mc_entry%%|*}" = "skip" ]; then
        _mc_skipped=$((_mc_skipped + 1))
      else
        _mc_ran=$((_mc_ran + 1))
      fi
    done
    step_final "Quality checks: ${_mc_ran} ran, ${_mc_skipped} skipped (tool absent)."
    if [ "$_mc_skipped" -gt 0 ]; then
      for _mc_entry in "${MAINT_CHECK_SUMMARY[@]}"; do
        [ "${_mc_entry%%|*}" = "skip" ] || continue
        _mc_rest="${_mc_entry#*|}"
        step_final "  - skipped: ${_mc_rest%%|*} (${_mc_rest#*|})"
      done
      step_final "  Install the missing tooling (see docs/dev/building.md) so the gate runs locally, not only in CI."
    fi
  fi

  # When --format-check was explicitly requested (e.g. CI), fail hard on
  # clang-format drift OR on any clang-tidy WarningsAsErrors-promoted
  # finding. The promoted set in .clang-tidy:WarningsAsErrors covers only
  # bugprone/clang-analyzer subchecks that are verified-clean in the current
  # baseline; other tidy categories remain advisory until
  # their baselines are cleared.
  if [ "$format_check" = true ] && [ "$CLANG_FORMAT_TOOL_MISSING" = true ]; then
    err_msg "clang-format check failed: no clang-format-${KARTEND_CLANG_FORMAT_VERSION} found, so the format gate ran nothing. Install the pinned formatter (apt.llvm.org) or run the check through the kartend-ci container — see build/*/logs/clang-format.log and docs/dev/building.md."
    exit 1
  fi
  if [ "$format_check" = true ] && [ "$CLANG_FORMAT_ISSUES" = true ]; then
    err_msg "clang-format check failed: formatting drift detected. Run '.scripts/build.sh --maintenance --format-apply' locally and recommit."
    exit 1
  fi
  if [ "$format_check" = true ] && [ "${TIDY_PROMOTED_FAILED:-false}" = true ]; then
    err_msg "clang-tidy check failed: a promoted bugprone-* / clang-analyzer-* check fired. See build/ninja-maintenance/logs/clang-tidy.log and either fix the finding or scope WarningsAsErrors more narrowly."
    exit 1
  fi
  exit 0
fi

########################################
# Regular build: release or debug (CMake)
########################################
if $sanitize_build; then
  ALL_STEPS=(); NEXT_STEP_IDX=0; PROGRESS_CUR=0
  plan_step "Prepare build directory"
  plan_step "Configure"
  plan_step "Build"
  if $run_tests; then
    plan_step "Run tests"
  fi
  if $make_reports; then
    plan_step "Assemble reports"
  fi
  if $install_after_build; then
    plan_step "Install"
  fi
  if $make_archive; then
    plan_step "Stage files for archive"
    plan_step "Create archive"
    plan_step "Cleanup archive staging"
  fi
  if ! $keep_builds; then
    plan_step "Prune build directories"
  fi

  progress_clearline
  printf "${CYAN}[*]${RESET} %-30s${CYAN}[%02d${MAGENTA}/${CYAN}%02d]${RESET}\n" "Building in SANITIZE mode" 0 "$PROGRESS_TOTAL"
  build_type="sanitize"; debug_build=true; QUIET=true

elif $debug_build && ! $pgo_build; then
  ALL_STEPS=(); NEXT_STEP_IDX=0; PROGRESS_CUR=0
  plan_step "Prepare build directory"
  plan_step "Configure"
  plan_step "Build"
  if $run_tests; then
    plan_step "Run tests"
  fi
  if $make_reports; then
    plan_step "Assemble reports"
  fi
  if $install_after_build; then
    plan_step "Install"
  fi
  if $make_archive; then
    plan_step "Stage files for archive"
    plan_step "Create archive"
    plan_step "Cleanup archive staging"
  fi
  if ! $keep_builds; then
    plan_step "Prune build directories"
  fi

  progress_clearline
  printf "${CYAN}[*]${RESET} %-30s${CYAN}[%02d${MAGENTA}/${CYAN}%02d]${RESET}\n" "Building in DEBUG mode" 0 "$PROGRESS_TOTAL"
  build_type="debug"; QUIET=true
elif $relwithdebinfo_build && ! $pgo_build; then
  ALL_STEPS=(); NEXT_STEP_IDX=0; PROGRESS_CUR=0
  plan_step "Prepare build directory"
  plan_step "Configure"
  plan_step "Build"
  if $run_tests; then
    plan_step "Run tests"
  fi
  if $make_reports; then
    plan_step "Assemble reports"
  fi
  if $install_after_build; then
    plan_step "Install"
  fi
  if $make_archive; then
    plan_step "Stage files for archive"
    plan_step "Create archive"
    plan_step "Cleanup archive staging"
  fi
  if ! $keep_builds; then
    plan_step "Prune build directories"
  fi

  progress_clearline
  printf "${CYAN}[*]${RESET} %-30s${CYAN}[%02d${MAGENTA}/${CYAN}%02d]${RESET}\n" "Building in RELWITHDEBINFO mode" 0 "$PROGRESS_TOTAL"
  build_type="relwithdebinfo"; QUIET=true
elif ! $pgo_build; then
  ALL_STEPS=(); NEXT_STEP_IDX=0; PROGRESS_CUR=0
  plan_step "Prepare build directory"
  plan_step "Configure"
  plan_step "Build"
  if $run_tests; then
    plan_step "Run tests"
  fi
  plan_step "Strip binaries"
  if $make_reports; then
    plan_step "Assemble reports"
    plan_step "Remove linker map files"
  fi
  if $install_after_build; then
    plan_step "Install"
  fi
  if $make_archive; then
    plan_step "Stage files for archive"
    plan_step "Create archive"
    plan_step "Cleanup archive staging"
  fi
  if ! $keep_builds; then
    plan_step "Prune build directories"
  fi

  progress_clearline
  printf "${CYAN}[*]${RESET} %-30s${CYAN}[%02d${MAGENTA}/${CYAN}%02d]${RESET}\n" "Building in RELEASE mode" 0 "$PROGRESS_TOTAL"
  build_type="release"; QUIET=true
fi

########################################
# PGO build
########################################
if $pgo_build; then
  ALL_STEPS=(); NEXT_STEP_IDX=0; PROGRESS_CUR=0
  plan_step "Prepare build directory"
  plan_step "Configure PGO generate"
  plan_step "Build PGO generate"
  plan_step "Configure PGO use"
  plan_step "Build PGO use"
  plan_step "Rename build directory"
  plan_step "Strip binaries"
  if $run_tests; then
    plan_step "Run tests"
  fi
  if $make_reports; then
    plan_step "Assemble reports"
  fi
  if $install_after_build; then
    plan_step "Install"
  fi
  if $make_archive; then
    plan_step "Stage files for archive"
    plan_step "Create archive"
    plan_step "Cleanup archive staging"
  fi
  if ! $keep_builds; then
    plan_step "Prune build directories"
  fi

  progress_clearline
  padded=$(printf "${CYAN}[*]${RESET} %-30s" "Building in PGO mode")
  printf "%s${CYAN}[%02d${MAGENTA}/${CYAN}%02d]${RESET}\n" "$padded" 0 "$PROGRESS_TOTAL"
  
  build_type="release"
  build_dir="$(build_dir_for_mode "$build_type")"
  logs_dir="$build_dir/logs"
  QUIET=true

  # Prepare build dir
  prep_tmp_log="$(mktemp "$root_dir/build/${build_type}.prepare.XXXX.log")"
  run_step "Prepare build directory" "$prep_tmp_log" maybe_prepare_build_dir "$build_dir" "$logs_dir" "$build_type"
  mkdir -p "$logs_dir" && mv -f "$prep_tmp_log" "$logs_dir/prepare.log"

  # Configure for PGO generate
  cmake_args=(
    -S "$root_dir"
    -B "$build_dir"
    "${generator_args[@]}"
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    "-DBUILD_DATE=$TS_HUMAN"
    -DCMAKE_C_COMPILER=clang
    -DCMAKE_CXX_COMPILER=clang++
    -DKARTEND_USE_PGO=ON
    -DKARTEND_PGO_GENERATE=ON
    -DKARTEND_BUILD_TESTS=OFF
  )
  if [ ${#extra_cmake_args[@]} -gt 0 ]; then
    cmake_args+=("${extra_cmake_args[@]}")
  fi
  if $build_tests; then
    cmake_args+=(-DKARTEND_BUILD_TESTS=ON)
  fi
  if $coverage_build; then
    cmake_args+=(-DKARTEND_ENABLE_COVERAGE=ON)
  fi
  if $ccache_available; then
    cmake_args+=(
      -DCMAKE_C_COMPILER_LAUNCHER=ccache
      -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
    )
  fi
  if ! $use_ccache; then
    cmake_args+=(-DKARTEND_ENABLE_CCACHE=OFF)
  fi
  # Kartend-oizkj: PGO needs an interactive pause between the generate and
  # use passes. Detect a no-TTY environment UP FRONT and fail with a clear
  # message, instead of burning the full generate build and then aborting at
  # the bare `read -r` under set -e. A real open attempt on /dev/tty — not
  # [ -r ], which only checks permission bits on a node that exists even
  # without a controlling terminal.
  if [ ! -t 0 ] && ! { exec 3</dev/tty; } 2>/dev/null; then
    echo "Error: --pgo requires an interactive terminal (the mid-PGO pause reads from stdin or /dev/tty)." >&2
    exit 1
  fi
  exec 3<&- 2>/dev/null || true

  # Wipe any stale .profraw files from a prior --pgo run so the user's
  # instrumented exercise produces a fresh profile set. Without this, an
  # incremental rebuild keeps the previous run's .profraw files alongside
  # the new ones, and the PGO use pass would merge biased data into the
  # optimised build.
  rm -rf "$build_dir/pgo_profiles"
  run_step "Configure PGO generate" "$logs_dir/configure.log" cmake "${cmake_args[@]}"
  # Kartend-oizkj: honor --jobs like every other build mode (matters on hosts
  # that must keep cores free, e.g. while streaming).
  run_step "Build PGO generate" "$logs_dir/build.log" cmake --build "$build_dir" -j"$build_jobs"

  # Prompt for manual run (force read from TTY for VS Code tasks/non-interactive shells)
  echo "Run \"${build_dir}/${target_name}\" now, exercise the app, then exit it."
  if [ -t 0 ]; then
    read -r -p "Press Enter to continue PGO..."
  else
    # Fallback to TTY if stdin isn't interactive
    if [ -r /dev/tty ]; then
      read -r -p "Press Enter to continue PGO..." </dev/tty || read -r
    else
      read -r
    fi
  fi

  # Reconfigure for PGO use
  cmake_args=(
    -S "$root_dir"
    -B "$build_dir"
    "${generator_args[@]}"
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    "-DBUILD_DATE=$TS_HUMAN"
    -DCMAKE_C_COMPILER=clang
    -DCMAKE_CXX_COMPILER=clang++
    -DKARTEND_USE_PGO=ON
    -DKARTEND_PGO_USE=ON
    -DKARTEND_BUILD_TESTS=OFF
  )
  if [ ${#extra_cmake_args[@]} -gt 0 ]; then
    cmake_args+=("${extra_cmake_args[@]}")
  fi
  if $build_tests; then
    cmake_args+=(-DKARTEND_BUILD_TESTS=ON)
  fi
  if $coverage_build; then
    cmake_args+=(-DKARTEND_ENABLE_COVERAGE=ON)
  fi
  if $ccache_available; then
    cmake_args+=(
      -DCMAKE_C_COMPILER_LAUNCHER=ccache
      -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
    )
  fi
  if ! $use_ccache; then
    cmake_args+=(-DKARTEND_ENABLE_CCACHE=OFF)
  fi
  run_step "Configure PGO use" "$logs_dir/configure2.log" cmake "${cmake_args[@]}"
  run_step "Build PGO use" "$logs_dir/build2.log" cmake --build "$build_dir" -j"$build_jobs"

  # Rename build directory
  new_build_dir="$root_dir/build/$(generator_tag)-release-pgo"
  run_step "Rename build directory" "$root_dir/build/rename.log" bash -lc "mv \"$build_dir\" \"$new_build_dir\""
  build_dir="$new_build_dir"
  build_type="release-pgo"
  logs_dir="$build_dir/logs"
  mv -f "$root_dir/build/rename.log" "$logs_dir/rename.log"

  # Strip
  if command -v strip >/dev/null 2>&1; then
    run_step "Strip binaries" "$logs_dir/strip.log" bash -lc 'find "'"$build_dir"'" -maxdepth 1 -type f -executable -print0 | xargs -0 -r strip --strip-all'
  fi

  if $run_tests; then
    run_step "Run tests" "$logs_dir/ctest.log" run_ctest "$build_dir"
  fi

  # Reports
  if $make_reports; then
    run_step "Assemble reports" "$logs_dir/reports-assemble.log" assemble_reports
  fi

  if $install_after_build; then
    run_step "Install" "$logs_dir/install.log" do_cmake_install cmake "$build_dir"
  fi

  abort_if_failed

  # Archive
  if $make_archive; then
    base="$(basename "$root_dir")"
    archive_name="${base}-${build_type}-${TS_HUMAN}"
    fname="${archive_name}.tar.gz"
    archive_dir="${parent_dir}/${archive_name}"
    run_step "Stage files for archive" "$logs_dir/archive-stage.log" bash -lc "
      mkdir -p \"$backups_dir\" &&
      mkdir -p \"$archive_dir\"
      cp -r \"$root_dir\"/* \"$archive_dir\"/ 2>/dev/null || true
      rm -rf \"$archive_dir\"/build \"$archive_dir\"/reports \"$archive_dir\"/.git \"$archive_dir\"/.github
    "
    run_step "Create archive" "$logs_dir/archive-tar.log" tar -v --exclude='build' --exclude='reports' --exclude='.*' -C "$parent_dir" -czf "${backups_dir}/${fname}" "$archive_name"
    run_optional "Cleanup archive staging" "$logs_dir/archive-clean.log" rm -rf "$archive_dir"
  fi

# Prune other build dirs (keep only current)
if ! $keep_builds; then
  run_step "Prune build directories" "$logs_dir/prune-build-dirs.log" prune_other_builds "$root_dir/build" "$(basename "$build_dir")"
fi

step_final "Build completed successfully."
publish_current_build_dir "$build_dir"
if $make_archive; then
  step_final "Archive created: ${backups_dir}/${fname}"
fi
exit 0
fi

if ! $pgo_build; then
  build_dir="$(build_dir_for_mode "$build_type")"; logs_dir="$build_dir/logs"

  # Prepare build dir (temp log -> logs/)
  prep_tmp_log="$(mktemp "$root_dir/build/${build_type}.prepare.XXXX.log")"
  run_step "Prepare build directory" "$prep_tmp_log" maybe_prepare_build_dir "$build_dir" "$logs_dir" "$build_type"
  mkdir -p "$logs_dir" && mv -f "$prep_tmp_log" "$logs_dir/prepare.log"

  # Configure and build
  case "$build_type" in
    debug|sanitize) cmake_build_type="Debug" ;;
    relwithdebinfo) cmake_build_type="RelWithDebInfo" ;;
    *)              cmake_build_type="Release" ;;
  esac
  cmake_args=(
    -S "$root_dir"
    -B "$build_dir"
    "${generator_args[@]}"
    "-DCMAKE_BUILD_TYPE=$cmake_build_type"
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    "-DBUILD_DATE=$TS_HUMAN"
    -DKARTEND_BUILD_TESTS=OFF
    -DKARTEND_LINKER_MAP=ON
  )
  if [ ${#extra_cmake_args[@]} -gt 0 ]; then
    cmake_args+=("${extra_cmake_args[@]}")
  fi
  if $build_tests; then
    cmake_args+=(-DKARTEND_BUILD_TESTS=ON)
  fi
  if $coverage_build; then
    cmake_args+=(-DKARTEND_ENABLE_COVERAGE=ON)
  fi
  if $ccache_available; then
    cmake_args+=(
      -DCMAKE_C_COMPILER_LAUNCHER=ccache
      -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
    )
  fi
  if ! $use_ccache; then
    cmake_args+=(-DKARTEND_ENABLE_CCACHE=OFF)
  fi
  if $force_clang && { [ "$build_type" = "release" ] || [ "$build_type" = "relwithdebinfo" ]; }; then
    if ! command -v clang >/dev/null 2>&1 || ! command -v clang++ >/dev/null 2>&1; then
      echo "Error: --clang requested but clang/clang++ not found in PATH." >&2
      exit 1
    fi
    cmake_args+=(-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++)
  fi
  if $pgo_generate || $pgo_use; then
    cmake_args+=(-DKARTEND_USE_PGO=ON)
    if $pgo_generate; then
      cmake_args+=(-DKARTEND_PGO_GENERATE=ON)
      # Standalone --pgo-generate path: wipe stale .profraw before instrumented
      # configure so the upcoming user run produces only fresh profiles.
      # Matches the cleanup the combined --pgo flow does just before its
      # "Configure PGO generate" step.
      rm -rf "$build_dir/pgo_profiles"
    elif $pgo_use; then
      cmake_args+=(-DKARTEND_PGO_USE=ON)
    fi
  fi

  if [ "$build_type" = "sanitize" ]; then
    cmake_args+=(-DKARTEND_ENABLE_SANITIZERS=ON)
  fi

  # Configure
  run_step "Configure" "$logs_dir/cmake_configure.log" "$cmake_bin" "${cmake_args[@]}"
  # Build
  run_step "Build" "$logs_dir/cmake_build.log" "$cmake_bin" --build "$build_dir" -j"$build_jobs"

  if $run_tests; then
    run_step "Run tests" "$logs_dir/ctest.log" run_ctest "$build_dir"
  fi

  # Strip in release
  if [ "$build_type" = "release" ] && command -v strip >/dev/null 2>&1; then
    run_step "Strip binaries" "$logs_dir/strip.log" bash -lc 'find "'"$build_dir"'" -maxdepth 1 -type f -executable -print0 | xargs -0 -r strip --strip-all'
  fi

  # Reports
  if $make_reports; then
    run_step "Assemble reports" "$logs_dir/reports-assemble.log" assemble_reports

    # Remove map files for non-debug
    if [ "$build_type" != "debug" ]; then
      run_optional "Remove linker map files" "$logs_dir/remove-map.log" bash -lc "rm -f \"$reports_dir/$target_name.map\" \"$reports_dir\"/$target_name-*.map"
    fi
  fi

  if $install_after_build; then
    run_step "Install" "$logs_dir/install.log" do_cmake_install "$cmake_bin" "$build_dir"
  fi

  abort_if_failed

  # Archive (exclude build & reports)
  if $make_archive; then
    base="$(basename "$root_dir")"
    archive_name="${base}-${build_type}-${TS_HUMAN}"
    fname="${archive_name}.tar.gz"
    parent_dir="$(dirname "$root_dir")"
    archive_dir="${parent_dir}/${archive_name}"

    run_step "Stage files for archive" "$logs_dir/archive-stage.log" bash -lc "
      mkdir -p \"$backups_dir\" &&
      rm -rf \"$archive_dir\" &&
      mkdir -p \"$archive_dir\" &&
      if command -v rsync >/dev/null 2>&1; then
        rsync -a --delete --itemize-changes --exclude='.git' --exclude='build' --exclude='reports' --exclude='.*' ./ \"$archive_dir\"/
      else
        # Kartend-oizkj: the old rm -rf \"\$dir\"/.* glob expanded to . and ..,
        # which GNU rm refuses (non-zero exit), aborting the whole build via
        # run_step on exactly the rsync-less systems this fallback exists for.
        cp -av . \"$archive_dir\"/ &&
        rm -rf \"$archive_dir\"/build \"$archive_dir\"/reports &&
        find \"$archive_dir\" -maxdepth 1 -name '.*' ! -name '.' ! -name '..' -exec rm -rf {} +
      fi
    "
    run_step "Create archive" "$logs_dir/archive-tar.log" tar -v --exclude='build' --exclude='reports' --exclude='.*' -C "$parent_dir" -czf "${backups_dir}/${fname}" "$archive_name"
    run_optional "Cleanup archive staging" "$logs_dir/archive-clean.log" rm -rf "$archive_dir"
  fi

  # Prune other build dirs (keep only current)
  if ! $keep_builds; then
    run_step "Prune build directories" "$logs_dir/prune-build-dirs.log" prune_other_builds "$root_dir/build" "$(basename "$build_dir")"
  fi

  step_final "Build completed successfully."
  publish_current_build_dir "$build_dir"
  if $make_archive; then
    step_final "Archive created: ${backups_dir}/${fname}"
  fi
  exit 0
fi
