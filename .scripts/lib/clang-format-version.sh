# Single source of truth for the pinned clang-format major version.
#
# CI hard-fails on clang-format drift, and the pin is consumed in several
# places that used to each hardcode "19" / "clang-format-19":
#   - .scripts/lib/quality.sh   (build.sh --maintenance --format-check/-apply)
#   - .scripts/git-hooks/pre-commit
#   - .github/workflows/build.yml (script-lint + maintenance-check)
#   - .scripts/Dockerfile.ci / .scripts/ci-local.sh (the container symlink)
# Drift between those call sites silently re-introduces "passes locally,
# fails CI" churn, so the version lives here once. The CI workflows read it
# via `KARTEND_CLANG_FORMAT_VERSION=$(. .scripts/lib/clang-format-version.sh
# && echo "$KARTEND_CLANG_FORMAT_VERSION")` (or just hardcode against this
# file's value, which the script-lint job verifies stays in sync).
#
# POSIX-sourceable: no bashisms, so build.sh's bash modules, the bash
# pre-commit hook, and a plain `sh -c '. ...'` in CI can all consume it.
# shellcheck shell=sh

KARTEND_CLANG_FORMAT_VERSION=19

# Resolve a clang-format binary that matches CI's pinned version. Ubuntu
# 24.04 ships v18 and current Arch ships v21; both format differently enough
# from the pin to produce false-positive drift on commits that pass CI.
# Echoes the resolved path/name on stdout and returns 0, or returns 1 (no
# output) when no matching candidate is found. Consumed by both
# .scripts/lib/quality.sh and .scripts/git-hooks/pre-commit so the lookup
# order can't drift between them.
resolve_clang_format() {
  _kcf_ver="$KARTEND_CLANG_FORMAT_VERSION"
  # 1. Debian/Ubuntu `clang-format-NN` package
  if command -v "clang-format-${_kcf_ver}" >/dev/null 2>&1; then
    printf 'clang-format-%s\n' "$_kcf_ver"; return 0
  fi
  # 2. Arch/Gentoo llvm slot
  if [ -x "/usr/lib/llvm/${_kcf_ver}/bin/clang-format" ]; then
    printf '/usr/lib/llvm/%s/bin/clang-format\n' "$_kcf_ver"; return 0
  fi
  # 3. CI symlink style (Dockerfile.ci + build.yml both write here)
  if [ -x /usr/local/bin/clang-format ]; then
    printf '/usr/local/bin/clang-format\n'; return 0
  fi
  # 4. Bare clang-format, but only if its --version reports the pinned major
  if command -v clang-format >/dev/null 2>&1 \
     && clang-format --version 2>/dev/null | grep -qE "version ${_kcf_ver}\\."; then
    printf 'clang-format\n'; return 0
  fi
  return 1
}

# Human-readable "where to get the right version" hint, reused by the
# fail-loud paths so the remediation message stays consistent. Printed to
# stderr by callers.
clang_format_missing_hint() {
  _kcf_ver="$KARTEND_CLANG_FORMAT_VERSION"
  printf 'No clang-format-%s found. The formatter is pinned to v%s to match CI.\n' "$_kcf_ver" "$_kcf_ver"
  printf 'Looked for: clang-format-%s, /usr/lib/llvm/%s/bin/clang-format,\n' "$_kcf_ver" "$_kcf_ver"
  # SC2016: the backticked clang-format here is literal help text, not a
  # command substitution.
  # shellcheck disable=SC2016
  printf '            /usr/local/bin/clang-format, and `clang-format` reporting v%s.x.\n' "$_kcf_ver"
  printf 'Fix: install clang-format-%s (apt.llvm.org on Debian/Ubuntu), or run the\n' "$_kcf_ver"
  printf '     check through the kartend-ci container, which symlinks the pinned\n'
  printf '     binary to /usr/local/bin/clang-format (see docs/dev/ci-local.md).\n'
}
