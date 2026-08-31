#!/usr/bin/env bash
# Run the GitHub Actions CI pipeline locally via `act` (nektos/act).
#
# Backed by .actrc at the repo root, which pins the runner image, container
# resource caps to match GHA's ubuntu-24.04 runner (4 vCPUs, 16GB), and the
# artifact server path. The wrapper here adds:
#   * version check (act 0.2.86+ is required for actions/cache@v6 = node24)
#   * artifact-server dir auto-create
#   * friendly subcommands so you don't have to remember exact job IDs
#
# Usage:
#   .scripts/ci-local.sh                 # run every job sequentially (full CI)
#   .scripts/ci-local.sh build           # full Release×Debug × gcc×clang matrix
#   .scripts/ci-local.sh build:rel:gcc   # one matrix cell
#   .scripts/ci-local.sh no-zstd
#   .scripts/ci-local.sh asan            # ASan/UBSan job
#   .scripts/ci-local.sh tsan            # ThreadSanitizer job
#   .scripts/ci-local.sh coverage
#   .scripts/ci-local.sh tidy            # maintenance-check (clang-tidy/format/cppcheck)
#   .scripts/ci-local.sh list            # show available jobs
#   .scripts/ci-local.sh shell           # drop into an interactive runner container
#   .scripts/ci-local.sh prune           # wipe the persistent act state (volumes + cache)
#   .scripts/ci-local.sh -- <act args>   # raw passthrough, e.g. .scripts/ci-local.sh -- -j build --verbose
#
# Faster Docker-direct variants (skip act overhead — same image, much quicker
# iteration when chasing CI failures). Require `docker build -f
# .scripts/Dockerfile.ci -t kartend-ci .` once; each docker:* subcommand
# checks the image exists first (require_ci_image) and prints that build
# command when it doesn't, then re-uses the warm cache on later runs.
#   .scripts/ci-local.sh docker:tidy     # maintenance-check via kartend-ci
#   .scripts/ci-local.sh docker:build    # Release+clang build + ctest via kartend-ci
#   .scripts/ci-local.sh docker:tsan     # TSan build + ctest via kartend-ci
#   .scripts/ci-local.sh docker:all      # docker:tidy + docker:build + docker:tsan in sequence
#
# Why prune exists:
#   `act` creates one named Docker volume per job+matrix cell (e.g.
#   `act-Build-and-Test-build-<hash>`) and REUSES it on subsequent runs.
#   `actions/checkout@v7` is emulated as a plain `docker cp` of the host
#   working tree into the container — files in the volume that no longer
#   exist on host (e.g. a header that was renamed in a refactor) are NOT
#   deleted by the cp, so the build sees BOTH the current source AND the
#   stale leftover at the old path. That produced phantom CI failures
#   (Kartend-fzuv: pre-bb23bb5 `src/modules/data/database/idatabasemanager.h`
#   surviving inside the per-job volume long after the refactor moved it
#   to `src/api/`). Every run with --reuse-the-default behaviour inherits
#   the rot. Each regular subcommand here auto-prunes job volumes that
#   match the matrix being run, so a `build:rel:gcc` invocation only
#   wipes the gcc Release volume — fast, predictable, and leaves the
#   actcache (apt-pkgs index, ccache tarballs) alone so warm runs stay
#   warm. Use the `prune` subcommand for the nuclear option.
set -euo pipefail

ACT_MIN_VERSION="0.2.86"

err() { printf '\033[31mci-local:\033[0m %s\n' "$*" >&2; }
info() { printf '\033[36mci-local:\033[0m %s\n' "$*" >&2; }

# Find act, preferring a newer one in ~/.local/bin over a possibly-stale
# system-packaged version. The Ubuntu/Gentoo-shipped act tends to lag the
# upstream release, and act 0.2.64 (Ubuntu noble's current version at time
# of writing) doesn't speak the node24 runtime that actions/cache@v6 needs.
find_act() {
  for candidate in "$HOME/.local/bin/act" "$(command -v act 2>/dev/null || true)"; do
    if [ -z "$candidate" ] || [ ! -x "$candidate" ]; then continue; fi
    local v
    v="$("$candidate" --version 2>/dev/null | awk '{print $NF}')"
    if version_ge "$v" "$ACT_MIN_VERSION"; then
      ACT_BIN="$candidate"
      ACT_VERSION="$v"
      return 0
    fi
  done
  return 1
}

