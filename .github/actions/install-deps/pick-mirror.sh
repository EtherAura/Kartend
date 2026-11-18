#!/usr/bin/env bash
# Put the apt mirror that answers fastest first in the runner's mirror list (Kartend-wym32, Kartend-b128j).
#
# The hosted Ubuntu images read their package mirrors from /etc/apt/apt-mirrors.txt, in priority order. apt moves
# on to the next mirror only when one FAILS; a mirror that answers slowly is used to the end. On 2026-11-17 the
# first mirror (us-east-2.ec2.archive.ubuntu.com) served about 60 kB/s for more than five hours: 160 MB of build
# dependencies could not finish inside the install step's 600 s bound in about half of all Linux jobs, main stayed
# red, and every re-run drew the same mirror again.
#
# This times one fetch of a real package file from every listed mirror and from two public fallbacks. A healthy
# first mirror leaves the list exactly as it is. Otherwise the list is rewritten fastest first. Whatever goes
# wrong here, the list stays as it was and the step exits 0: it may only help.
#
# The file timed is one of the packages this very job is about to download (the arguments are the job's package
# list; apt names the files and their sizes), about 1 MB. It must be a package file: that day the slow mirror
# served its package INDEX at 3 MB/s and the packages themselves at 100 kB/s, so a first version of this script
# that timed the index called the mirror healthy and changed nothing.
#
#     pick-mirror.sh [PACKAGE...]      run after `apt-get update`, before `apt-get install`
#
# Environment (all optional; the tests set them):
#   APT_MIRRORS_FILE          the mirror list                          default /etc/apt/apt-mirrors.txt
#   APT_MIRROR_HEALTHY_BPS    bytes/s at which the first mirror is     default 1000000
#                             left alone (a healthy in-cloud mirror
#                             does 10 to 100 times this)
#   APT_MIRROR_PROBE_SECONDS  time limit of one timing fetch           default 5
#   APT_MIRROR_FALLBACKS      mirrors timed besides the listed ones    default azure.archive and archive.ubuntu.com
#   APT_MIRROR_PROBE_PATH     path fetched below each mirror           default a package file of this job (see above)
#   APT_MIRROR_APT_GET        the apt-get that names the job's files   default apt-get
#   APT_MIRROR_SUDO           command that may write the list          default sudo ("" writes as the caller)
set -u

LIST="${APT_MIRRORS_FILE:-/etc/apt/apt-mirrors.txt}"
HEALTHY_BPS="${APT_MIRROR_HEALTHY_BPS:-1000000}"
PROBE_SECONDS="${APT_MIRROR_PROBE_SECONDS:-5}"
FALLBACKS="${APT_MIRROR_FALLBACKS-http://azure.archive.ubuntu.com/ubuntu/ http://archive.ubuntu.com/ubuntu/}"
SUDO="${APT_MIRROR_SUDO-sudo}"

if [ ! -f "$LIST" ]; then
    echo "pick-mirror: no $LIST (not a hosted runner image): apt's sources are left as they are"
    exit 0
fi
if ! command -v curl >/dev/null 2>&1; then
    echo "pick-mirror: curl is not installed: the mirror list is left as it is"
    exit 0
fi

# The file to time: of everything apt would download for this job's packages, the one nearest 1 MB (big enough to
# show a trickle inside the time limit, small enough to cost a healthy mirror a fraction of a second). apt prints
# one line per file: 'URI' name size hash; the path from pool/ on is the same on every mirror.
APT_GET="${APT_MIRROR_APT_GET:-apt-get}"
probe_path="${APT_MIRROR_PROBE_PATH:-}"
if [ -z "$probe_path" ] && [ "$#" -gt 0 ]; then
    probe_path="$("$APT_GET" install --print-uris -qq -y "$@" 2>/dev/null | awk '
        {
            uri = $1; size = $3 + 0
            if (!match(uri, /\/pool\/[^\047]+\.deb/)) next
            path = substr(uri, RSTART + 1, RLENGTH - 1)
            distance = size > 1000000 ? size - 1000000 : 1000000 - size
            if (best == "" || distance < best_distance) { best = path; best_distance = distance }
        }
        END { print best }')"
