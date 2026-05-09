#!/usr/bin/env bash
# Run the GitHub Actions CI pipeline locally via `act` (nektos/act).
#
# Backed by .actrc at the repo root, which pins the runner image, container
# resource caps to match GHA's ubuntu-24.04 runner (4 vCPUs, 16GB), and the
# artifact server path. The wrapper here adds:
#   * version check (act 0.2.86+ is required for actions/cache@v5 = node24)
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
#   .scripts/ci-local.sh -- <act args>   # raw passthrough, e.g. .scripts/ci-local.sh -- -j build --verbose
set -euo pipefail

ACT_MIN_VERSION="0.2.86"

err() { printf '\033[31mci-local:\033[0m %s\n' "$*" >&2; }
info() { printf '\033[36mci-local:\033[0m %s\n' "$*" >&2; }

# Find act, preferring a newer one in ~/.local/bin over a possibly-stale
# system-packaged version. The Ubuntu/Gentoo-shipped act tends to lag the
# upstream release, and act 0.2.64 (Ubuntu noble's current version at time
# of writing) doesn't speak the node24 runtime that actions/cache@v5 needs.
find_act() {
  for candidate in "$HOME/.local/bin/act" "$(command -v act 2>/dev/null || true)"; do
    [ -n "$candidate" ] && [ -x "$candidate" ] || continue
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

ARG="${1:-all}"
shift || true

case "$ARG" in
  list|--list|-l)
    exec "$ACT_BIN" -l
    ;;

  shell|sh)
    info "starting interactive container with the build job's environment"
    # Run a no-op job that just opens a shell; a hack but works without
    # extra workflow files. Easiest is to pull the runner image manually.
    image="$(awk -F= '/^-P ubuntu-24.04=/ { print $2 }' "$(dirname "$0")/../.actrc" | tr -d ' ')"
    image="${image#*=}"
    : "${image:=ghcr.io/catthehacker/ubuntu:full-24.04}"
    exec docker run --rm -it \
      -v "$(cd "$(dirname "$0")/.." && pwd):/work" \
      -w /work \
      --cpus=4 --memory=16g \
      "$image" /bin/bash
    ;;

  build)
    exec "$ACT_BIN" -j build "$@"
    ;;

  build:rel:gcc)
    exec "$ACT_BIN" -j build --matrix build_type:Release --matrix compiler:gcc "$@"
    ;;
  build:rel:clang)
    exec "$ACT_BIN" -j build --matrix build_type:Release --matrix compiler:clang "$@"
    ;;
  build:dbg:gcc)
    exec "$ACT_BIN" -j build --matrix build_type:Debug --matrix compiler:gcc "$@"
    ;;
  build:dbg:clang)
    exec "$ACT_BIN" -j build --matrix build_type:Debug --matrix compiler:clang "$@"
    ;;

  no-zstd)
    exec "$ACT_BIN" -j build-no-zstd "$@"
    ;;
  asan|sanitizers)
    exec "$ACT_BIN" -j sanitizers "$@"
    ;;
  tsan|thread-sanitizer)
    exec "$ACT_BIN" -j thread-sanitizer "$@"
    ;;
  coverage|cov)
    info "note: act's mock artifact server fails on actions/upload-artifact@v7"
    info "      with a 'mime_type' protobuf error. The coverage *measurement*"
    info "      will succeed; only the upload step at the end will fail."
    info "      That step works correctly on real GitHub Actions."
    exec "$ACT_BIN" -j coverage "$@"
    ;;
  tidy|maintenance|maintenance-check)
    exec "$ACT_BIN" -j maintenance-check "$@"
    ;;

  all|"")
    info "running every job sequentially (~1-1.5 hr first time, faster on cache hits)"
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
