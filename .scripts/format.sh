#!/usr/bin/env bash
# shellcheck source-path=SCRIPTDIR
#
# Run the CI-pinned clang-format over Kartend sources.
#
# WHY THIS EXISTS (Kartend-o07rm). Formatting by hand meant typing a docker
# invocation, and the obvious one is wrong:
#
#     docker run --rm -v "$PWD":/src -w /src kartend-ci clang-format -i <files>
#
# With no --user the container runs as root, so every file it rewrites in place
# becomes root-owned in the working tree. Nothing fails at the time — the format
# succeeds, the build succeeds, the commit succeeds. It surfaces days later as a
# permission-denied error the next time that file is edited, reading like a
# tooling bug rather than something the formatter did. Eleven files across src/
# and tests/ had accumulated that way by 2026-08-13.
#
# .scripts/ci-local.sh always got this right; the gap was only ever in
# hand-written invocations. So this script hard-codes the correct one and
# nobody — human or agent — has to remember the flag.
#
# It also prefers a NATIVE pinned formatter when one is installed, which is both
# faster and sidesteps the ownership question entirely. Docker is the fallback,
# not the default.
#
# Usage:
#   .scripts/format.sh                  # format files changed vs HEAD
#   .scripts/format.sh --check          # report drift, change nothing (exit 1 on drift)
#   .scripts/format.sh --all            # every .cpp/.h under src/ and tests/
#   .scripts/format.sh path/a.cpp ...   # only these files
#
# --check is what CI enforces; the pre-commit hook runs the same pinned binary
# against staged content.

set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

# Shared pinned version + resolver (Kartend-gv2xq): the version lives in exactly
# one place so this script, the pre-commit hook, build.sh's quality lib and CI
# cannot drift apart. Provides KARTEND_CLANG_FORMAT_VERSION,
# resolve_clang_format and clang_format_missing_hint.
# shellcheck source=lib/clang-format-version.sh
. "$ROOT/.scripts/lib/clang-format-version.sh"

red()    { printf '\033[31m%s\033[0m\n' "$*"; }
yellow() { printf '\033[33m%s\033[0m\n' "$*"; }
green()  { printf '\033[32m%s\033[0m\n' "$*"; }

usage() {
  cat <<'EOF'
Run the CI-pinned clang-format over Kartend sources.

Prefers a natively installed pinned clang-format; falls back to the kartend-ci
container, always with --user so it cannot leave root-owned files behind
(Kartend-o07rm).

Usage:
  .scripts/format.sh                  format files changed vs HEAD (default)
  .scripts/format.sh --check          report drift, change nothing (exit 1 on drift)
  .scripts/format.sh --all            every .cpp/.h under src/ and tests/
  .scripts/format.sh path/a.cpp ...   only these files
EOF
}

mode="apply"
scope="changed"
files=()

for arg in "$@"; do
  case "$arg" in
    --check)   mode="check" ;;
    --all)     scope="all" ;;
    -h|--help) usage; exit 0 ;;
    -*)        red "Unknown option: $arg"; usage >&2; exit 2 ;;
    *)         scope="explicit"; files+=("$arg") ;;
  esac
done

# ── File selection ───────────────────────────────────────────────────────────
case "$scope" in
  explicit) : ;; # already in files[]
  all)
    mapfile -d '' files < <(
      find src tests -type f \( -name '*.cpp' -o -name '*.h' \) -print0
    )
    ;;
  changed)
    # Worktree state, not the index: this script formats files on disk, so the
    # working-tree copy is what matters (the pre-commit hook is the one that
    # deliberately reads staged blobs instead). --diff-filter=ACM skips
    # deletions; the untracked pass catches brand-new files, which `git diff`
    # never reports.
    mapfile -d '' files < <(
      {
        git diff --name-only --diff-filter=ACM -z HEAD -- '*.cpp' '*.h'
        git ls-files --others --exclude-standard -z -- '*.cpp' '*.h'
      } | sort -zu
    )
    ;;
esac

# Drop anything that vanished between selection and now.
present=()
for f in "${files[@]}"; do
  [ -n "$f" ] && [ -f "$f" ] && present+=("$f")
done
files=("${present[@]}")

if [ ${#files[@]} -eq 0 ]; then
  green "No .cpp/.h files to format."
  exit 0
fi

# ── Formatter selection: native pinned binary, else the CI container ─────────
# Both branches run the SAME pinned major version; the container symlinks it to
# /usr/local/bin/clang-format at image-build time (Dockerfile.ci).
run_format() {
  if [ "$use_docker" = "true" ]; then
    # --user is the whole point of this script: without it the container writes
    # as root. -e HOME=/tmp keeps clang-format from probing a home it cannot
    # read. Invocation deliberately mirrors .scripts/ci-local.sh.
    docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp \
      -v "$ROOT:/src" -w /src kartend-ci clang-format "$@"
  else
    "$CF" "$@"
  fi
}

use_docker=false
if CF=$(resolve_clang_format); then
  : # native pinned binary found
elif docker image inspect kartend-ci >/dev/null 2>&1; then
  use_docker=true
  CF="kartend-ci container"
else
  red "No pinned formatter available."
  clang_format_missing_hint >&2
  echo >&2
  echo "This script would also have accepted the kartend-ci container, but that" >&2
  echo "image is not built. Build it with: .scripts/ci-local.sh build-image" >&2
  exit 1
fi

# ── Run ──────────────────────────────────────────────────────────────────────
if [ "$mode" = "check" ]; then
  # Tee the diagnostics through so the user still sees them, while capturing
  # them to derive the fix hint.
  drift_output=$(run_format --style=file --dry-run --Werror "${files[@]}" 2>&1) && {
    green "clang-format clean (${#files[@]} file(s), using $CF)."
    exit 0
  }
  printf '%s\n' "$drift_output" >&2

  # The hint must name ONLY the files that actually drifted, never the whole
  # input set. In a shared working tree the input can include files another
  # session is mid-edit on, and a hint listing those invites reformatting
  # somebody else's in-flight work with one paste. clang-format reports
  # "<path>:<line>:<col>: error: ..." per violation, so the paths are recoverable.
  mapfile -t drifted < <(
    printf '%s\n' "$drift_output" | sed -n 's/^\(.*\):[0-9]\+:[0-9]\+: error: .*/\1/p' | sort -u
  )
  red "clang-format drift in ${#drifted[@]} file(s)."
  if [ ${#drifted[@]} -gt 0 ]; then
    yellow "Fix with: .scripts/format.sh ${drifted[*]}"
  else
    # Werror fired without a parseable path (malformed file, unreadable input).
    yellow "Fix with: .scripts/format.sh <the file(s) named above>"
  fi
  exit 1
fi

run_format --style=file -i "${files[@]}"
green "Formatted ${#files[@]} file(s) using $CF."

# Belt and braces: if an EARLIER hand-written docker run (from before this
# script existed) left root-owned files behind, say so now rather than letting
# it surface as a mystery permission-denied error mid-edit. This script cannot
# itself create them.
mapfile -d '' root_owned < <(
  find . -path ./build -prune -o -path ./.git -prune -o -user root -print0 2>/dev/null
)
if [ ${#root_owned[@]} -gt 0 ]; then
  echo
  yellow "Heads up: ${#root_owned[@]} root-owned file(s) in the working tree, left by an"
  yellow "earlier docker run that omitted --user (Kartend-o07rm). Reclaim them with:"
  printf '    sudo chown "%s:%s"' "$USER" "$USER"
  printf ' %q' "${root_owned[@]}"
  printf '\n'
fi
