#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: .scripts/build.sh [options]

Build modes (mutually exclusive):
  --debug           Debug build (keeps qDebug/qWarning output)
  --relwithdebinfo  Release build with debug symbols (for profiling)
  --sanitize        Sanitizer build (Debug + sanitizers)
  --maintenance     Release build + static analysis helpers
  --pgo             Two-pass PGO build (generate + use)
  --pgo-generate    Configure/build PGO generate pass only
  --pgo-use         Configure/build PGO use pass only

Build options:
  --tests           Configure with -DBUILD_TESTS=ON
  --run-tests       Run ctest after a successful build (requires --tests)
  --ninja           Force Ninja generator (if available)
  --make            Force Unix Makefiles generator
  --incremental     Reuse existing build directory (don't rm -rf it) (default)
  --clean           Remove build directory before configuring
  --no-archive      Skip creating the source archive (.backups/*.tar.gz)
  --no-reports      Skip assembling source/UI reports into .backups/reports
  --fast            Shorthand for --no-archive --no-reports
  --keep-builds     Don't prune other build directories
  --no-ccache       Disable ccache launcher even if installed

Maintenance-only:
  --apply-fixes      Apply safe clang-tidy fixes (requires --maintenance)
  --format-check     Run clang-format check (requires --maintenance)
  --format-apply     Apply clang-format (requires --maintenance)

Other:
  -h, --help        Show this help
EOF
}

# Parse args
debug_build=false
relwithdebinfo_build=false
sanitize_build=false
maintenance_build=false
apply_fixes=false
format_check=false
format_apply=false
pgo_generate=false
pgo_use=false
pgo_build=false
keep_builds=false
use_ccache=true
build_tests=false
run_tests=false
generator_preference="auto"  # auto|ninja|make
incremental_build=true
make_archive=true
make_reports=true
for arg in "${@:-}"; do
  case "$arg" in
    -h|--help) usage; exit 0 ;;
    --debug)       debug_build=true ;;
    --relwithdebinfo) relwithdebinfo_build=true ;;
    --sanitize|--sanitizers) sanitize_build=true ;;
    --maintenance) maintenance_build=true ;;
    --apply-fixes) apply_fixes=true ;;
    --format-check) format_check=true ;;
    --format-apply) format_apply=true ;;
    --tests)       build_tests=true ;;
    --run-tests)   run_tests=true ;;
    --ninja)       generator_preference="ninja" ;;
    --make)        generator_preference="make" ;;
    --incremental) incremental_build=true ;;
    --clean)       incremental_build=false ;;
    --no-archive)  make_archive=false ;;
    --no-reports)  make_reports=false ;;
    --fast)        make_archive=false; make_reports=false ;;
    --pgo-generate) pgo_generate=true ;;
    --pgo-use)     pgo_use=true ;;
    --pgo)         pgo_build=true ;;
    --keep-builds) keep_builds=true ;;
    --no-ccache)   use_ccache=false ;;
    *) ;;
  esac
done
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
if $format_apply && ! $maintenance_build; then
  echo "Error: --format-apply is only supported with --maintenance."
  exit 1
fi
if $format_check && ! $maintenance_build; then
  echo "Error: --format-check is only supported with --maintenance."
  exit 1
fi

if $run_tests && ! $build_tests; then
  echo "Error: --run-tests requires --tests (BUILD_TESTS=ON)."
  exit 1
fi

# Paths
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
root_dir="$(cd "$script_dir/.." && pwd)"
cd "$root_dir"
mkdir -p build
parent_dir="$(dirname "$root_dir")"
backups_dir="$parent_dir/.backups"
mkdir -p "$backups_dir"
reports_dir="$backups_dir/reports"

# Colors
setup_colors() {
  if [ -t 1 ] && command -v tput >/dev/null 2>&1 && [ "$(tput colors || echo 0)" -ge 8 ]; then
    RESET="$(tput sgr0)"; BOLD="$(tput bold)"
    RED="$(tput setaf 1)"; GREEN="$(tput setaf 2)"; YELLOW="$(tput setaf 3)"
    BLUE="$(tput setaf 4)"; MAGENTA="$(tput setaf 5)"; CYAN="$(tput setaf 6)"
  else
    RESET=""; BOLD=""; RED=""; GREEN=""; YELLOW=""; BLUE=""; MAGENTA=""; CYAN=""
  fi
}
setup_colors

# Optional build accelerators
ccache_available=false
if $use_ccache && command -v ccache >/dev/null 2>&1; then
  ccache_available=true
fi

# Output helpers
PROGRESS_TOTAL=0
PROGRESS_CUR=0
ALL_STEPS=()
NEXT_STEP_IDX=0
CURRENT_STEP_IDX=-1
CURRENT_STEP_DESC=""
EXTRA_TAB=""

# Failure state
FAILED=false
FAILED_RC=0
FAILED_DESC=""
FAILED_LOGFILE=""
FAILED_STEP_IDX=-1

# Soft-failure flags (non-fatal notices)
CLANG_TIDY_FAILED=false
IWYU_FAILED=false
IWYU_SUGGESTED=false
CPPCHECK_WARNED=false
CLANG_FORMAT_ISSUES=false

# Collect warnings to print at the end
COLLECTED_WARNINGS=""

# Track last-running logfile for improved exit diagnostics
LAST_RUN_LOG=""

progress_clearline() { printf "\r\033[2K"; }
# Print step with counts aligned; no extra tab
step() { progress_clearline; printf "${CYAN}[${MAGENTA}*${CYAN}]${RESET} %-30s${CYAN}[%02d${MAGENTA}/${CYAN}%02d]${RESET}" "$*" "$PROGRESS_CUR" "$PROGRESS_TOTAL"; }
step_completed() { progress_clearline; printf "${CYAN}[*]${RESET} %-30s${CYAN}[%02d${MAGENTA}/${CYAN}%02d]${RESET}\n" "$*" "$PROGRESS_CUR" "$PROGRESS_TOTAL"; }
step_final() {
  progress_clearline
  if [[ "$*" == Build\ completed\ successfully.* ]]; then
    echo -e "${CYAN}[${GREEN}✓${CYAN}]${RESET} $*"
  elif [[ "$*" == Archive\ created:* ]]; then
    echo -e "${CYAN}[${GREEN}✓${CYAN}]${RESET} $*"
  else
    echo -e "${CYAN}[${YELLOW}*${CYAN}]${RESET} $*"
  fi
}
warn() { 
  if [ -n "$COLLECTED_WARNINGS" ]; then
    COLLECTED_WARNINGS="${COLLECTED_WARNINGS}\n"
  fi
  COLLECTED_WARNINGS="${COLLECTED_WARNINGS}${CYAN}[${YELLOW}WARN${CYAN}]${RESET} $*"
}
err_msg() { progress_clearline; echo -e "${CYAN}[${RED}ERROR${CYAN}]${RESET} $*" >&2; }

# Failure summary printed at script exit
on_exit() {
  local rc=$?
  if [ $rc -ne 0 ]; then
    progress_clearline
    printf "\n" >&2
    if $FAILED; then
      printf "%b\n" "${CYAN}[${RED}ERROR${CYAN}]${RESET} Build failed at step $((FAILED_STEP_IDX+1))/${PROGRESS_TOTAL}: ${FAILED_DESC}" >&2
      if [ -n "$FAILED_LOGFILE" ]; then
        local rel="${FAILED_LOGFILE#"$root_dir/"}"
        printf "%b\n" "${RED}Log file:${RESET} ${rel}" >&2
      fi
      if [ $((FAILED_STEP_IDX+1)) -lt $PROGRESS_TOTAL ]; then
        printf "%b\n" "${RED}Remaining steps that weren't performed:${RESET}" >&2
        local i
        for (( i=FAILED_STEP_IDX+1; i<PROGRESS_TOTAL; i++ )); do
          printf " - %s\n" "${ALL_STEPS[$i]}" >&2
        done
      fi
    else
      printf "%b\n" "${CYAN}[${RED}ERROR${CYAN}]${RESET} Script exited with code ${rc}." >&2
      if [ -n "$CURRENT_STEP_DESC" ]; then
        printf "%b\n" "${RED}While running step:${RESET} ${CURRENT_STEP_DESC} (index $((CURRENT_STEP_IDX+1))/${PROGRESS_TOTAL})" >&2
      fi
      if [ -n "$LAST_RUN_LOG" ] && [ -f "$LAST_RUN_LOG" ]; then
        printf "%b\n" "${RED}Recent log (tail):${RESET} ${LAST_RUN_LOG#"$root_dir/"}" >&2
        tail -n 200 "$LAST_RUN_LOG" >&2 || true
      fi
    fi
  fi
}
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
    # Safety: only prune build dirs created by this script.
    if [ -f "$d/$build_marker_file" ]; then
      rm -rf -- "$d"
    fi
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

run_ctest() {
  local dir="$1"
  ctest --test-dir "$dir" --output-on-failure
}

# Step planning
plan_step() { ALL_STEPS+=("$1"); PROGRESS_TOTAL=${#ALL_STEPS[@]}; }
mark_next_step() { CURRENT_STEP_DESC="$1"; CURRENT_STEP_IDX="$NEXT_STEP_IDX"; NEXT_STEP_IDX=$((NEXT_STEP_IDX + 1)); }

# Critical step: fails immediately on error
run_step() {
  local desc="$1"; shift
  local logfile="$1"; shift
  local -a cmd=( "$@" )
  mark_next_step "$desc"
  LAST_RUN_LOG="$logfile"
  mkdir -p "$(dirname "$logfile")"
  PROGRESS_CUR=$((PROGRESS_CUR + 1))
  step "$desc"

  local rc tmp; tmp="$(mktemp)"
  if ${QUIET:-true}; then
    (
      set +e
      "${cmd[@]}" >"$tmp" 2>&1
      rc=$?
      if [ ! -s "$tmp" ]; then echo "(no output)" >"$logfile"; else cat "$tmp" >"$logfile"; fi
      exit $rc
    )
    rc=$?
  else
    set +e
    "${cmd[@]}" 2>&1 | tee "$tmp"
    rc=${PIPESTATUS[0]}
    set -e
    if [ ! -s "$tmp" ]; then echo "(no output)" >"$logfile"; else cat "$tmp" >"$logfile"; fi
  fi
  rm -f "$tmp"

  if [ $rc -ne 0 ]; then
    err_msg "$desc failed. See ${logfile#"$root_dir/"}"
    FAILED=true
    FAILED_RC=$rc
    FAILED_DESC="$desc"
    FAILED_LOGFILE="$logfile"
    FAILED_STEP_IDX="$CURRENT_STEP_IDX"
    exit "$rc"
  fi
  step_completed "$desc"
}

# Quality check: warns on failure but continues (for code analysis tools)
run_quality_check() {
  local desc="$1"; shift
  local logfile="$1"; shift
  local -a cmd=( "$@" )
  mark_next_step "$desc"
  LAST_RUN_LOG="$logfile"
  mkdir -p "$(dirname "$logfile")"
  PROGRESS_CUR=$((PROGRESS_CUR + 1))
  step "$desc"

  local rc tmp; tmp="$(mktemp)"
  if ${QUIET:-true}; then
    (
      set +e
      "${cmd[@]}" >"$tmp" 2>&1
      rc=$?
      if [ ! -s "$tmp" ]; then echo "(no output)" >"$logfile"; else cat "$tmp" >"$logfile"; fi
      exit $rc
    )
    rc=$?
  else
    set +e
    "${cmd[@]}" 2>&1 | tee "$tmp"
    rc=${PIPESTATUS[0]}
    set -e
    if [ ! -s "$tmp" ]; then echo "(no output)" >"$logfile"; else cat "$tmp" >"$logfile"; fi
  fi
  rm -f "$tmp"

  if [ $rc -ne 0 ]; then
    warn "$desc reported issues. See ${logfile#"$root_dir/"}"
  else
    step_completed "$desc"
  fi
  return $rc
}

# Optional step: warns on failure, doesn't affect build success
run_optional() {
  local desc="$1"; shift
  local logfile="$1"; shift
  local -a cmd=( "$@" )
  mark_next_step "$desc"
  LAST_RUN_LOG="$logfile"
  mkdir -p "$(dirname "$logfile")"
  PROGRESS_CUR=$((PROGRESS_CUR + 1))
  step "$desc"

  local rc tmp; tmp="$(mktemp)"
  if ${QUIET:-true}; then
    (
      set +e
      "${cmd[@]}" >"$tmp" 2>&1
      rc=$?
      if [ ! -s "$tmp" ]; then echo "(no output)" >"$logfile"; else cat "$tmp" >"$logfile"; fi
      exit $rc
    )
    rc=$?
  else
    set +e
    "${cmd[@]}" 2>&1 | tee "$tmp"
    rc=${PIPESTATUS[0]}
    set -e
    if [ ! -s "$tmp" ]; then echo "(no output)" >"$logfile"; else cat "$tmp" >"$logfile"; fi
  fi
  rm -f "$tmp"

  if [ $rc -ne 0 ]; then
    warn "$desc skipped or failed. See ${logfile#"$root_dir/"}"
  else
    step_completed "$desc"
  fi
  return $rc
}

abort_if_failed() {
  if [ "$FAILED" = true ]; then
    err_msg "Aborting: previous step $((FAILED_STEP_IDX+1)) failed: ${FAILED_DESC}"
    if [ -n "$FAILED_LOGFILE" ]; then
      local rel="${FAILED_LOGFILE#"$root_dir/"}"
      err_msg "See log: ${rel}"
    fi
    exit "${FAILED_RC:-1}"
  fi
}

# Assemble cpp/h/ui reports
assemble_reports() {
  mkdir -p "$reports_dir"
  : > "$reports_dir/cpp.txt"
  : > "$reports_dir/h.txt"
  : > "$reports_dir/ui.txt"
  find "$root_dir/src" -type f -name '*.cpp' -print0 | while IFS= read -r -d '' f; do
    printf "=== %s ===\n" "$f" >> "$reports_dir/cpp.txt"; cat "$f" >> "$reports_dir/cpp.txt"
  done
  find "$root_dir/src" -type f -name '*.h' -print0 | while IFS= read -r -d '' f; do
    printf "=== %s ===\n" "$f" >> "$reports_dir/h.txt"; cat "$f" >> "$reports_dir/h.txt"
  done
  find "$root_dir/src" -type f -name '*.ui' -print0 | while IFS= read -r -d '' f; do
    printf "=== %s ===\n" "$f" >> "$reports_dir/ui.txt"; cat "$f" >> "$reports_dir/ui.txt"
  done
}

# Sanitize/translate GCC-only flags in compile_commands.json for Clang-based tools
sanitize_compdb() {
  local in="$1" out="$2"
  mkdir -p "$(dirname "$out")"
  cp "$in" "$out"
  if command -v jq >/dev/null 2>&1; then
    jq '
      map(
        if has("arguments") then
          .arguments = (.arguments
            | map(
                if . == "-mno-direct-extern-access" then "-fno-direct-access-external-data"
                elif . == "-mdirect-extern-access" then "-fdirect-access-external-data"
                else . end
              )
          )
        else . end
      )
      | map(
        if has("command") then
          .command = (.command
            | gsub("(^|[[:space:]])-mno-direct-extern-access([[:space:]]|$)"; " -fno-direct-access-external-data ")
            | gsub("(^|[[:space:]])-mdirect-extern-access([[:space:]]|$)"; " -fdirect-access-external-data ")
          )
        else . end
      )
    ' "$in" > "$out.tmp" && mv "$out.tmp" "$out"
  else
    sed -E -i 's/(^| )-mno-direct-extern-access( |$)/ -fno-direct-access-external-data /g' "$out"
    sed -E -i 's/(^| )-mdirect-extern-access( |$)/ -fdirect-access-external-data /g' "$out"
    sed -E -i 's/,\s*"-mno-direct-extern-access"/,"-fno-direct-access-external-data"/g' "$out" || true
    sed -E -i 's/,\s*"-mdirect-extern-access"/,"-fdirect-access-external-data"/g' "$out" || true
  fi
}

# Optional tool runners
do_clang_tidy() {
  local compdb="$1" srcdir="$2" checks="$3"
  if command -v clang-tidy >/dev/null 2>&1 && [ -f "$compdb" ]; then
    local tmpdir rc rootdir
    tmpdir="$(mktemp -d)"
    rootdir="$(dirname "$srcdir")"
    sanitize_compdb "$compdb" "$tmpdir/compile_commands.json"
    # Focus on project code only, exclude Qt/system headers
    # Use .clang-tidy config file if present (checks arg is fallback)
    local config_arg=""
    if [ -f "$rootdir/.clang-tidy" ]; then
      config_arg="--config-file=$rootdir/.clang-tidy"
    else
      config_arg="-checks=$checks"
    fi
    find "$srcdir" -name '*.cpp' -print0 | xargs -0 -n1 -P"$(nproc)" clang-tidy \
      -p="$tmpdir" \
      $config_arg \
      --header-filter="$srcdir/.*"
    rc=$?
    rm -rf "$tmpdir"
    return $rc
  else
    echo "skipped: clang-tidy not available or compile_commands.json missing"; return 0
  fi
}
do_clang_tidy_apply_fixes() {
  local compdb="$1" srcdir="$2" checks="$3"
  if command -v clang-tidy >/dev/null 2>&1 && [ -f "$compdb" ]; then
    local tmpdir rc
    tmpdir="$(mktemp -d)"
    sanitize_compdb "$compdb" "$tmpdir/compile_commands.json"
    # Apply curated, safe fixes across all .cpp files
    rc=0
    if ! find "$srcdir" -name '*.cpp' -print0 | xargs -0 -n1 -P"$(nproc)" clang-tidy \
      -p="$tmpdir" \
      -checks="$checks" \
      -fix -fix-errors \
      --header-filter="$srcdir/.*"; then
      rc=$?
    fi
    rm -rf "$tmpdir"
    return $rc
  else
    echo "skipped: clang-tidy not available or compile_commands.json missing"; return 0
  fi
}
do_iwyu() {
  local compdb="$1" srcdir="$2"
  if [ -f "$compdb" ]; then
    local tmpdir rc rootdir mapping_arg
    tmpdir="$(mktemp -d)"
    rootdir="$(dirname "$srcdir")"
    sanitize_compdb "$compdb" "$tmpdir/compile_commands.json"
    
    # Use mapping file if present (maps Qt internal headers to public headers)
    mapping_arg=""
    if [ -f "$rootdir/.iwyu.imp" ]; then
      mapping_arg="-Xiwyu --mapping_file=$rootdir/.iwyu.imp"
    fi
    
    # Use iwyu_tool wrapper (preferred - handles compile_commands.json properly)
    # Check various names: iwyu_tool (Arch/common), iwyu-tool (Debian), iwyu_tool.py (pip)
    if command -v iwyu_tool >/dev/null 2>&1; then
      iwyu_tool -p "$tmpdir" -- -Xiwyu --max_line_length=100 $mapping_arg
      rc=$?
    elif command -v iwyu-tool >/dev/null 2>&1; then
      iwyu-tool -p "$tmpdir" -- -Xiwyu --max_line_length=100 $mapping_arg
      rc=$?
    elif command -v iwyu_tool.py >/dev/null 2>&1; then
      iwyu_tool.py -p "$tmpdir" -- -Xiwyu --max_line_length=100 $mapping_arg
      rc=$?
    else
      # Direct include-what-you-use invocation requires extracting flags from compile_commands.json
      # This is complex and error-prone; skip if iwyu-tool is not available
      echo "skipped: iwyu_tool not available (install iwyu package with iwyu_tool wrapper)"
      rm -rf "$tmpdir"
      return 0
    fi
    
    rm -rf "$tmpdir"
    return $rc
  else
    echo "skipped: compile_commands.json missing"; return 0
  fi
}
do_cppcheck() {
  local srcdir="$1"
  if command -v cppcheck >/dev/null 2>&1; then
    # Use C++23 standard, remove excessive suppressions, focus on actionable issues
    cppcheck \
      --enable=warning,style,performance,portability \
      --std=c++23 \
      --suppress=missingIncludeSystem \
      --suppress=unusedFunction \
      --library=qt \
      --inline-suppr \
      -I "$srcdir" -I "$srcdir/core" -I "$srcdir/managers" -I "$srcdir/ui" -I "$srcdir/utils" \
      "$srcdir"
  else
    echo "skipped: cppcheck not installed"; return 0
  fi
}
do_clang_format() {
  local srcdir="$1"
  if command -v clang-format >/dev/null 2>&1; then
    # Check formatting without applying changes, report issues but don't fail
    local issues=0
    while IFS= read -r -d '' file; do
      if ! clang-format --style=LLVM --dry-run --Werror "$file" >/dev/null 2>&1; then
        echo "Format issue: $file"
        issues=$((issues + 1))
      fi
    done < <(find "$srcdir" \( -name '*.cpp' -o -name '*.h' \) -print0)
    if [ $issues -gt 0 ]; then
      echo "Found $issues files with formatting issues (use --format-apply to fix)"
    else
      echo "All files properly formatted"
    fi
    return 0  # Always return success for quality checks
  else
    echo "skipped: clang-format not available"; return 0
  fi
}
do_clang_format_apply() {
  local srcdir="$1"
  if command -v clang-format >/dev/null 2>&1; then
    # Apply formatting fixes to all source files
    find "$srcdir" \( -name '*.cpp' -o -name '*.h' \) -print0 | \
      xargs -0 clang-format --style=LLVM -i
    return $?
  else
    echo "skipped: clang-format not available"; return 0
  fi
}
do_export_symbols() {
  local bin="$1" outlist="$2"
  if [ -x "$bin" ]; then
    nm -C "$bin" | sort > "$outlist"
  else
    echo "skipped: binary not found at $bin"; return 0
  fi
}

########################################
# Maintenance build (CMake)
########################################
if $maintenance_build; then
  ALL_STEPS=(); NEXT_STEP_IDX=0; PROGRESS_CUR=0
  plan_step "Prepare build directory"
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
  plan_step "clang-tidy analysis"
  plan_step "IWYU analysis"
  plan_step "cppcheck analysis"
  plan_step "Heuristic duplicate checks"
  plan_step "Export symbols (nm)"
  if $make_reports; then
    plan_step "Assemble reports"
    plan_step "Remove linker map files"
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

  cmake_args=(
    -S "$root_dir"
    -B "$build_dir"
    "${generator_args[@]}"
    -DCMAKE_BUILD_TYPE=Release
    -DMAINTENANCE=ON
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    -DCMAKE_C_COMPILER=clang
    -DCMAKE_CXX_COMPILER=clang++
    "-DBUILD_DATE=$TS_HUMAN"
    -DBUILD_TESTS=OFF
  )
  if $build_tests; then
    cmake_args+=(-DBUILD_TESTS=ON)
  fi
  if $ccache_available; then
    cmake_args+=(
      -DCMAKE_C_COMPILER_LAUNCHER=ccache
      -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
    )
  fi
  if ! $use_ccache; then
    cmake_args+=(-DENABLE_CCACHE=OFF)
  fi
  run_step "Configure" "$logs_dir/cmake_configure.log" "$cmake_bin" "${cmake_args[@]}"
  run_step "Build" "$logs_dir/cmake_build.log" "$cmake_bin" --build "$build_dir" -j"$(nproc)"
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

  # clang-format operations
  if $format_apply; then
    run_optional "clang-format (apply fixes)" "$logs_dir/clang-format-apply.log" do_clang_format_apply "$root_dir/src"
  fi
  if $format_check || ! $format_apply; then
    run_quality_check "clang-format check" "$logs_dir/clang-format.log" do_clang_format "$root_dir/src"
    # Check for clang-format issues
    if [ -s "$logs_dir/clang-format.log" ]; then
      if grep -qE 'Format issue:|Found [0-9]+ files with formatting issues' "$logs_dir/clang-format.log"; then
        CLANG_FORMAT_ISSUES=true
      fi
    fi
  fi

  if $apply_fixes; then
    fix_checks="readability-braces-around-statements,modernize-use-nullptr,modernize-use-override,readability-implicit-bool-conversion"
    run_optional "clang-tidy (apply fixes)" "$logs_dir/clang-tidy-fixes.log" do_clang_tidy_apply_fixes "$COMPDB_FILE" "$root_dir/src" "$fix_checks"
    run_step "Rebuild after clang-tidy fixes" "$logs_dir/cmake_build_after_fixes.log" "$cmake_bin" --build "$build_dir" -j"$(nproc)"
  fi

  checks="-*,clang-analyzer-*,modernize-*,performance-*,readability-*,-readability-static-accessed-through-instance,google-*"
  run_quality_check "clang-tidy analysis" "$logs_dir/clang-tidy.log" do_clang_tidy "$COMPDB_FILE" "$root_dir/src" "$checks"
  # IWYU is quality check; detect failure and suggestions for end-of-run notice
  if ! run_quality_check "IWYU analysis" "$logs_dir/iwyu.log" do_iwyu "$COMPDB_FILE" "$root_dir/src"; then
    IWYU_FAILED=true
  fi
  if [ -s "$logs_dir/iwyu.log" ]; then
    if grep -qE 'should add these lines:|should remove these lines:' "$logs_dir/iwyu.log"; then
      IWYU_SUGGESTED=true
    fi
  fi
  
  run_quality_check "cppcheck analysis" "$logs_dir/cppcheck.log" do_cppcheck "$root_dir/src"
  # Check for cppcheck warnings
  if [ -s "$logs_dir/cppcheck.log" ]; then
    if grep -qE ': (error|warning|style|performance|portability):' "$logs_dir/cppcheck.log"; then
      CPPCHECK_WARNED=true
    fi
  fi
  
  run_optional "Heuristic duplicate checks" "$logs_dir/dup-heuristics.log" bash -lc "grep -R --line-number -E \"QString .* = .*replace|completeBaseName|replace\\('_',' ')\" src || true"
  
  run_optional "Export symbols (nm)" "$logs_dir/nm.log" do_export_symbols "$build_dir/$target_name" "$build_dir/nm-list.txt"

  if $make_reports; then
    run_step "Assemble reports" "$logs_dir/reports-assemble.log" assemble_reports
    run_optional "Remove linker map files" "$logs_dir/remove-map.log" bash -lc "rm -f \"$reports_dir/$target_name.map\" \"$reports_dir\"/$target_name-*.map"
  fi

  abort_if_failed

  if $make_archive; then
    base="$(basename "$root_dir")"
    archive_name="${base}-${build_type}-${TS_HUMAN}"
    fname="${archive_name}.tar.gz"
    parent_dir="$(dirname "$root_dir")"
    archive_dir="${parent_dir}/${archive_name}"

    run_step "Stage files for archive" "$logs_dir/archive-stage.log" bash -lc "
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
  if [ "$CLANG_FORMAT_ISSUES" = true ]; then
    rel_clang_format="${logs_dir#"$root_dir/"}/clang-format.log"
    step_final "Notice: clang-format reported formatting issues. See ${rel_clang_format}"
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
    -DUSE_PGO=ON
    -DPGO_GENERATE=ON
    -DBUILD_TESTS=OFF
  )
  if $build_tests; then
    cmake_args+=(-DBUILD_TESTS=ON)
  fi
  if $ccache_available; then
    cmake_args+=(
      -DCMAKE_C_COMPILER_LAUNCHER=ccache
      -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
    )
  fi
  if ! $use_ccache; then
    cmake_args+=(-DENABLE_CCACHE=OFF)
  fi
  run_step "Configure PGO generate" "$logs_dir/configure.log" cmake "${cmake_args[@]}"
  run_step "Build PGO generate" "$logs_dir/build.log" cmake --build "$build_dir"

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
    -DUSE_PGO=ON
    -DPGO_USE=ON
    -DBUILD_TESTS=OFF
  )
  if $build_tests; then
    cmake_args+=(-DBUILD_TESTS=ON)
  fi
  if $ccache_available; then
    cmake_args+=(
      -DCMAKE_C_COMPILER_LAUNCHER=ccache
      -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
    )
  fi
  if ! $use_ccache; then
    cmake_args+=(-DENABLE_CCACHE=OFF)
  fi
  run_step "Configure PGO use" "$logs_dir/configure2.log" cmake "${cmake_args[@]}"
  run_step "Build PGO use" "$logs_dir/build2.log" cmake --build "$build_dir"

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

  abort_if_failed

  # Archive
  if $make_archive; then
    base="$(basename "$root_dir")"
    archive_name="${base}-${build_type}-${TS_HUMAN}"
    fname="${archive_name}.tar.gz"
    archive_dir="${parent_dir}/${archive_name}"
    run_step "Stage files for archive" "$logs_dir/archive-stage.log" bash -lc "
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
    -DBUILD_TESTS=OFF
  )
  if $build_tests; then
    cmake_args+=(-DBUILD_TESTS=ON)
  fi
  if $ccache_available; then
    cmake_args+=(
      -DCMAKE_C_COMPILER_LAUNCHER=ccache
      -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
    )
  fi
  if ! $use_ccache; then
    cmake_args+=(-DENABLE_CCACHE=OFF)
  fi
  if [ "$build_type" = "release" ] || [ "$build_type" = "relwithdebinfo" ]; then
    cmake_args+=(-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++)
  fi
  if $pgo_generate || $pgo_use; then
    cmake_args+=(-DUSE_PGO=ON)
    if $pgo_generate; then
      cmake_args+=(-DPGO_GENERATE=ON)
    elif $pgo_use; then
      cmake_args+=(-DPGO_USE=ON)
    fi
  fi

  if [ "$build_type" = "sanitize" ]; then
    cmake_args+=(-DENABLE_SANITIZERS=ON)
  fi

  # Configure
  run_step "Configure" "$logs_dir/cmake_configure.log" "$cmake_bin" "${cmake_args[@]}"
  # Build
  run_step "Build" "$logs_dir/cmake_build.log" "$cmake_bin" --build "$build_dir" -j"$(nproc)"

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

  abort_if_failed

  # Archive (exclude build & reports)
  if $make_archive; then
    base="$(basename "$root_dir")"
    archive_name="${base}-${build_type}-${TS_HUMAN}"
    fname="${archive_name}.tar.gz"
    parent_dir="$(dirname "$root_dir")"
    archive_dir="${parent_dir}/${archive_name}"

    run_step "Stage files for archive" "$logs_dir/archive-stage.log" bash -lc "
      rm -rf \"$archive_dir\" &&
      mkdir -p \"$archive_dir\" &&
      if command -v rsync >/dev/null 2>&1; then
        rsync -a --delete --itemize-changes --exclude='.git' --exclude='build' --exclude='reports' --exclude='.*' ./ \"$archive_dir\"/
      else
        cp -av . \"$archive_dir\"/ &&
        rm -rf \"$archive_dir\"/build \"$archive_dir\"/reports \"$archive_dir\"/.*
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
  if $make_archive; then
    step_final "Archive created: ${backups_dir}/${fname}"
  fi
  exit 0
fi