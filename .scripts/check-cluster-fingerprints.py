#!/usr/bin/env python3
"""Settings-cluster fingerprint completeness lint for Kartend (Kartend-10sb1).

THE DEFECT CLASS. SettingsManager's hot-reload diff keeps a per-cluster HASH
rather than a full CollectionConfig copy (Kartend-lc58a), so dirty detection
relies on each leaf cluster's qHash catching any field change. A field added to
a cluster struct but to NEITHER operator== nor qHash makes a real user edit
fingerprint clean, and its fine-grained hot-reload signal never fires. That has
shipped twice — SidebarAppearance::sidebarJustification (Kartend-g3fth) and
CollectionBackground::toolbarColorSource (Kartend-5lzvl) — and both were found
by human audit, not by the suite.

WHY A LINT AND NOT A TEST. tests/modules/settings/test_collectiondifffingerprint
.cpp asserts, per field, that qHash and operator== both react. That catches the
two functions drifting apart, but only for fields somebody wrote a case for: a
field missing from all three places has no case and stays invisible. Closing
that needs to compare the struct's MEMBER LIST against the two function bodies
and against the test — which plain C++ cannot express without reflection. Hence
a lint, matching the .scripts/check-*.py convention this repo already uses for
rules the compiler cannot hold.

WHAT IT CHECKS, per cluster struct that has a qHash overload:
  1. every member appears in operator== (skipped for `= default`, which covers
     every member by construction and cannot drift)
  2. every member appears in the qHash body
  3. every member has a hashDetects<T>("member", ...) case in the test

SCOPE. src/utils/app/collection/ — the leaf clusters that feed the settings
fingerprint. qHash overloads elsewhere (e.g. CoverFlowScaledKey, a render cache
key) are a different job with no diff baseline behind them, so they are out of
scope here rather than silently half-checked.

Exit status: 0 = clean, 1 = violations found, 2 = usage error.
"""

from __future__ import annotations

import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
CLUSTER_DIR = REPO / "src" / "utils" / "app" / "collection"
TEST_FILE = REPO / "tests" / "modules" / "settings" / "test_collectiondifffingerprint.cpp"

# `inline size_t qHash(const Foo &key, size_t seed = 0) {` — the marker that a
# struct participates in the fingerprint baseline at all.
QHASH_RE = re.compile(r"\bsize_t\s+qHash\s*\(\s*const\s+(\w+)\s*&\s*(\w+)")
STRUCT_RE = re.compile(r"^struct\s+(\w+)\s*\{", re.M)
# A data member: optional type tokens then `name;` or `name = init;`. Anchored
# at two-space indent because that is what clang-format produces for a struct
# body here, which keeps nested-scope declarations out.
# A data member declaration: type tokens, then `name;` or `name = init;`. Only
# ever applied to lines at struct scope (see members_of), which is what keeps
# locals inside an inline method body from being read as members — several of
# these structs declare helpers like launcherDisplayName() above operator==.
MEMBER_RE = re.compile(r"^\s*(?:[\w:<>,\s\*&]+?)\s(\w+)\s*(?:=[^;]*)?;\s*$")
# Declarations that are not data: statics/constants, type aliases, and anything
# with a parameter list.
NON_MEMBER_RE = re.compile(r"\b(?:static|constexpr|using|friend|typedef|return|struct|enum|class)\b")
DEFAULTED_EQ_RE = re.compile(r"bool\s+operator==\s*\([^)]*\)\s*const\s*=\s*default\s*;")


