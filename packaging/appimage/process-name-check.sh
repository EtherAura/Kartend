#!/usr/bin/env bash
# Run one command under `timeout` and check the name its process runs under.
#
#   process-name-check.sh --name NAME --exe FILE --ready ERE --timeout SECONDS \
#     --log LOG --status-file FILE -- COMMAND [ARG...]
#
# smoke-test.sh runs the unpacked AppImage's AppRun through this. AppRun is a
# shell script that ends in exec, so the process `timeout` starts becomes
# Kartend itself, and Linux names it (/proc/<pid>/comm) after the file name
# it was executed by. The app must run as "kartend" there, not as
# "AppRun.wrapped": that name ends up in process lists and the X11 window
# class.
#
# How the process is found: `timeout` runs a small sh that writes its own pid
# to a private file and then execs COMMAND, so that pid is the one that ends
# up running FILE. No other process is looked at.
#
# When it is looked at: only once a line matching ERE is in LOG (COMMAND's
# stdout and stderr). That line has to be one only FILE writes, after its
# exec: smoke-test.sh uses Qt's plugin-loader log. /proc/<pid>/exe changing
# is not enough, the kernel switches it before it renames the process.
#
# What is checked: the pid is still the direct child of this run's timeout
# (before and after the reads, so it cannot be some later process that got
# the same pid), it runs FILE, and its name is exactly NAME: the whole
# /proc/<pid>/comm record must be NAME and the kernel's one newline, so a name
# that holds or ends in a newline never passes for NAME. A process that
# ends, never gets ready or cannot be read is a failure, never a pass.
#
# COMMAND is never stopped early: it runs until it finishes by itself, or
# until `timeout` ends it after SECONDS. Its exit status (as `timeout`
# reports it, 124 when it ran out of time) is written to the status file.
#
# Exit status: 0 when the name was checked and right, 1 when it was wrong or
# could not be checked, 2 for wrong usage.
set -euo pipefail

PROG="process-name-check"
say() { printf '%s: %s\n' "$PROG" "$*" >&2; }
usage_error() {
  say "$*"
  exit 2
}

NAME=""
EXE=""
READY=""
SECONDS_ALLOWED=""
LOG=""
STATUS_FILE=""
while (($# > 0)); do
  case "$1" in
    --name | --exe | --ready | --timeout | --log | --status-file)
      (($# >= 2)) || usage_error "$1 needs a value"
      case "$1" in
        --name) NAME=$2 ;;
        --exe) EXE=$2 ;;
        --ready) READY=$2 ;;
        --timeout) SECONDS_ALLOWED=$2 ;;
        --log) LOG=$2 ;;
        --status-file) STATUS_FILE=$2 ;;
      esac
      shift 2
      ;;
    --)
      shift
      break
      ;;
    *) usage_error "unknown argument: $1" ;;
  esac
