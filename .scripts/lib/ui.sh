# UI & reporting helpers
#
# Colors, progress/step output, collected warnings, the usage text, and
# the EXIT-trap failure summary (on_exit).
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
  --tests           Configure with -DKARTEND_BUILD_TESTS=ON
  --run-tests       Run ctest after a successful build (requires --tests)
  --coverage        Configure with -DKARTEND_ENABLE_COVERAGE=ON for gcov
                    instrumentation (implies --debug + --tests; pair with
                    --run-tests to populate coverage counters; capture the
                    report with lcov against the build dir afterwards).
  --install         Run `cmake --install` after a successful build (honors DESTDIR;
                    auto-elevates with sudo or doas when the install prefix
                    isn't writable by the current user)
  --uninstall       Run the `uninstall` target on the most recent build dir
                    (reads install_manifest.txt; auto-elevates with sudo/doas
                    when needed)
  --prefix=PATH     Set CMAKE_INSTALL_PREFIX (default: CMake/system default,
                    typically /usr/local)
  --jobs=N          Override parallelism for the build step (default: nproc)
  --ninja           Force Ninja generator (if available)
  --make            Force Unix Makefiles generator
  --incremental     Reuse existing build directory (don't rm -rf it) (default)
  --clean           Remove build directory before configuring
  --archive         Create a source archive (.backups/*.tar.gz)
  --reports         Assemble source/UI reports into .backups/reports
  --keep-builds     Don't prune other build directories
  --no-ccache       Disable ccache launcher even if installed
  --clang           Force Clang/LLD toolchain for release builds (default:
                    use the system compiler; maintenance and PGO modes
                    require Clang and set this implicitly)

Maintenance-only:
  --apply-fixes      Apply safe clang-tidy fixes (requires --maintenance)
  --format-check     Run clang-format check (requires --maintenance)
  --format-apply     Apply clang-format (requires --maintenance)

Environment (maintenance-only):
  KARTEND_TIDY_ONLY_FILES=PATH
                    Scope clang-tidy to the .cpp TUs listed (one per line,
                    repo-relative or absolute) in PATH instead of the full
                    src/ sweep. CI sets this on pull_request events; changed
                    headers do NOT pull in their including TUs.

Other:
  -h, --help        Show this help
EOF
}

# Colors
setup_colors() {
  if [ -t 1 ] && command -v tput >/dev/null 2>&1 && [ "$(tput colors || echo 0)" -ge 8 ]; then
    RESET="$(tput sgr0)"
    RED="$(tput setaf 1)"; GREEN="$(tput setaf 2)"; YELLOW="$(tput setaf 3)"
    MAGENTA="$(tput setaf 5)"; CYAN="$(tput setaf 6)"
  else
    RESET=""; RED=""; GREEN=""; YELLOW=""; MAGENTA=""; CYAN=""
  fi
}

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
  # Kartend-12vqq: flush soft/non-fatal notices collected via warn(). They
  # accumulate in COLLECTED_WARNINGS (commented "print at the end") but nothing
  # ever printed them, so every warn() was silently swallowed. Print on BOTH
  # success and failure exits, before the failure diagnostics below. %b renders
  # the embedded color codes + "\n" separators warn() inserts.
  if [ -n "${COLLECTED_WARNINGS:-}" ]; then
    progress_clearline
    printf "%b\n" "$COLLECTED_WARNINGS" >&2
  fi
  if [ "$rc" -ne 0 ]; then
    progress_clearline
    printf "\n" >&2
    if $FAILED; then
      printf "%b\n" "${CYAN}[${RED}ERROR${CYAN}]${RESET} Build failed at step $((FAILED_STEP_IDX+1))/${PROGRESS_TOTAL}: ${FAILED_DESC}" >&2
      if [ -n "$FAILED_LOGFILE" ]; then
        local rel="${FAILED_LOGFILE#"$root_dir/"}"
        printf "%b\n" "${RED}Log file:${RESET} ${rel}" >&2
      fi
      if [ $((FAILED_STEP_IDX+1)) -lt "$PROGRESS_TOTAL" ]; then
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
