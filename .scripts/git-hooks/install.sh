#!/usr/bin/env bash
#
# Install Kartend's git hooks. Idempotent — re-running is safe.
#
# We don't auto-install at clone time because git hooks are per-checkout
# state, not tracked content. Contributors opt in by running this once.
#
# Coexistence with beads:
#   If `git config core.hooksPath` is set (e.g. to `.beads/hooks/` after
#   `bd hooks install`), this script writes to THAT directory instead of
#   `.git/hooks/`. Beads-managed hooks bracket their content with
#   `--- BEGIN/END BEADS INTEGRATION ---` markers and document that
#   "any user content outside the markers is preserved across installs
#   and upgrades", so we append a similarly-marked project section after
#   beads' END marker. The project section runs the matching script under
#   .scripts/git-hooks/ and propagates its exit code.

set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
HOOKS_SRC="$ROOT/.scripts/git-hooks"

# Resolve the active hooks directory. core.hooksPath may be relative
# (interpreted by git as relative to the repo root) or absolute.
HOOKS_PATH_CFG="$(git config --get core.hooksPath || true)"
if [ -n "$HOOKS_PATH_CFG" ]; then
  case "$HOOKS_PATH_CFG" in
    /*) HOOKS_DST="$HOOKS_PATH_CFG" ;;
    *)  HOOKS_DST="$ROOT/$HOOKS_PATH_CFG" ;;
  esac
else
  HOOKS_DST="$ROOT/.git/hooks"
fi

if [ ! -d "$HOOKS_DST" ]; then
  echo "Error: hooks directory $HOOKS_DST does not exist (not a git checkout, or core.hooksPath misconfigured?)" >&2
  exit 1
fi

PROJECT_BEGIN="# --- BEGIN KARTEND PROJECT HOOK v1.0.0 ---"
PROJECT_END="# --- END KARTEND PROJECT HOOK v1.0.0 ---"

append_project_section() {
  local dst="$1"
  local name="$2"

  if grep -qF "$PROJECT_BEGIN" "$dst" 2>/dev/null; then
    echo "  $name: project section already present in $dst"
    return
  fi

  # POSIX sh inside the appended section so it works with beads' /usr/bin/env sh
  # shim. Delegates to the real script under .scripts/git-hooks/.
  cat >> "$dst" <<EOF

$PROJECT_BEGIN
# Managed by .scripts/git-hooks/install.sh. Runs the Kartend project hook
# defined in .scripts/git-hooks/$name.
_kartend_root="\$(git rev-parse --show-toplevel)"
"\$_kartend_root/.scripts/git-hooks/$name" "\$@"
_kartend_exit=\$?
if [ \$_kartend_exit -ne 0 ]; then exit \$_kartend_exit; fi
$PROJECT_END
EOF
  echo "  $name: appended project section to $dst"
}

installed=0
for src in "$HOOKS_SRC"/*; do
  name="$(basename "$src")"
  case "$name" in
    install.sh|README*|*.sample) continue ;;
  esac
  [ -f "$src" ] || continue
  [ -x "$src" ] || chmod +x "$src"

  dst="$HOOKS_DST/$name"

  if [ -e "$dst" ] && grep -qF "BEGIN BEADS INTEGRATION" "$dst" 2>/dev/null; then
    # Beads-managed (or similar markered) hook — coexist by appending.
    append_project_section "$dst" "$name"
    installed=$((installed + 1))
  elif [ -L "$dst" ] || [ ! -e "$dst" ]; then
    # Fresh install or replacing our own symlink.
    ln -sf "../../.scripts/git-hooks/$name" "$dst"
    echo "  $name: symlinked to $dst"
    installed=$((installed + 1))
  else
    # Plain file already exists and isn't ours — don't clobber.
    echo "Skipping $name: $dst already exists and is not a symlink or markered hook."
    echo "  Remove it manually if you want this hook, or merge by hand."
  fi
done

if [ "$installed" -eq 0 ]; then
  echo "No hooks installed."
else
  echo "Done. $installed hook(s) active in $HOOKS_DST"
fi