done
(($# > 0)) || usage_error "no command given"
[ -n "$NAME" ] && [ -n "$EXE" ] && [ -n "$READY" ] && [ -n "$LOG" ] && [ -n "$STATUS_FILE" ] ||
  usage_error "--name, --exe, --ready, --log and --status-file are all needed"
[[ "$SECONDS_ALLOWED" =~ ^[1-9][0-9]*$ ]] || usage_error "--timeout needs a whole number of seconds"
[ -r /proc/self/stat ] || {
  say "no /proc here: the process name cannot be checked"
  exit 1
}
EXPECTED_EXE="$(readlink -f -- "$EXE")" || EXPECTED_EXE=""
[ -n "$EXPECTED_EXE" ] && [ -f "$EXPECTED_EXE" ] || {
  say "$EXE does not exist"
  exit 1
}

PID_FILE="$STATUS_FILE.pid"
rm -f -- "$STATUS_FILE" "$PID_FILE"
TIMEOUT_PID=""
# shellcheck disable=SC2329 # run by the EXIT trap
cleanup() {
  # Only reached with the command still running when this script itself is
  # interrupted; the normal way out waits for the command first.
  if [ -n "$TIMEOUT_PID" ] && kill -0 "$TIMEOUT_PID" 2>/dev/null; then
    kill -TERM "$TIMEOUT_PID" 2>/dev/null || true
    wait "$TIMEOUT_PID" 2>/dev/null || true
  fi
  rm -f -- "$PID_FILE"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM

# shellcheck disable=SC2016 # $$ and $@ belong to the inner sh
timeout "$SECONDS_ALLOWED" sh -c 'printf "%s\n" "$$" >"$1" && shift && exec "$@"' sh "$PID_FILE" "$@" \
  </dev/null >"$LOG" 2>&1 &
TIMEOUT_PID=$!

# parent_of PID: the parent pid from /proc/PID/stat, or nothing when PID is
# gone. The name in that line is in parentheses and may hold spaces.
parent_of() {
  local stat rest state parent
  stat="$(cat "/proc/$1/stat" 2>/dev/null)" || return 0
  rest=${stat##*) }
  read -r state parent _ <<<"$rest" || return 0
  [ "$state" = "Z" ] || printf '%s\n' "$parent"
}

verdict=1
pid=""
# One second more than the command may run ($SECONDS counts whole seconds),
# so this never gives up on a command that `timeout` still lets run.
deadline=$((SECONDS + SECONDS_ALLOWED + 1))
while :; do
  if [ -z "$pid" ] && [ -s "$PID_FILE" ]; then
    line=""
    # read fails until the whole line, with its newline, is there.
    if IFS= read -r line <"$PID_FILE" && [[ "$line" =~ ^[1-9][0-9]*$ ]]; then
      pid=$line
    fi
  fi
  if [ -n "$pid" ] && grep -qE -- "$READY" "$LOG"; then
    parent="$(parent_of "$pid")"
    # The whole comm record, not its first line: the kernel ends it with one
    # newline, and a name may itself hold newlines (a file named
    # "kartend<newline>" gives "kartend<newline><newline>"). read -d '' stops
    # only at a NUL, which comm never has, so it keeps every byte and fails at
    # the end of the file; an empty record means it could not be read.
    record=""
    IFS= read -r -d '' record 2>/dev/null <"/proc/$pid/comm" || true
    exe="$(readlink "/proc/$pid/exe" 2>/dev/null)" || exe=""
    parent_after="$(parent_of "$pid")"
    # The name for messages: the record without its one terminator, in single
    # quotes, or as $'...' when it holds a newline or other unprintable byte.
    name=${record%$'\n'}
    if [[ "$name" == *[![:print:]]* ]]; then
      printf -v name '%q' "$name"
    else
      name="'$name'"
    fi
    if [ "$parent" != "$TIMEOUT_PID" ] || [ "$parent_after" != "$TIMEOUT_PID" ] || [ -z "$record" ] || [ -z "$exe" ]; then
      say "pid $pid ended or could not be read once it was ready (parent '$parent', then '$parent_after'; expected $TIMEOUT_PID): its name is unknown"
    elif [ "$exe" != "$EXPECTED_EXE" ]; then
      say "pid $pid runs $exe, not $EXPECTED_EXE (process name $name)"
    elif [ "$record" != "$NAME"$'\n' ]; then
      say "pid $pid runs $exe under the process name $name, expected '$NAME'"
    else
      say "pid $pid runs $exe under the process name $name"
      verdict=0
    fi
    break
  fi
  if ! kill -0 "$TIMEOUT_PID" 2>/dev/null; then
    say "the command ended before it was ready (no line matching '$READY' in its log${pid:+, pid $pid}): its name is unknown"
    break
  fi
  if ((SECONDS >= deadline)); then
    say "the command was not ready within $SECONDS_ALLOWED s (no line matching '$READY' in its log): its name is unknown"
    break
  fi
  sleep 0.05
done

# The command finishes by itself (or by its timeout); it is not stopped here.
status=0
wait "$TIMEOUT_PID" || status=$?
TIMEOUT_PID=""
printf '%s\n' "$status" >"$STATUS_FILE"
exit "$verdict"
