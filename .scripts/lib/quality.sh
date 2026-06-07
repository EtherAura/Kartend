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
    local -a config_arg
    if [ -f "$rootdir/.clang-tidy" ]; then
      config_arg=(--config-file="$rootdir/.clang-tidy")
    else
      config_arg=(-checks="$checks")
    fi
    find "$srcdir" -name '*.cpp' -print0 | xargs -0 -n1 -P"$(nproc)" clang-tidy \
      -p="$tmpdir" \
      "${config_arg[@]}" \
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

# Resolve a clang-format binary that matches CI's v19 pin. Ubuntu 24.04 ships
# v18 and current Arch ships v21; both format differently enough from v19 to
# produce false-positive drift on commits that pass CI. Returns the resolved
# path/name on stdout, or empty (rc != 0) if no v19 candidate was found. Kept
# in sync with .scripts/git-hooks/pre-commit's copy of the same resolver.
resolve_clang_format_19() {
  if command -v clang-format-19 >/dev/null 2>&1; then
    printf 'clang-format-19\n'; return 0
  fi
  if [ -x /usr/lib/llvm/19/bin/clang-format ]; then
    printf '/usr/lib/llvm/19/bin/clang-format\n'; return 0
  fi
  if [ -x /usr/local/bin/clang-format ]; then
    printf '/usr/local/bin/clang-format\n'; return 0
  fi
  if command -v clang-format >/dev/null 2>&1 \
     && clang-format --version 2>/dev/null | grep -qE 'version 19\.'; then
    printf 'clang-format\n'; return 0
  fi
  return 1
}

do_clang_format() {
  local srcdir="$1"
  local cf
  if cf=$(resolve_clang_format_19); then
    # Check formatting without applying changes, report issues but don't fail
    local issues=0
    while IFS= read -r -d '' file; do
      if ! "$cf" --style=file --dry-run --Werror "$file" >/dev/null 2>&1; then
        echo "Format issue: $file"
        issues=$((issues + 1))
      fi
    done < <(find "$srcdir" \( -name '*.cpp' -o -name '*.h' \) -print0)
    if [ $issues -gt 0 ]; then
      echo "Found $issues files with formatting issues (use --format-apply to fix)"
    else
      echo "All files properly formatted (using $cf)"
    fi
    return 0  # Always return success for quality checks
  else
    echo "skipped: no clang-format-19 found (matches CI pin)"; return 0
  fi
}

do_clang_format_apply() {
  local srcdir="$1"
  local cf
  if cf=$(resolve_clang_format_19); then
    # Apply formatting fixes to all source files
    echo "Applying formatting with $cf"
    find "$srcdir" \( -name '*.cpp' -o -name '*.h' \) -print0 | \
      xargs -0 "$cf" --style=file -i
    return $?
  else
    echo "skipped: no clang-format-19 found (matches CI pin)"; return 0
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