fi
if [ -z "$probe_path" ]; then
    echo "pick-mirror: apt named no package file to time: the mirror list is left as it is"
    exit 0
fi
echo "pick-mirror: timing $probe_path on each mirror"

# The listed mirrors in their order (first field of each line), then the fallbacks that are not listed.
mirrors=()
while read -r url _; do
    case "$url" in http://* | https://*) mirrors+=("$url") ;; esac
done <"$LIST"
if [ "${#mirrors[@]}" -eq 0 ]; then
    echo "pick-mirror: $LIST names no http(s) mirror: it is left as it is"
    exit 0
fi
listed="${#mirrors[@]}"
for url in $FALLBACKS; do
    known=0
    for have in "${mirrors[@]}"; do
        # the same mirror over http and https is one mirror
        have="${have#*://}"
        want="${url#*://}"
        [ "${have%/}" = "${want%/}" ] && known=1
    done
    [ "$known" -eq 1 ] || mirrors+=("$url")
done

# Bytes per second of one bounded fetch; 0 when the mirror does not serve the file. A fetch cut off by the time
# limit still reports the speed it reached, which is the number wanted from a slow mirror.
speed_of() {
    local out code speed
    out="$(curl -sS -o /dev/null --max-time "$PROBE_SECONDS" -w '%{http_code} %{speed_download}' \
        "${1%/}/$probe_path" 2>/dev/null)" || true
    code="${out%% *}"
    speed="${out##* }"
    speed="${speed%%.*}"
    case "$speed" in '' | *[!0-9]*) speed=0 ;; esac
    [ "$code" = "200" ] || speed=0
    echo "$speed"
}

speeds=()
for url in "${mirrors[@]}"; do
    speeds+=("$(speed_of "$url")")
    echo "pick-mirror: ${speeds[-1]} B/s  $url"
    # a healthy first mirror ends it: the others are not timed and nothing changes
    if [ "${#speeds[@]}" -eq 1 ] && [ "${speeds[0]}" -ge "$HEALTHY_BPS" ]; then
        echo "pick-mirror: the first mirror is healthy: the list is left as it is"
        exit 0
    fi
done

# Fastest first; equal speeds keep their order (so the listed order breaks ties, and unreachable mirrors stay last).
order="$(for i in "${!mirrors[@]}"; do echo "${speeds[$i]} $i"; done | sort -s -k1,1nr | awk '{print $2}')"
first="$(echo "$order" | head -n 1)"
if [ "$first" -eq 0 ] || [ "${speeds[$first]}" -le "${speeds[0]}" ]; then
    echo "pick-mirror: no mirror is faster than the first one: the list is left as it is"
    exit 0
fi

new="$(mktemp)" || exit 0
priority=0
for i in $order; do
    # a fallback that did not answer is not added to a list it was never in
    if [ "$i" -ge "$listed" ] && [ "${speeds[$i]}" -eq 0 ]; then
        continue
    fi
    priority=$((priority + 1))
    printf '%s\tpriority:%d\n' "${mirrors[$i]}" "$priority" >>"$new"
done
echo "pick-mirror: the first mirror is slow (${speeds[0]} B/s, under $HEALTHY_BPS); the list was:"
sed 's/^/    /' "$LIST"
# shellcheck disable=SC2086  # SUDO is a command, possibly with arguments, possibly empty
if $SUDO install -m 0644 "$new" "$LIST"; then
    echo "pick-mirror: it is now:"
    sed 's/^/    /' "$LIST"
else
    echo "pick-mirror: $LIST could not be written: it is left as it was"
fi
rm -f "$new"
exit 0
