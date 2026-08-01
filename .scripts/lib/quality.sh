# Code-quality tooling wrappers
#
# compile_commands sanitize, clang-tidy (+apply), IWYU, cppcheck,
# clang-format (CI-pinned v19), nm symbol export, and report assembly.
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

# Sanitize/translate GCC-only flags in compile_commands.json for Clang-based
# tools. Requires jq — the previous sed fallback was a fragile pile of regexes
# that could silently produce malformed JSON on edge cases (escaped quotes,
# multiline command strings, the `arguments` array form, etc.). Better to fail
# loudly with a clear remediation than to silently corrupt the compdb.
sanitize_compdb() {
  local in="$1" out="$2"
  mkdir -p "$(dirname "$out")"
  if ! command -v jq >/dev/null 2>&1; then
    echo "Error: jq is required to sanitize compile_commands.json for clang-based tools." >&2
    echo "  Install it (Arch: 'pacman -S jq', Debian/Ubuntu: 'apt install jq', macOS: 'brew install jq')." >&2
    return 1
  fi
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
}

# Python guardrail lints — the same check-*.py scripts CI's script-lint job
# enforces (layering DAG + DI invariants, singleShot "why" comments, the
# src<->tests mapping, bd-ID leakage, required-checks path lockstep). They are
# build-independent and finish in well under a second, so --maintenance runs
# them up front: CONTRIBUTING documents the maintenance gate as including the
# layering lint, and before this helper existed that claim was only true in
# CI. Prints "skipped: ..." (and still returns 0) when python3 is absent so
# run_quality_check records the skip in the ran-vs-skipped summary instead of
# reporting a green gate that ran nothing.
do_python_guardrails() {
  local rootdir="$1"
  if ! command -v python3 >/dev/null 2>&1; then
    echo "skipped: python3 not found — the guardrail lints still run in CI's script-lint job"
    return 0
  fi
  local script rc=0
  for script in \
    check-layering.py \
    check-singleshot-comments.py \
    check-test-mapping.py \
    check-bd-id-leakage.py \
    check-required-checks-consistency.py; do
    echo "== $script =="
    python3 "$rootdir/.scripts/$script" || rc=1
  done
  return "$rc"
}

