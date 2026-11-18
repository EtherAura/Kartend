#!/usr/bin/env bash
# Build the Kartend AppImage the way a release does: inside the release base
# container (named in common.sh), from this checkout.
#
#   packaging/appimage/build-in-container.sh
#
# This is the one command CI, the release workflow and a developer all run.
# It starts the base image with Docker, mounts the checkout, and runs
# prepare-build-env.sh and build-appimage.sh in it. The AppImage lands in
# build/appimage/ and its path is the last line on stdout.
#
# CMake, Qt, QtKeychain and the packaging tools are kept in --prefix-cache on
# the host (default: build/appimage-prefix), so a second run does not download
# them again. Delete that directory, or point --prefix-cache at an empty one,
# for a build that fetches everything fresh; a release does.
#
# Needs Docker (or Podman with `--docker podman`) and about 6 GB of disk.
set -euo pipefail

KARTEND_APPIMAGE_PROG="build-in-container"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/../.." && pwd)"
# shellcheck source=packaging/appimage/common.sh
. "$HERE/common.sh"

usage() {
  cat <<EOF
Usage: build-in-container.sh [options]

  --prefix-cache DIR   host directory that keeps CMake, Qt, QtKeychain and the
                       packaging tools between runs (default: <repo>/build/appimage-prefix)
  --jobs N             parallel build jobs (default: all cores)
  --docker PROG        container program (default: \$KARTEND_APPIMAGE_DOCKER, else docker)
  --skip-smoke-test    passed on to build-appimage.sh
  -h, --help           show this help

The base image is $KARTEND_APPIMAGE_BASE_IMAGE. The AppImage is written to <repo>/build/appimage/.
EOF
}

PREFIX_CACHE="$REPO_ROOT/build/appimage-prefix"
DOCKER="${KARTEND_APPIMAGE_DOCKER:-docker}"
build_args=()

while (($# > 0)); do
  case "$1" in
    --prefix-cache | --docker)
      (($# >= 2)) || kartend_appimage_die "$1 needs a value"
      case "$1" in
        --prefix-cache) PREFIX_CACHE=$2 ;;
        --docker) DOCKER=$2 ;;
      esac
      shift 2
      ;;
    --jobs)
      (($# >= 2)) || kartend_appimage_die "--jobs needs a value"
      build_args+=(--jobs "$2")
      shift 2
      ;;
    --skip-smoke-test)
      build_args+=(--skip-smoke-test)
      shift
      ;;
    -h | --help)
      usage
      exit 0
      ;;
    *)
      usage >&2
      kartend_appimage_die "unknown argument: $1"
      ;;
  esac
done

command -v "$DOCKER" >/dev/null 2>&1 || kartend_appimage_die "'$DOCKER' is not installed"
mkdir -p "$PREFIX_CACHE" "$REPO_ROOT/build/appimage"
PREFIX_CACHE="$(cd "$PREFIX_CACHE" && pwd)"

# A registry hiccup should not fail a build; a missing image should.
pulled=0
for attempt in 1 2 3; do
  if "$DOCKER" pull --quiet "$KARTEND_APPIMAGE_BASE_IMAGE" >&2; then
    pulled=1
    break
  fi
  kartend_appimage_note "pulling $KARTEND_APPIMAGE_BASE_IMAGE failed (attempt $attempt of 3)"
  sleep $((attempt * 10))
done
((pulled)) || kartend_appimage_die "could not pull $KARTEND_APPIMAGE_BASE_IMAGE"

# The container runs as root (apt-get needs it). Everything it leaves in the
# two mounted directories is handed back to the calling user at the end,
# whether the build passed or not, so nothing root-owned stays in the tree.
# shellcheck disable=SC2016  # the quoted script is expanded inside the container, not here
"$DOCKER" run --rm \
  --volume "$REPO_ROOT:/src" \
  --volume "$PREFIX_CACHE:$KARTEND_APPIMAGE_DEFAULT_PREFIX" \
  --workdir /src \
  --env "HOST_UID=$(id -u)" \
  --env "HOST_GID=$(id -g)" \
  --env "KARTEND_APPIMAGE_PREFIX=$KARTEND_APPIMAGE_DEFAULT_PREFIX" \
  "$KARTEND_APPIMAGE_BASE_IMAGE" \
  bash -c '
    set -uo pipefail
    status=0
    packaging/appimage/prepare-build-env.sh &&
      packaging/appimage/build-appimage.sh --tools-dir "$KARTEND_APPIMAGE_PREFIX/tools" "$@" >/src/build/appimage/.result-path ||
      status=$?
    chown -R "$HOST_UID:$HOST_GID" /src/build/appimage "$KARTEND_APPIMAGE_PREFIX"
    exit "$status"
  ' bash "${build_args[@]}" >&2

# build-appimage.sh printed the path as the container sees it (/src/...).
result="$(tail -n 1 "$REPO_ROOT/build/appimage/.result-path")"
rm -f "$REPO_ROOT/build/appimage/.result-path"
host_path="$REPO_ROOT/${result#/src/}"
[ -s "$host_path" ] || kartend_appimage_die "the build reported $result, but $host_path does not exist"
printf '%s\n' "$host_path"
