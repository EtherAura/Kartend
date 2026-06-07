# Step planning & execution harness
#
# Plan/track build steps and run commands as critical (run_step),
# advisory (run_quality_check), or optional (run_optional).
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
      exit "$rc"
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

  if [ "$rc" -ne 0 ]; then
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
      exit "$rc"
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

  # Record ran-vs-skipped for the end-of-run maintenance summary. Lint helpers
  # print "skipped: <reason>" and still return 0 when their tool is absent, so
  # rc alone can't distinguish a real pass from a silent no-op.
  if grep -qi '^skipped:' "$logfile" 2>/dev/null; then
    MAINT_CHECK_SUMMARY+=("skip|$desc|$(grep -im1 '^skipped:' "$logfile" | sed -E 's/^[Ss]kipped:[[:space:]]*//')")
  else
    MAINT_CHECK_SUMMARY+=("ran|$desc|")
  fi

  if [ "$rc" -ne 0 ]; then
    warn "$desc reported issues. See ${logfile#"$root_dir/"}"
  else
    step_completed "$desc"
  fi
  return "$rc"
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
      exit "$rc"
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

  if [ "$rc" -ne 0 ]; then
    warn "$desc skipped or failed. See ${logfile#"$root_dir/"}"
  else
    step_completed "$desc"
  fi
  return "$rc"
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
