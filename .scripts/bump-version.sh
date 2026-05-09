#!/usr/bin/env bash
#
# Bump Kartend's project version in every place it's tracked. Single
# entry-point so a release prep can't accidentally update PKGBUILD while
# leaving the AppStream metainfo on the previous version.
#
# Usage:
#   .scripts/bump-version.sh 0.0.6
#
# Effects (all local — no commit, no push, no tag):
#   - VERSION                                    overwritten
#   - packaging/PKGBUILD                         pkgver= updated
#   - packaging/kartend-<old>.ebuild  →  packaging/kartend-<new>.ebuild
#                                                (git mv if tracked)
#   - packaging/io.github.EtherAura.Kartend.metainfo.xml
#                                                <release version="<new>"
#                                                date="<today UTC>"/> inserted
#                                                at the top of <releases>
#
# After running, review with `git diff` and `git status`, then commit
# (typical message: "chore(release): bump version to X.Y.Z") and tag.
# release.yml will fail the tag if any of the above drift apart, so this
# script's job is to keep them in sync.
#
# This script does NOT touch sha256sums in PKGBUILD — that is filled in
# automatically by .github/workflows/release.yml after the tag is pushed
# (see the 'Update PKGBUILD sha256sums on default branch' step).

set -euo pipefail

usage() {
  echo "Usage: $0 <new-version>" >&2
  echo "  e.g. $0 0.0.6" >&2
  exit 1
}

[ "$#" -eq 1 ] || usage

NEW_VERSION="$1"

if ! [[ "$NEW_VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Error: '$NEW_VERSION' is not a MAJOR.MINOR.PATCH semver triple." >&2
  exit 1
fi

# Move to repo root regardless of where the script is invoked from.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT"

if [ ! -f VERSION ]; then
  echo "Error: VERSION file not found at $ROOT/VERSION" >&2
  exit 1
fi

OLD_VERSION="$(tr -d '[:space:]' < VERSION)"

if [ "$OLD_VERSION" = "$NEW_VERSION" ]; then
  echo "VERSION already at $NEW_VERSION — nothing to do."
  exit 0
fi

echo "Bumping $OLD_VERSION → $NEW_VERSION"

# 1. VERSION file
echo "$NEW_VERSION" > VERSION

# 2. PKGBUILD pkgver=
sed -i -E "s/^pkgver=.*/pkgver=${NEW_VERSION}/" packaging/PKGBUILD

# 3. ebuild rename (use git mv if tracked, plain mv otherwise)
OLD_EBUILD="packaging/kartend-${OLD_VERSION}.ebuild"
NEW_EBUILD="packaging/kartend-${NEW_VERSION}.ebuild"
if [ -f "$OLD_EBUILD" ]; then
  if git ls-files --error-unmatch "$OLD_EBUILD" >/dev/null 2>&1; then
    git mv "$OLD_EBUILD" "$NEW_EBUILD"
  else
    mv "$OLD_EBUILD" "$NEW_EBUILD"
  fi
fi

# 4. AppStream metainfo: insert a new <release> entry at the top of <releases>.
# The existing entries below stay untouched (full release history is the
# AppStream convention).
METAINFO="packaging/io.github.EtherAura.Kartend.metainfo.xml"
TODAY="$(date -u +%Y-%m-%d)"
if [ -f "$METAINFO" ]; then
  # Insert immediately after the <releases> opening tag. Indentation matches
  # the existing entries (4 spaces per the file's convention).
  python3 - "$METAINFO" "$NEW_VERSION" "$TODAY" <<'PY'
import re
import sys

path, version, date = sys.argv[1], sys.argv[2], sys.argv[3]
with open(path, encoding="utf-8") as f:
    content = f.read()

# Detect indentation of existing <release> entries by looking at the first one.
match = re.search(r"^( *)<release\b", content, re.MULTILINE)
indent = match.group(1) if match else "    "
new_entry = f'{indent}<release version="{version}" date="{date}"/>\n'

# Insert right after the <releases> opening tag (handle either form).
new_content, count = re.subn(
    r"(<releases>\s*\n)",
    r"\1" + new_entry,
    content,
    count=1,
)
if count == 0:
    # Fallback: <releases> on its own line might have leading whitespace.
    new_content, count = re.subn(
        r"(<releases>[^\n]*\n)",
        r"\1" + new_entry,
        content,
        count=1,
    )
if count == 0:
    sys.stderr.write("Could not find <releases> opening tag in metainfo XML\n")
    sys.exit(1)

with open(path, "w", encoding="utf-8") as f:
    f.write(new_content)
PY
fi

cat <<EOF

✓ Version bumped to ${NEW_VERSION}.

Files changed:
  VERSION
  packaging/PKGBUILD
  ${NEW_EBUILD}  (renamed from ${OLD_EBUILD})
  ${METAINFO}

Review with: git diff && git status
Then commit and tag:
  git commit -am "chore(release): bump version to ${NEW_VERSION}"
  git tag v${NEW_VERSION}
  git push && git push --tags

The release.yml workflow will validate consistency and publish the release.
EOF
