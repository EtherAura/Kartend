#!/usr/bin/env bash
#
# Install Kartend's git hooks. Idempotent — re-running is safe.
#
# We don't auto-install at clone time because git hooks are per-checkout
# state, not tracked content. Contributors opt in by running this once.

set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
HOOKS_SRC="$ROOT/.scripts/git-hooks"
HOOKS_DST="$ROOT/.git/hooks"

if [ ! -d "$HOOKS_DST" ]; then
  echo "Error: $HOOKS_DST does not exist (not a git checkout?)" >&2
  exit 1
fi

installed=0
for src in "$HOOKS_SRC"/*; do
  name="$(basename "$src")"
  case "$name" in
    install.sh|README*|*.sample) continue ;;
  esac
  [ -f "$src" ] || continue

  dst="$HOOKS_DST/$name"
  # If a non-symlink hook already exists, leave it alone — the user may have
  # customized it. Print a hint instead of clobbering.
  if [ -e "$dst" ] && [ ! -L "$dst" ]; then
    echo "Skipping $name: $dst already exists and is not a symlink."
    echo "  Remove it manually if you want this hook, or merge by hand."
    continue
  fi

  ln -sf "../../.scripts/git-hooks/$name" "$dst"
  echo "Installed $name → $(readlink "$dst")"
  installed=$((installed + 1))
done

if [ "$installed" -eq 0 ]; then
  echo "No hooks installed."
else
  echo "Done. $installed hook(s) active."
fi
