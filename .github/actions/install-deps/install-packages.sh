#!/usr/bin/env bash
# Download and install a job's apt packages, moving on to the next mirror when one does not deliver
# (Kartend-wym32, Kartend-b128j).
#
#     install-packages.sh PACKAGE...
#
# The hosted Ubuntu images read their package mirrors from /etc/apt/apt-mirrors.txt, in priority order. apt moves
# on to the next mirror only when one FAILS. On 2026-11-17 the first mirror, us-east-2.ec2.archive.ubuntu.com,
# neither failed nor was it slow as a whole: it delivered libclang-cpp19 (14.3 MB) in 0.9 s and then sat on
# libllvm15t64 (25.5 MB) for five minutes; other jobs got libcodec2 (9 MB) at 60 kB/s. Every Linux job with a
# large package set hit the install bound, main was red for hours, and re-runs drew the same mirror. No timing
# test in advance can see this (two were tried in the pull request that added this script: the package index and
# a single package file both came at 3 MB/s from the mirror that then hung).
#
# So the download is its own step with a short bound. A download that does not finish in time is stopped, the
# first mirror goes to the end of the list, and the download carries on with the next one: the packages already
# fetched stay in apt's cache. Only then are the packages installed, from that cache.
#
#   1. apt-get install --download-only, at most DOWNLOAD_ATTEMPT_SECONDS per mirror (a healthy mirror delivers a
#      160 MB set in well under half a minute). Stopping it is safe: no package is being unpacked yet.
#   2. apt-get install, once, never retried: a stop in the middle of dpkg can leave it needing
#      `dpkg --configure -a`, and a blind second run would fail more confusingly than the labelled timeout.
#
# Both share INSTALL_TIMEOUT; a third of it (180 s at most) is kept back for step 2. Exit status: apt's own, or
# 124 when the time ran out (with an annotation that says so).
#
# Environment:
#   INSTALL_TIMEOUT           seconds for everything                      default 600
#   DOWNLOAD_ATTEMPT_SECONDS  seconds one mirror gets for the download    default 120
#   APT_OPTS, RECOMMENDS_OPT  passed to apt-get as they are
#   APT_MIRRORS_FILE          the mirror list                             default /etc/apt/apt-mirrors.txt
#   APT_MIRROR_FALLBACKS      mirrors added behind the listed ones        default azure.archive and archive.ubuntu.com
#   APT_GET, APT_SUDO         the apt-get to run and how to become root   default apt-get, sudo ("" for none)
set -u

if [ "$#" -eq 0 ]; then
    echo "install-packages: no package named" >&2
    exit 2
fi
INSTALL_TIMEOUT="${INSTALL_TIMEOUT:-600}"
DOWNLOAD_ATTEMPT="${DOWNLOAD_ATTEMPT_SECONDS:-120}"
MIRRORS="${APT_MIRRORS_FILE:-/etc/apt/apt-mirrors.txt}"
FALLBACKS="${APT_MIRROR_FALLBACKS-http://azure.archive.ubuntu.com/ubuntu/ http://archive.ubuntu.com/ubuntu/}"
SUDO="${APT_SUDO-sudo}"
# shellcheck disable=SC2086,SC2206  # SUDO, APT_OPTS and RECOMMENDS_OPT are word lists, possibly empty
apt_install=($SUDO env DEBIAN_FRONTEND=noninteractive "${APT_GET:-apt-get}" ${APT_OPTS:-} ${RECOMMENDS_OPT:-} install -y)

first_mirror() {
    local url _
    [ -f "$MIRRORS" ] || return 0
    while read -r url _; do
        case "$url" in http://* | https://*) echo "$url"; return 0 ;; esac
    done <"$MIRRORS"
}

# The first mirror goes to the end of the list; the public fallbacks join the list in front of it, once.
# Fails, changing nothing, when there is no list, no other mirror, or the list cannot be written.
next_mirror() {
    local listed=() order=() url _ have want known new priority=0
    [ -f "$MIRRORS" ] || return 1
    while read -r url _; do
        case "$url" in http://* | https://*) listed+=("$url") ;; esac
    done <"$MIRRORS"
    [ "${#listed[@]}" -gt 0 ] || return 1
    order=("${listed[@]:1}")
    for url in $FALLBACKS; do
        known=0
        want="${url#*://}"   # the same mirror over http and https is one mirror
        want="${want%/}"
        for have in "${listed[@]}"; do
            have="${have#*://}"
            [ "${have%/}" = "$want" ] && known=1
        done
        [ "$known" -eq 1 ] || order+=("$url")
    done
    order+=("${listed[0]}")
    [ "${#order[@]}" -gt 1 ] || return 1
    new="$(mktemp)" || return 1
    for url in "${order[@]}"; do
        priority=$((priority + 1))
        printf '%s\tpriority:%d\n' "$url" "$priority" >>"$new"
    done
    # shellcheck disable=SC2086  # SUDO is a command, possibly with arguments, possibly empty
    if $SUDO install -m 0644 "$new" "$MIRRORS"; then
        rm -f "$new"
        return 0
    fi
    rm -f "$new"
    return 1
}

start=$SECONDS
reserve=$((INSTALL_TIMEOUT / 3))
[ "$reserve" -le 180 ] || reserve=180
download_until=$((INSTALL_TIMEOUT - reserve))
status=0
while :; do
    left=$((download_until - (SECONDS - start)))
    if [ "$left" -lt 5 ]; then
        status=124
        break
    fi
    bound=$DOWNLOAD_ATTEMPT
    [ "$bound" -le "$left" ] || bound=$left
    status=0
    timeout "$bound" "${apt_install[@]}" --download-only "$@" || status=$?
    [ "$status" -eq 124 ] || break
    slow="$(first_mirror)"
    if next_mirror; then
        echo "::warning title=apt mirror did not deliver::The package download did not finish in ${bound}s on ${slow:-the first mirror}; carrying on with $(first_mirror) (Kartend-wym32)."
    else
        echo "install-packages: the download did not finish in ${bound}s and there is no other mirror to move on to"
        break
    fi
done

if [ "$status" -eq 0 ]; then
    left=$((INSTALL_TIMEOUT - (SECONDS - start)))
    [ "$left" -ge 30 ] || left=30
    timeout "$left" "${apt_install[@]}" "$@" || status=$?
fi
if [ "$status" -eq 124 ]; then
    echo "::error title=apt install timed out::Dependency install exceeded ${INSTALL_TIMEOUT}s — the apt mirror is crawling (Kartend-wym32). Every mirror in the list had its turn; the warnings above name them."
fi
exit "$status"