def strip_comments(text: str) -> str:
    """Blank out // and /* */ comments, preserving line structure.

    Doc comments on these structs quote member names constantly ("Ignored by
    Overlay mode", "NOT `treeIconSize`"), so a substring search over raw text
    would report a member as covered because its own documentation mentions it.
    """
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def brace_span(text: str, open_index: int) -> tuple[int, int]:
    """Return (start, end) of the balanced { } block whose { is at open_index."""
    depth = 0
    for i in range(open_index, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return open_index, i
    return open_index, len(text)


def members_of(struct_body: str) -> list[str]:
    """Data members declared directly in `struct_body`, in declaration order.

    Walks line by line tracking brace depth so only struct scope (depth 1) is
    considered. Depth matters: LauncherProfile declares launcherAt() and
    launcherDisplayName() ABOVE operator==, and their locals (`basename`,
    `slash`, `additionalIndex`) read exactly like member declarations to any
    check that only looks at indentation.
    """
    members: list[str] = []
    depth = 0
    for line in struct_body.splitlines():
        stripped = line.strip()
        # Count this line's braces AFTER deciding whether to inspect it, so a
        # member declaration and an opening `{` on the same line behave.
        at_struct_scope = depth == 1
        depth += line.count("{") - line.count("}")
        if not at_struct_scope or not stripped or "(" in stripped:
            continue
        if NON_MEMBER_RE.search(stripped):
            continue
        match = MEMBER_RE.match(stripped)
        if match:
            members.append(match.group(1))
    return members


def parse_clusters(path: pathlib.Path) -> list[dict]:
    """Every struct in `path` that has a qHash overload, with its members."""
    raw = path.read_text(encoding="utf-8")
    text = strip_comments(raw)
    hashed = {m.group(1): m.group(2) for m in QHASH_RE.finditer(text)}
    clusters = []
    for match in STRUCT_RE.finditer(text):
        name = match.group(1)
        if name not in hashed:
            continue
        start, end = brace_span(text, match.end() - 1)
        body = text[start : end + 1]
        # Members are everything declared before operator== — past that point
        # the "declarations" are the comparison and hash plumbing itself.
        eq_at = body.find("bool operator==")
        members = members_of(body[:eq_at] + "}" if eq_at >= 0 else body)

        eq_defaulted = bool(DEFAULTED_EQ_RE.search(body))
        eq_body = ""
        if not eq_defaulted and eq_at >= 0:
            eq_open = body.find("{", eq_at)
            if eq_open >= 0:
                s, e = brace_span(body, eq_open)
                eq_body = body[s : e + 1]

        hash_at = text.find(f"qHash(const {name} &")
        hash_body = ""
        if hash_at >= 0:
            hash_open = text.find("{", hash_at)
            if hash_open >= 0:
                s, e = brace_span(text, hash_open)
                hash_body = text[s : e + 1]

        clusters.append(
            {
                "name": name,
                "file": path,
                "members": members,
                "eq_defaulted": eq_defaulted,
                "eq_body": eq_body,
                "hash_body": hash_body,
                "hash_param": hashed[name],
            }
        )
    return clusters


def test_cases_by_cluster() -> tuple[dict[str, set[str]], dict[str, dict[str, str]]]:
    """Map cluster name -> members with a hashDetects case, and -> exemptions.

    Each test function opens with `using T = <Cluster>;`, so the cases that
    follow belong to that cluster until the next such line.

    A field can be legitimately untestable: DetailsPanePattern has a single
    enumerator, so there is no other value to mutate sidebarPattern to. Those
    carry an inline marker

        // fingerprint-exempt: <member> <reason>

    which keeps the reason at the place a reader meets the gap rather than in a
    table over here. A marker with no reason is itself a failure — the point of
    an exemption is the argument for it.
    """
    if not TEST_FILE.exists():
        return {}, {}
    cases: dict[str, set[str]] = {}
    exempt: dict[str, dict[str, str]] = {}
    current = None
    # Exemption markers live IN comments, so read the raw text and strip
    # comments only for the case scan.
    for raw_line in TEST_FILE.read_text(encoding="utf-8").splitlines():
        using = re.search(r"\busing\s+T\s*=\s*(\w+)\s*;", raw_line)
        if using:
            current = using.group(1)
            cases.setdefault(current, set())
            continue
        marker = re.search(r"//\s*fingerprint-exempt:\s*(\w+)\s*(.*)$", raw_line)
        if marker and current:
            exempt.setdefault(current, {})[marker.group(1)] = marker.group(2).strip()
            continue
        case = re.search(r'hashDetects<T>\(\s*"([^"]+)"', strip_comments(raw_line))
        if case and current:
            cases[current].add(case.group(1))
    return cases, exempt


def main() -> int:
    if not CLUSTER_DIR.is_dir():
        print(f"check-cluster-fingerprints: missing {CLUSTER_DIR}", file=sys.stderr)
        return 2

    clusters = []
    for header in sorted(CLUSTER_DIR.glob("*.h")):
        clusters.extend(parse_clusters(header))
    if not clusters:
        print("check-cluster-fingerprints: found no fingerprint clusters", file=sys.stderr)
        return 2

    cases, exemptions = test_cases_by_cluster()
    problems: list[str] = []
    checked_fields = 0
    exempted_fields = 0

    for cluster in clusters:
        name = cluster["name"]
        rel = cluster["file"].relative_to(REPO)
        if not cluster["members"]:
            problems.append(f"{rel}: {name} — parsed no members; the lint cannot vouch for it")
            continue
        if name not in cases:
            problems.append(
                f"{rel}: {name} is a fingerprint cluster with NO hashDetects cases — "
                f"add a <cluster>HashTracksEachField() case to {TEST_FILE.relative_to(REPO)}"
            )

        for member in cluster["members"]:
            checked_fields += 1
            if not cluster["eq_defaulted"] and not re.search(
                rf"\bother\.{re.escape(member)}\b", cluster["eq_body"]
            ):
                problems.append(f"{rel}: {name}::{member} is missing from operator==")
            if not re.search(
                rf"\b{re.escape(cluster['hash_param'])}\.{re.escape(member)}\b",
                cluster["hash_body"],
            ):
                problems.append(f"{rel}: {name}::{member} is missing from qHash")
            excused = exemptions.get(name, {})
            if member in excused:
                exempted_fields += 1
                if not excused[member]:
                    problems.append(
                        f"{rel}: {name}::{member} is fingerprint-exempt with no reason given"
                    )
            # A case label may QUALIFY the member it mutates —
            # "additionalLaunchers.size" and "additionalLaunchers[0].launcherPath"
            # both exercise additionalLaunchers, and say more about what they
            # change than the bare name would. Accept the member followed by a
            # non-identifier character, so `sidebarWidth` is still not covered
            # by a case for `sidebarWidthLocked`.
            elif name in cases and not any(
                label == member or re.match(rf"{re.escape(member)}\W", label)
                for label in cases[name]
            ):
                problems.append(
                    f"{rel}: {name}::{member} has no hashDetects case in "
                    f"{TEST_FILE.relative_to(REPO)}"
                )

    if problems:
        print("check-cluster-fingerprints: FAIL", file=sys.stderr)
        for problem in problems:
            print(f"  {problem}", file=sys.stderr)
        print(
            "\nA field in neither operator== nor qHash makes a real user edit fingerprint\n"
            "clean, so its hot-reload signal never fires (Kartend-g3fth, Kartend-5lzvl).",
            file=sys.stderr,
        )
        return 1

    defaulted = sum(1 for c in clusters if c["eq_defaulted"])
    print(
        f"check-cluster-fingerprints: OK — {len(clusters)} cluster(s), {checked_fields} field(s); "
        f"each is in operator== ({defaulted} cluster(s) use `= default`), in qHash, "
        f"and has a hashDetects case ({exempted_fields} exempted with a reason)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
