#!/bin/sh
# Exercise the packaged binary against an isolated, disposable library.
set -eu

app=${1:-/usr/local/bin/kartend}
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/kartend-freebsd.XXXXXXXX")
server_pid=
app_pid=
# shellcheck disable=SC2317
cleanup() {
    if [ -n "$app_pid" ]; then
        kill "$app_pid" 2>/dev/null || :
        wait "$app_pid" 2>/dev/null || :
    fi
    if [ -n "$server_pid" ]; then
        kill "$server_pid" 2>/dev/null || :
        wait "$server_pid" 2>/dev/null || :
    fi
    rm -rf -- "$test_dir"
}
trap cleanup EXIT

export HOME="$test_dir" XDG_RUNTIME_DIR="$test_dir/runtime"
mkdir -m 700 "$XDG_RUNTIME_DIR"
export KARTEND_LAUNCH_RECEIPT="$test_dir/launched"
mkdir -p "$test_dir/original" "$test_dir/export/config/kartend" "$test_dir/import/config/kartend"
printf 'Fixture for native Kartend\n' > "$test_dir/original/launch-probe.txt"
cat > "$test_dir/record-launch.sh" <<'LAUNCH'
#!/bin/sh
set -eu
printf '%s\n' "$@" > "$KARTEND_LAUNCH_RECEIPT"
LAUNCH
chmod 700 "$test_dir/record-launch.sh"

profile() {
    profile_dir=$1
    export XDG_CONFIG_HOME="$profile_dir/config"
    export XDG_DATA_HOME="$profile_dir/data"
    export XDG_CACHE_HOME="$profile_dir/cache"
}

collection() {
    cat > "$XDG_CONFIG_HOME/kartend/kartend.cfg" <<EOF
[General]
firstRunComplete=true
useHomeView=false

[FreeBSDProbe]
name=FreeBSDProbe
mediaDirectory=$1
launcherPath=$2
extensions=txt
EOF
}

profile "$test_dir/export"
collection "$test_dir/original" /usr/bin/true
QT_QPA_PLATFORM=offscreen "$app" --export-kart FreeBSDProbe --export-out "$test_dir/fixture.kart"
profile "$test_dir/import"
QT_QPA_PLATFORM=offscreen "$app" --import-kart "$test_dir/fixture.kart" --to "$test_dir/imported"
imported=$(find "$test_dir/imported" -type f -name launch-probe.txt)
test -n "$imported"
cmp "$imported" "$test_dir/original/launch-probe.txt"
echo "The installed binary exported and imported the fixture into a fresh profile."

# The headless command extracts the bundle without registering a GUI collection.
collection "$(dirname "$imported")" "$test_dir/record-launch.sh"
Xvfb -displayfd 3 -screen 0 1920x1080x24 -nolisten tcp \
    3> "$test_dir/display" > "$test_dir/xvfb.log" 2>&1 &
server_pid=$!
for _ in 1 2 3 4 5; do
    test -s "$test_dir/display" && break
    sleep 1
done
if [ ! -s "$test_dir/display" ]; then
    cat "$test_dir/xvfb.log"
    exit 1
fi
DISPLAY=":$(cat "$test_dir/display")"
export DISPLAY
QT_QPA_PLATFORM=xcb KARTEND_SMOKE_TEST_EXIT_MS=12500 "$app" --collection FreeBSDProbe \
    > "$test_dir/kartend.log" 2>&1 &
app_pid=$!
selected=false
for _ in 1 2 3 4 5 6 7; do
    window=$(xdotool search --onlyvisible --name '^FreeBSDProbe [(]1 Item[)]$' | head -1)
    if [ -n "$window" ]; then
        xdotool windowfocus "$window"
        if [ "$selected" = false ]; then
            xdotool key Home
            selected=true
            sleep 1
        fi
        xdotool key Return
    fi
    test -s "$KARTEND_LAUNCH_RECEIPT" && break
    sleep 1
done
if [ ! -s "$KARTEND_LAUNCH_RECEIPT" ]; then
    xwininfo -root -tree
    cat "$test_dir/kartend.log"
    exit 1
fi
test "$(cat "$KARTEND_LAUNCH_RECEIPT")" = "$imported"
wait "$app_pid"
app_pid=
echo "The installed GUI passed the exact imported file path to its launcher."