# Optional tool runners
#
# Kartend-z4ev0: when KARTEND_TIDY_ONLY_FILES is set (a path to a
# newline-delimited list of .cpp paths, relative to the repo root or
# absolute), do_clang_tidy analyzes only the listed TUs that exist under
# $srcdir instead of fanning out over every .cpp. CI sets this on
# pull_request events so a PR only pays for the TUs it touched; local runs
# and the nightly lint sweep leave it unset and keep the full sweep
# (push-to-main maintenance runs skip this advisory pass entirely via
# --analysis=off — the workflow's curated enforcing gate covers them).
# KNOWN LIMITATION: a changed header does not pull its including TUs into
# the scoped set — header-only regressions surface in the nightly full
# sweep, not on the PR.
do_clang_tidy() {
  local compdb="$1" srcdir="$2" checks="$3"
  if command -v clang-tidy >/dev/null 2>&1 && [ -f "$compdb" ]; then
    local tmpdir rc rootdir
    tmpdir="$(mktemp -d)"
    rootdir="$(dirname "$srcdir")"

    # Resolve the optional diff scope BEFORE doing any work so an empty
    # changed-set short-circuits cleanly (e.g. a docs-only PR).
    local -a scoped_tus=()
    if [ -n "${KARTEND_TIDY_ONLY_FILES:-}" ]; then
      if [ ! -f "$KARTEND_TIDY_ONLY_FILES" ]; then
        echo "Error: KARTEND_TIDY_ONLY_FILES points to a missing file: $KARTEND_TIDY_ONLY_FILES" >&2
        rm -rf "$tmpdir"
        return 1
      fi
      local entry resolved
      while IFS= read -r entry; do
        [ -n "$entry" ] || continue
        case "$entry" in
          /*) resolved="$entry" ;;
          *)  resolved="$rootdir/$entry" ;;
        esac
        # Keep only .cpp TUs that still exist (diff lists can contain
        # deleted files) and live under the scan root this call covers.
        case "$resolved" in
          "$srcdir"/*.cpp)
            [ -f "$resolved" ] && scoped_tus+=("$resolved")
            ;;
        esac
      done < "$KARTEND_TIDY_ONLY_FILES"
      if [ "${#scoped_tus[@]}" -eq 0 ]; then
        echo "skipped: diff scope is empty — no changed .cpp TUs under $srcdir (KARTEND_TIDY_ONLY_FILES)"
        rm -rf "$tmpdir"
        return 0
      fi
      echo "diff scope: analyzing ${#scoped_tus[@]} changed TU(s) under $srcdir (KARTEND_TIDY_ONLY_FILES)"
    fi

    sanitize_compdb "$compdb" "$tmpdir/compile_commands.json"
    # Focus on project code only, exclude Qt/system headers
    # Use .clang-tidy config file if present (checks arg is fallback)
    local -a config_arg
    if [ -f "$rootdir/.clang-tidy" ]; then
      config_arg=(--config-file="$rootdir/.clang-tidy")
    else
      config_arg=(-checks="$checks")
    fi
    if [ "${#scoped_tus[@]}" -gt 0 ]; then
      printf '%s\0' "${scoped_tus[@]}" | xargs -0 -n1 -P"$(nproc)" clang-tidy \
        -p="$tmpdir" \
        "${config_arg[@]}" \
        --header-filter="$srcdir/.*"
      rc=$?
    else
      find "$srcdir" -name '*.cpp' -print0 | xargs -0 -n1 -P"$(nproc)" clang-tidy \
        -p="$tmpdir" \
        "${config_arg[@]}" \
        --header-filter="$srcdir/.*"
      rc=$?
    fi
    rm -rf "$tmpdir"
    return $rc
  else
    echo "skipped: clang-tidy not available or compile_commands.json missing"; return 0
  fi
}

# Kartend-rqvp7: print the apply-path hazard warnings to the script's own
# output (they previously lived only in docs/dev/building.md, so anyone
# invoking --apply-fixes / --format-apply directly never saw them).
warn_apply_hazards() {
  cat <<'WARN'
─────────────────────────────────────────────────────────────────────────
 CAUTION: in-place auto-fix paths rewrite tracked source files.
   * The clang-tidy auto-fixer has been known to MANGLE headers under
     src/utils/uiconstants/ — inspect `git diff` carefully afterward and
     be ready to `git checkout -- src/utils/uiconstants/` if it corrupts
     them.
   * Fixes are applied in place. This run requires a clean working tree
     (or --apply-fixes-dirty-ok) so the resulting diff is isolated and
     reviewable.
─────────────────────────────────────────────────────────────────────────
WARN
}

# Kartend-rqvp7: refuse to rewrite files on a dirty tree so the apply diff is
# reviewable in isolation (the auto-fixers can mangle headers; mixing their
# edits into uncommitted work makes the churn impossible to inspect or back
# out). Returns 0 if clean (or git is unavailable / not a repo — in which
# case there's nothing to clobber from VCS's view), non-zero if dirty.
# Override with KARTEND_APPLY_FIXES_DIRTY_OK=1 (build.sh exposes
# --apply-fixes-dirty-ok). Checks tracked files only (untracked files aren't
# rewritten by the fixers).
require_clean_tree_for_apply() {
  local rootdir="$1"
  if [ "${KARTEND_APPLY_FIXES_DIRTY_OK:-0}" = "1" ]; then
    echo "Note: KARTEND_APPLY_FIXES_DIRTY_OK=1 — skipping the clean-tree guard; the apply diff will be mixed with your uncommitted changes."
    return 0
  fi
  if ! command -v git >/dev/null 2>&1; then
    return 0
  fi
  if ! git -C "$rootdir" rev-parse --git-dir >/dev/null 2>&1; then
    return 0
  fi
  # Tracked-file changes only (staged or unstaged). Untracked files are not
  # touched by clang-tidy/clang-format -i, so they don't compromise the diff.
  if ! git -C "$rootdir" diff --quiet || ! git -C "$rootdir" diff --cached --quiet; then
    echo "Error: refusing to apply in-place fixes on a dirty working tree." >&2
    echo "  The auto-fixers rewrite tracked source files in place; running them on" >&2
    echo "  top of uncommitted work makes the resulting diff impossible to review or" >&2
    echo "  back out cleanly (and can mangle src/utils/uiconstants/ headers)." >&2
    echo "  Commit or stash your changes first, then re-run — or set" >&2
    echo "  KARTEND_APPLY_FIXES_DIRTY_OK=1 (build.sh: --apply-fixes-dirty-ok) to override." >&2
    return 1
  fi
  return 0
}

# Kartend-rqvp7: the apply check list is derived from .clang-tidy rather than
# a separate hardcoded set. The old hardcoded list included
# readability-braces-around-statements and readability-implicit-bool-conversion
# — both DISABLED in .clang-tidy — so --apply-fixes introduced edits that
# violated the project's own committed style policy and then got reverted.
# Using --config-file makes clang-tidy honor exactly the enabled set (the
# WarningsAsErrors-promoted checks plus the modernize-use-* fixers the config
# leaves on), so the fix set can never drift from the source of truth again.
do_clang_tidy_apply_fixes() {
  local compdb="$1" srcdir="$2" checks="$3"
  if command -v clang-tidy >/dev/null 2>&1 && [ -f "$compdb" ]; then
    local tmpdir rc rootdir
    tmpdir="$(mktemp -d)"
    rootdir="$(dirname "$srcdir")"
    sanitize_compdb "$compdb" "$tmpdir/compile_commands.json"
    # Prefer the .clang-tidy config so the applied fixes match the project's
    # enabled-check policy exactly. The legacy explicit `$checks` list is the
    # fallback only when no config is present.
    local -a config_arg
    if [ -f "$rootdir/.clang-tidy" ]; then
      config_arg=(--config-file="$rootdir/.clang-tidy")
    else
      config_arg=(-checks="$checks")
    fi
    # Apply fixes across all .cpp files
    rc=0
    if ! find "$srcdir" -name '*.cpp' -print0 | xargs -0 -n1 -P"$(nproc)" clang-tidy \
      -p="$tmpdir" \
      "${config_arg[@]}" \
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
    local tmpdir rc rootdir
    local -a mapping_arg=()
    tmpdir="$(mktemp -d)"
    rootdir="$(dirname "$srcdir")"
    sanitize_compdb "$compdb" "$tmpdir/compile_commands.json"

    # Use mapping file if present (maps Qt internal headers to public headers)
    if [ -f "$rootdir/.iwyu.imp" ]; then
      mapping_arg=(-Xiwyu "--mapping_file=$rootdir/.iwyu.imp")
    fi
    
    # Use iwyu_tool wrapper (preferred - handles compile_commands.json properly)
    # Check various names: iwyu_tool (Arch/common), iwyu-tool (Debian), iwyu_tool.py (pip)
    if command -v iwyu_tool >/dev/null 2>&1; then
      iwyu_tool -p "$tmpdir" -- -Xiwyu --max_line_length=100 "${mapping_arg[@]}"
      rc=$?
    elif command -v iwyu-tool >/dev/null 2>&1; then
      iwyu-tool -p "$tmpdir" -- -Xiwyu --max_line_length=100 "${mapping_arg[@]}"
      rc=$?
    elif command -v iwyu_tool.py >/dev/null 2>&1; then
      iwyu_tool.py -p "$tmpdir" -- -Xiwyu --max_line_length=100 "${mapping_arg[@]}"
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

# First arg is the src/ root (anchors the -I include set); any further args
# are additional scan roots (Kartend-rni3g: build.sh passes tests/ so the
# test tree gets the same cppcheck pass).
do_cppcheck() {
  local srcdir="$1"
  if command -v cppcheck >/dev/null 2>&1; then
    # Use C++23 standard, remove excessive suppressions, focus on actionable issues
    local common_args=(
      "--enable=warning,style,performance,portability"
      --std=c++23
      --suppress=missingIncludeSystem
      --suppress=unusedFunction
      --library=qt
      --inline-suppr
    )
    # Kartend-oizkj: prefer the compile database. The old hand-rolled -I list
    # had gone stale (src/managers/ no longer exists; modules/, api/, chrome/
    # were never listed), so cppcheck silently failed to resolve most project
    # includes and skipped every check that needed them — a green gate that
    # analyzed far less than intended. The compdb carries the real per-TU
    # include dirs and defines. Positional dir args become file filters.
    if [ -n "${COMPDB_FILE:-}" ] && [ -f "${COMPDB_FILE:-}" ]; then
      local file_filters=()
      local p
      for p in "$@"; do
        file_filters+=("--file-filter=${p}/*")
      done
      cppcheck "${common_args[@]}" --project="$COMPDB_FILE" "${file_filters[@]}"
    else
      # Fallback when no build dir / compdb exists: corrected include list
      # covering the real top-level src/ areas.
      cppcheck "${common_args[@]}" \
        -I "$srcdir" -I "$srcdir/api" -I "$srcdir/chrome" -I "$srcdir/core" \
        -I "$srcdir/modules" -I "$srcdir/ui" -I "$srcdir/utils" \
        "$@"
    fi
  else
    echo "skipped: cppcheck not installed"; return 0
  fi
}

# The clang-format resolver (resolve_clang_format) and the pinned version
# (KARTEND_CLANG_FORMAT_VERSION) live in .scripts/lib/clang-format-version.sh,
# sourced by build.sh before this module so the pin can't drift between the
# build script, the pre-commit hook, and CI. The hint text for the fail-loud
# paths is clang_format_missing_hint there too.

# Accepts one or more directories (Kartend-rni3g: callers pass src/ AND
# tests/ so the hand-maintained test tree is held to the same format bar).
#
# Kartend-gv2xq: when an enforcing --format-check is requested (build.sh's
# $format_check global, true in CI's maintenance run), a missing pinned
# clang-format is now a HARD FAILURE with a clear remediation message
# instead of a silent no-op — a silent skip made the gate look enforced
# while doing nothing. The default advisory pass (no --format-check) still
# self-skips so a contributor without the v19 binary isn't blocked, but the
# skip is surfaced in the end-of-run ran-vs-skipped summary.
do_clang_format() {
  local cf
  if cf=$(resolve_clang_format); then
    # Check formatting without applying changes, report issues but don't fail
    local issues=0 srcdir
    for srcdir in "$@"; do
      [ -d "$srcdir" ] || continue
      while IFS= read -r -d '' file; do
        if ! "$cf" --style=file --dry-run --Werror "$file" >/dev/null 2>&1; then
          echo "Format issue: $file"
          issues=$((issues + 1))
        fi
      done < <(find "$srcdir" \( -name '*.cpp' -o -name '*.h' \) -print0)
    done
    if [ $issues -gt 0 ]; then
      echo "Found $issues files with formatting issues (use --format-apply to fix)"
    else
      echo "All files properly formatted (using $cf)"
    fi
    return 0  # Always return success for quality checks
  elif [ "${format_check:-false}" = true ]; then
    # Enforcing check (CI) with no pinned binary: fail loud, do not pretend.
    echo "Error: clang-format check cannot run — no pinned formatter found." >&2
    clang_format_missing_hint >&2
    return 1
  else
    echo "skipped: no clang-format-${KARTEND_CLANG_FORMAT_VERSION} found (matches CI pin)"; return 0
  fi
}

do_clang_format_apply() {
  local cf
  if cf=$(resolve_clang_format); then
    # Apply formatting fixes to all source files
    echo "Applying formatting with $cf"
    local srcdir
    for srcdir in "$@"; do
      [ -d "$srcdir" ] || continue
      find "$srcdir" \( -name '*.cpp' -o -name '*.h' \) -print0 | \
        xargs -0 -r "$cf" --style=file -i || return $?
    done
    return 0
  else
    echo "skipped: no clang-format-${KARTEND_CLANG_FORMAT_VERSION} found (matches CI pin)"; return 0
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
