#!/usr/bin/env bash
# Reproduce CI failures locally in a Docker container that mirrors
# ubuntu-24.04 GitHub-hosted runner (Qt 6.4, gcc 13.2, no audio device).
#
# Usage:
#   .scripts/ci-repro.sh                # repro: Release+gcc, no pulse (mirrors the original SIGILL)
#   .scripts/ci-repro.sh --with-pulse   # verify round-3 fix: same job, but with dummy pulse running
#   .scripts/ci-repro.sh --maintenance  # repro the maintenance-check job (clang-format/tidy/cppcheck)
#   .scripts/ci-repro.sh --shell        # drop into the container for interactive debug (gdb available)
#
# Build dir is /work/build-ci-repro inside the container, $REPO/build-ci-repro
# on the host (.gitignored as build-*/). ccache persists in a docker volume so
# repeated runs are fast.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="kartend-ci-repro:24.04"
CCACHE_VOL="kartend-ci-repro-ccache"

# Build the image on first use, or when the Dockerfile is newer than the
# image. Subsequent invocations are instant.
needs_build=true
if image_id="$(docker images -q "$IMAGE")" && [ -n "$image_id" ]; then
  image_ts="$(docker inspect -f '{{.Created}}' "$IMAGE" 2>/dev/null | xargs -I{} date -d {} +%s)"
  dockerfile_ts="$(stat -c %Y "$ROOT/.scripts/ci-repro.Dockerfile")"
  if [ "$image_ts" -ge "$dockerfile_ts" ]; then
    needs_build=false
  fi
fi
if $needs_build; then
  echo ">>> Building $IMAGE (one-time, ~3-5 min)"
  docker build \
    --build-arg HOST_UID="$(id -u)" \
    --build-arg HOST_GID="$(id -g)" \
    -t "$IMAGE" \
    -f "$ROOT/.scripts/ci-repro.Dockerfile" \
    "$ROOT/.scripts"
fi

# Persistent ccache so consecutive runs don't recompile from scratch. New
# volumes are root-owned by default; chown to the dev user (UID matches
# host) so ccache itself can write to it.
if ! docker volume inspect "$CCACHE_VOL" >/dev/null 2>&1; then
  docker volume create "$CCACHE_VOL" >/dev/null
  docker run --rm -v "$CCACHE_VOL:/cache" "$IMAGE" sudo chown "$(id -u):$(id -g)" /cache
fi

run_in_container() {
  docker run --rm -it \
    -v "$ROOT:/work" \
    -v "$CCACHE_VOL:/home/dev/.cache/ccache" \
    -e CCACHE_DIR=/home/dev/.cache/ccache \
    "$IMAGE" \
    bash -c "$1"
}

start_pulse_snippet='
mkdir -p /tmp/pulse-runtime && chmod 700 /tmp/pulse-runtime
export XDG_RUNTIME_DIR=/tmp/pulse-runtime
pulseaudio --start --exit-idle-time=-1 --daemonize \
  --load="module-null-sink sink_name=NullOutput" \
  --load="module-null-source source_name=NullInput" || true
'

build_and_test_snippet='
ccache --zero-stats >/dev/null 2>&1 || true
cmake -S /work -B /work/build-ci-repro -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DKARTEND_BUILD_TESTS=ON \
  -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
cmake --build /work/build-ci-repro --parallel "$(nproc)"
cd /work/build-ci-repro
QT_QPA_PLATFORM=offscreen ctest --output-on-failure
'

case "${1:-default}" in
  --shell)
    echo ">>> Interactive shell. Pulse is NOT started — run the start_pulse() function below if you need it."
    run_in_container "
$start_pulse_snippet
start_pulse() { :; }  # already ran above; placeholder if you want to re-invoke
exec bash
"
    ;;

  --with-pulse)
    echo ">>> Reproducing build (Release, gcc) WITH pulseaudio dummy daemon (round-3 fix)"
    run_in_container "
$start_pulse_snippet
$build_and_test_snippet
"
    ;;

  --maintenance)
    echo ">>> Reproducing maintenance-check (clang-format/tidy/cppcheck)"
    run_in_container "
bash .scripts/build.sh --maintenance --format-check
"
    ;;

  default|"")
    echo ">>> Reproducing build (Release, gcc) WITHOUT pulseaudio (mirrors bare GitHub runner pre-round-3)"
    echo ">>> Expect SIGILL on TestMainWindowSmoke::testFixtureBuildsWithoutTouchingRealConfig if the bug is environmental."
    run_in_container "
$build_and_test_snippet
"
    ;;

  *)
    echo "Unknown option: $1" >&2
    sed -n '2,12p' "$0" >&2
    exit 2
    ;;
esac