# Pure-bash version compare: `version_ge A B` returns 0 if A >= B.
version_ge() {
  printf '%s\n%s\n' "$2" "$1" | sort -V -C
}

case "${1:-}" in
  -h|--help|help)
    sed -n '2,21p' "$0"
    exit 0
    ;;
esac

if ! find_act; then
  err "act >= $ACT_MIN_VERSION not found."
  err "Install it from https://github.com/nektos/act/releases (or your package manager"
  err "if it ships a recent enough build), then re-run this script."
  err
  err "Quick install (user-local, no root):"
  err "  curl -sL https://github.com/nektos/act/releases/download/v$ACT_MIN_VERSION/act_Linux_x86_64.tar.gz \\"
  err "    | tar -xz -C \"\$HOME/.local/bin\" act"
  exit 2
fi
info "using $ACT_BIN ($ACT_VERSION)"

# act's artifact server needs a writable directory; .actrc sets the path
# but doesn't create it. Doing so here makes the first invocation clean.
mkdir -p /tmp/act-artifacts

# Wipe the act-managed Docker volume(s) for a job before running it. The
# volume persists across runs (it's the workspace bind-mount target the
# emulated `actions/checkout` cps INTO), so a renamed/deleted file from a
# previous run is left lying around and the build resurrects it.
#
# `pattern` matches volume names with `docker volume ls --filter name=`,
# which is a SUBSTRING match. The act naming convention is
# `act-<workflow-name>-<job-name>[-<matrix-cell>]-<hash>` plus a paired
# `*-env` volume. Passing `act-Build-and-Test-build-` matches every
# `build` matrix cell; pass a longer prefix to scope tighter. Empty
# pattern intentionally matches every act-managed volume (the `prune`
# subcommand's nuclear case) — we never blank-default this in the
# regular flow.
#
# act-toolcache is the cross-job tool cache (setup-* actions) and never
# holds workspace files; preserved across all prune modes so warm runs
# don't re-download Node/Python/etc. each time.
prune_act_volumes() {
  local pattern="$1"
  if [ -z "$pattern" ]; then
    err "internal: prune_act_volumes called without a pattern"
    return 2
  fi
  local volumes
  volumes="$(docker volume ls -q --filter "name=$pattern" 2>/dev/null \
    | grep -v '^act-toolcache$' || true)"
  if [ -z "$volumes" ]; then
    return 0
  fi
  # A leftover container from a previous run can be holding the volume,
  # which makes `docker volume rm` refuse. Containers share the volume's
  # name prefix, so the same `--filter name=` matches the ones we need to
  # tear down first. Real GHA workers don't have this case — each runner
  # is a fresh VM — so this teardown is purely an act-side concern.
  local containers
  containers="$(docker ps -aq --filter "name=$pattern" 2>/dev/null || true)"
  if [ -n "$containers" ]; then
    echo "$containers" | xargs -r docker rm -f >/dev/null
  fi
  info "pruning stale act volumes (pattern=$pattern):"
  # sed indents each volume name for display; ${var//…} can't anchor per-line.
  # shellcheck disable=SC2001
  echo "$volumes" | sed 's/^/  /' >&2
  echo "$volumes" | xargs -r docker volume rm >/dev/null
}

