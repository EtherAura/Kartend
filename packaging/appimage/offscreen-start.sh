#!/usr/bin/env bash
# The offscreen start of an unpacked AppImage, as smoke-test.sh runs it.
#
#   offscreen-start.sh ROOT FOREIGN_PLUGINS RUN_MS LOG STATUS_FILE
#
# Starts ROOT/AppRun on the offscreen platform with Qt's plugin loading
# logged and QT_PLUGIN_PATH pointing at FOREIGN_PLUGINS (a directory outside
# the AppImage, which the AppImage must ignore), lets the app quit by itself
# after RUN_MS, and has process-name-check.sh check that it runs as the
# process named "kartend". Its output goes to LOG, its exit status (124 when
# it hung for 180 s) to STATUS_FILE. The caller sets the rest of the
# environment (smoke-test.sh: a private HOME and XDG directories).
#
# The name is read once Qt's plugin-loader log shows that the app itself is
# running, which the log only shows with the category in front of each
# message. A QT_MESSAGE_PATTERN of the caller (such as '%{message}') would
# drop it, so this start sets Qt's default pattern itself, and logs to
# stderr (that is, LOG) even where Qt would log to the journal. The message
# bodies, which smoke-test.sh's plugin checks read, are the same either way.
#
# Exit status: that of process-name-check.sh (0 when the name was checked
# and right).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if (($# != 5)); then
  printf 'offscreen-start: usage: offscreen-start.sh ROOT FOREIGN_PLUGINS RUN_MS LOG STATUS_FILE\n' >&2
  exit 2
fi
ROOT=$1
FOREIGN_PLUGINS=$2
RUN_MS=$3
LOG=$4
STATUS_FILE=$5

exec env QT_DEBUG_PLUGINS=1 "QT_PLUGIN_PATH=$FOREIGN_PLUGINS" "KARTEND_SMOKE_TEST_EXIT_MS=$RUN_MS" \
  QT_QPA_PLATFORM=offscreen 'QT_MESSAGE_PATTERN=%{if-category}%{category}: %{endif}%{message}' \
  QT_FORCE_STDERR_LOGGING=1 \
  bash "$HERE/process-name-check.sh" --name kartend --exe "$ROOT/usr/bin/kartend" \
  --ready '^qt\.core\.(plugin|library)' --timeout 180 --log "$LOG" \
  --status-file "$STATUS_FILE" -- "$ROOT/AppRun"