# Fail fast (with the exact build command) when a docker:* subcommand is
# invoked before the kartend-ci image has ever been built — ci-local.md
# promises this check, and without it the first-use failure was docker's
# opaque "Unable to find image 'kartend-ci:latest' locally" pull error.
require_ci_image() {
  if ! docker image inspect kartend-ci >/dev/null 2>&1; then
    err "kartend-ci image not found. Build it once, then re-run:"
    err "  docker build -f .scripts/Dockerfile.ci -t kartend-ci ."
    exit 2
  fi

  # …and refuse an image older than the Dockerfile that defines it
  # (Kartend-n94ln). A stale image does not fail loudly — it produces WRONG
  # TEST RESULTS that look exactly like a regression in your own branch. The
  # case that motivated this: ee6b63ec added qt6-image-formats-plugins on
  # 2026-08-05, an image built before it lacked Qt's WebP codec, and two
  # ScrapePersistence cases failed on a fixture QImage::save(…, "WEBP") could
  # no longer produce. That reads as "my change broke the scraper" and costs a
  # diagnostic cycle to disprove.
  #
  # Compared against the LATER of the file's commit date and its mtime, so a
  # locally-edited-but-uncommitted Dockerfile counts too. Hard failure rather
  # than a warning: a warning scrolls off the top of a 30-minute run, and the
  # whole point of this wrapper is a verdict you can trust. The escape hatch
  # is one env var, for when you know the delta is cosmetic.
  local repo_root dockerfile
  repo_root=$(cd "$(dirname "$0")/.." && pwd)
  dockerfile="$repo_root/.scripts/Dockerfile.ci"
  [ -f "$dockerfile" ] || return 0
  local file_epoch commit_epoch image_iso image_epoch
  file_epoch=$(stat -c %Y "$dockerfile" 2>/dev/null || echo 0)
  commit_epoch=$(git log -1 --format=%ct -- "$dockerfile" 2>/dev/null || echo 0)
  [ "${commit_epoch:-0}" -gt "$file_epoch" ] && file_epoch=$commit_epoch
  image_iso=$(docker image inspect -f '{{.Created}}' kartend-ci 2>/dev/null || echo "")
  image_epoch=$(date -d "$image_iso" +%s 2>/dev/null || echo 0)

  if [ "$image_epoch" -gt 0 ] && [ "$file_epoch" -gt "$image_epoch" ]; then
    if [ -n "${KARTEND_CI_ALLOW_STALE_IMAGE:-}" ]; then
      info "kartend-ci image predates Dockerfile.ci — continuing (KARTEND_CI_ALLOW_STALE_IMAGE set)"
      return 0
    fi
    err "kartend-ci image is OLDER than .scripts/Dockerfile.ci — its results cannot be trusted."
    err "  image built:      $(date -d "@$image_epoch" '+%Y-%m-%d %H:%M')"
    err "  Dockerfile dated: $(date -d "@$file_epoch" '+%Y-%m-%d %H:%M')"
    err "Rebuild it, then re-run:"
    err "  docker build -f .scripts/Dockerfile.ci -t kartend-ci ."
    err "(Set KARTEND_CI_ALLOW_STALE_IMAGE=1 to run anyway.)"
    exit 2
  fi
}

# Map a subcommand to the volume-name pattern act will use for its jobs.
# Tight matching keeps unrelated jobs warm across a focused re-run.
volume_pattern_for() {
  case "$1" in
    build:rel:gcc|build:rel:clang|build:dbg:gcc|build:dbg:clang|build) echo "act-Build-and-Test-build-" ;;
    no-zstd)                                                            echo "act-Build-and-Test-build-no-zstd-" ;;
    asan|sanitizers)                                                    echo "act-Build-and-Test-sanitizers-" ;;
    tsan|thread-sanitizer)                                              echo "act-Build-and-Test-thread-sanitizer-" ;;
    # coverage lives in its own workflow (coverage.yml, `name: Coverage`),
    # so its volumes follow that workflow's name, not Build-and-Test's.
    coverage|cov)                                                       echo "act-Coverage-coverage-" ;;
    tidy|maintenance|maintenance-check)                                 echo "act-Build-and-Test-maintenance-check-" ;;
    all|"")                                                             echo "act-Build-and-Test-" ;;
    *)                                                                  echo "" ;;
  esac
}

ARG="${1:-all}"
shift || true

case "$ARG" in
  list|--list|-l)
    exec "$ACT_BIN" -l
    ;;

  prune)
    # Nuclear: remove every act-managed job volume (keeps act-toolcache).
    # Leaves ~/.cache/actcache alone so apt-pkgs / ccache snapshots survive.
    prune_act_volumes "act-Build-and-Test-"
    info "prune complete; ~/.cache/actcache preserved"
    exit 0
    ;;

  shell|sh)
    info "starting interactive container with the build job's environment"
    # Run a no-op job that just opens a shell; a hack but works without
    # extra workflow files. Easiest is to pull the runner image manually.
    image="$(awk -F= '/^-P ubuntu-24.04=/ { print $2 }' "$(dirname "$0")/../.actrc" | tr -d ' ')"
    image="${image#*=}"
    : "${image:=ghcr.io/catthehacker/ubuntu:full-24.04}"
    # Kartend-75w9n deliberately did NOT add --user here, unlike the docker:*
    # subcommands. This is the interactive escape hatch — the reason to open it
    # is usually to apt-get something or poke at the runner image, both of which
    # need root. The trade-off is that anything you BUILD in here lands
    # root-owned on the host, same failure the docker:* runs used to have; run
    # `sudo rm -rf build/<dir>` afterwards, or build under /tmp inside the
    # container instead of in the bind mount.
    exec docker run --rm -it \
      -v "$(cd "$(dirname "$0")/.." && pwd):/work" \
      -w /work \
      --cpus=4 --memory=16g \
      "$image" /bin/bash
    ;;

  build)
    prune_act_volumes "$(volume_pattern_for "$ARG")"
    exec "$ACT_BIN" -j build "$@"
    ;;

  build:rel:gcc)
    prune_act_volumes "$(volume_pattern_for "$ARG")"
    exec "$ACT_BIN" -j build --matrix build_type:Release --matrix compiler:gcc "$@"
    ;;
  build:rel:clang)
    prune_act_volumes "$(volume_pattern_for "$ARG")"
    exec "$ACT_BIN" -j build --matrix build_type:Release --matrix compiler:clang "$@"
    ;;
  build:dbg:gcc)
    prune_act_volumes "$(volume_pattern_for "$ARG")"
    exec "$ACT_BIN" -j build --matrix build_type:Debug --matrix compiler:gcc "$@"
    ;;
  build:dbg:clang)
    prune_act_volumes "$(volume_pattern_for "$ARG")"
    exec "$ACT_BIN" -j build --matrix build_type:Debug --matrix compiler:clang "$@"
    ;;

  no-zstd)
    prune_act_volumes "$(volume_pattern_for "$ARG")"
    exec "$ACT_BIN" -j build-no-zstd "$@"
    ;;
  asan|sanitizers)
    prune_act_volumes "$(volume_pattern_for "$ARG")"
    exec "$ACT_BIN" -j sanitizers "$@"
    ;;
  tsan|thread-sanitizer)
    prune_act_volumes "$(volume_pattern_for "$ARG")"
    exec "$ACT_BIN" -j thread-sanitizer "$@"
    ;;
  coverage|cov)
    info "note: act's mock artifact server fails on actions/upload-artifact@v7"
    info "      with a 'mime_type' protobuf error. The coverage *measurement*"
    info "      will succeed; only the upload step at the end will fail."
    info "      That step works correctly on real GitHub Actions."
    prune_act_volumes "$(volume_pattern_for "$ARG")"
    # The coverage job lives in coverage.yml, not the build.yml that .actrc
    # pins via -W. CLI flags are appended after .actrc's contents and act's
    # flag parsing is last-one-wins, so this -W overrides the .actrc default
    # for exactly this invocation.
    exec "$ACT_BIN" -W .github/workflows/coverage.yml -j coverage "$@"
    ;;
  tidy|maintenance|maintenance-check)
    prune_act_volumes "$(volume_pattern_for "$ARG")"
    exec "$ACT_BIN" -j maintenance-check "$@"
    ;;

  # docker:* subcommands — bypass act entirely, use the kartend-ci image
  # (Dockerfile.ci) directly. Faster turn-around than act since there's no
  # workflow YAML parsing, artifact mock server, or job-volume bookkeeping;
  # but skips the GitHub Actions surface (env vars, services, matrix
  # expansion) so it only reproduces the *build/lint/test* surface of a job.
  # Most CI failures are in that surface, so this is the right tool for
  # iterating on "why does my push fail CI" without paying act's overhead.
  #
  # The image (kartend-ci) is built once via `docker build -f
  # .scripts/Dockerfile.ci -t kartend-ci .` — re-running auto-skips when the
  # cache is warm. Each subcommand below calls require_ci_image first, so a
  # missing image fails fast with that build command instead of docker's
  # opaque registry-pull error.
  #
  # Kartend-75w9n — every docker:* run below passes
  #   --user "$(id -u):$(id -g)" -e HOME=/tmp
  # The repo is bind-mounted read-write, so a container running as root leaves
  # its build tree (build/ninja-maintenance, build/Release-clang, build/TSan)
  # root-owned ON THE HOST. The host user then cannot delete it, and a native
  # `.scripts/build.sh --maintenance` dies at "Prepare build directory". It
  # hides well: each block re-creates its own build dir as root, so repeating
  # the SAME subcommand self-heals and only the host user is stuck.
  #
  # HOME=/tmp because a uid with no /etc/passwd entry gets HOME=/, which is not
  # writable for a non-root user — ccache defaults its cache_dir to
  # $HOME/.cache/ccache and would fail to create it. /tmp keeps that cache
  # exactly as ephemeral as it already was: the image sets HOME=/root and these
  # containers are --rm, so the ccache directory has never survived a run and
  # no cross-run hit rate is being given up here.
  docker:tidy|docker:maintenance|docker:maintenance-check)
    require_ci_image
    info "running kartend-ci maintenance-check (clang-tidy + format + IWYU + cppcheck)"
    # Same image / Qt / clang as CI's maintenance-check, but NOT an exact
    # match: this runs the full advisory analysis trio over src/ without
    # test TUs, while CI configures --tests and runs a diff-scoped (PR) or
    # curated enforcing (push) clang-tidy pass — see docs/dev/ci-local.md.
    info "same image, Qt, and clang as CI maintenance-check (analysis scope differs — see ci-local.md)"
    exec docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp \
      -v "$(cd "$(dirname "$0")/.." && pwd):/src" kartend-ci bash -c '
      # Kartend-75w9n: the clang-format pin used to be symlinked into
      # /usr/local/bin here at runtime, which was both redundant and the ONLY
      # step in this block that required root — Dockerfile.ci already bakes the
      # identical symlink at image-build time, where it belongs. The pin itself
      # is owned by .scripts/lib/clang-format-version.sh and CI verifies the
      # literal stays in sync (Kartend-gv2xq).
      cd /src
      # Kartend-75w9n: a tree left by a PRE-fix root-owned run cannot be removed
      # by the unprivileged user we now run as, and cmake would then fail deep in
      # "Prepare build directory" with a bare Permission denied. Say what to do.
      if ! rm -rf build/ninja-maintenance 2>/dev/null; then
        echo "ERROR: cannot remove build/ninja-maintenance as $(id -u):$(id -g)."
        echo "It is almost certainly root-owned, left by a docker:* run from before"
        echo "Kartend-75w9n made these containers run as the invoking user."
        echo "Clear it once on the HOST, then re-run this command:"
        echo "    sudo rm -rf build/ninja-maintenance"
        exit 1
      fi
      bash .scripts/build.sh --maintenance --format-check
      rc=$?
      echo
      echo "=== clang-tidy promoted errors ==="
      grep "error:" build/ninja-maintenance/logs/clang-tidy.log 2>/dev/null | head -20 || echo "(none)"
      echo "=== clang-format drift ==="
      cat build/ninja-maintenance/logs/clang-format.log 2>/dev/null | head -40
      exit $rc
    '
    ;;

  docker:build|docker:release|docker:release-clang)
    require_ci_image
    info "running kartend-ci Release+clang build (matches CI's build (Release, clang) matrix cell)"
    exec docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp \
      -v "$(cd "$(dirname "$0")/.." && pwd):/src" kartend-ci bash -c '
      cd /src
      # See the Kartend-75w9n note in docker:tidy for why this is guarded.
      if ! rm -rf build/Release-clang 2>/dev/null; then
        echo "ERROR: cannot remove build/Release-clang as $(id -u):$(id -g) —"
        echo "root-owned leftover from a pre-Kartend-75w9n run. On the HOST:"
        echo "    sudo rm -rf build/Release-clang"
        exit 1
      fi
      cmake -S . -B build/Release-clang -G Ninja -DCMAKE_BUILD_TYPE=Release -DKARTEND_BUILD_TESTS=ON -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
      cmake --build build/Release-clang --parallel
      QT_QPA_PLATFORM=offscreen ctest --test-dir build/Release-clang --output-on-failure -LE benchmark
    '
    ;;

  docker:tsan|docker:thread-sanitizer)
    require_ci_image
    info "running kartend-ci TSan build (matches CI thread-sanitizer job)"
    # --security-opt seccomp=unconfined: TSan calls
    # personality(ADDR_NO_RANDOMIZE) at startup to switch off ASLR for its
    # fixed shadow layout, and Docker's default seccomp profile whitelists
    # only a handful of personality values — this one EPERMs, which TSan
    # treats as a fatal CHECK (tsan_platform_linux.cpp:282) before main()
    # runs. Verified 2026-08-05 on kernel 7.1.4-cachyos-optim: with the
    # profile relaxed a TSan probe binary runs clean, and the older
    # 'unexpected memory mapping' failure from the 7.1.2 era is gone.
    #
    # setarch x86_64 -R on the ctest step: kernel 7.2 moved the mmap layout
    # again and the container's GCC 13.3 TSan runtime dies with 'FATAL:
    # ThreadSanitizer: unexpected memory mapping' before main() on nearly
    # every test even WITH the relaxed seccomp profile (the personality()
    # call now succeeds but the resulting layout still isn't the one this
    # libtsan expects). Disabling ASLR outright for the test processes via
    # setarch gives the runtime the fixed layout it wants. Verified
    # 2026-08-30 on 7.2.0-cachyos1-optim: probe binary then the full suite
    # run clean under the wrapper. Harmless no-op on kernels where the
    # layout was already acceptable, so it is applied unconditionally.
    # History: 7.1.2 era both paths failed deterministically; 7.1.4 fixed
    # by seccomp=unconfined alone; 7.2 additionally needs setarch -R.
    exec docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp \
      --security-opt seccomp=unconfined \
      -v "$(cd "$(dirname "$0")/.." && pwd):/src" kartend-ci bash -c '
      cd /src
      pulseaudio --start --exit-idle-time=-1 2>/dev/null || true
      # See the Kartend-75w9n note in docker:tidy for why this is guarded.
      if ! rm -rf build/TSan 2>/dev/null; then
        echo "ERROR: cannot remove build/TSan as $(id -u):$(id -g) —"
        echo "root-owned leftover from a pre-Kartend-75w9n run. On the HOST:"
        echo "    sudo rm -rf build/TSan"
        exit 1
      fi
      cmake -S . -B build/TSan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DKARTEND_BUILD_TESTS=ON -DKARTEND_ENABLE_TSAN=ON
      cmake --build build/TSan --parallel
      # -LE benchmark matches every CI ctest invocation (Kartend-egd3a): the
      # QBENCHMARK suites only run in the nightly benchmark job, never under
      # sanitizers or the PR matrix.
      QT_QPA_PLATFORM=offscreen TSAN_OPTIONS=halt_on_error=1:suppressions=$PWD/tests/suppressions/tsan.txt setarch x86_64 -R ctest --test-dir build/TSan --output-on-failure -LE benchmark
    '
    ;;

  docker:all)
    info "running kartend-ci docker:tidy then docker:build then docker:tsan in sequence (~30 min total)"
    "$0" docker:tidy && "$0" docker:build && "$0" docker:tsan
    exit $?
    ;;

  all|"")
    info "running every job sequentially (~1-1.5 hr first time, faster on cache hits)"
    prune_act_volumes "$(volume_pattern_for "$ARG")"
    exec "$ACT_BIN" --concurrent-jobs 1 "$@"
    ;;

  --)
    exec "$ACT_BIN" "$@"
    ;;

  *)
    err "unknown subcommand: $ARG"
    err "run '.scripts/ci-local.sh help' for usage"
    exit 2
    ;;
esac
