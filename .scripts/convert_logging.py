#!/usr/bin/env python3
"""
Convert Kartend's diagnostic logging from raw qWarning() + per-file
diagLog/searchDiagEnabled macros to qCDebug/qCWarning under three central
QLoggingCategory values: lcPerfTrace, lcSearchDiag, lcScanFlow.

Operations per file:
  1. Replace `qWarning() << "[PerfTrace] ...` -> `qCDebug(lcPerfTrace) << "...`
  2. Replace `qWarning() << "[SearchDiag][Module] ...` -> `qCDebug(lcSearchDiag) << "[Module] ...`
  3. Replace `qWarning() << "[ScanFlow] ...` -> `qCDebug(lcScanFlow) << "...`
  4. Drop `if (qEnvironmentVariableIsSet("KARTEND_PERF_TRACE")) { qCDebug(...) ... }`
     wrappers (the category gating handles it). Conservative: only drop when
     the entire if-block is a single qCDebug call. Otherwise leave for manual review.
  5. Drop per-file static `searchDiagEnabled()` helpers and `diagLog(...)` macros;
     replace `diagLog(msg)` call sites with `qCDebug(lcSearchDiag) << msg`.
  6. Inject `#include "loggingcategories.h"` if any conversion happens.

Run from repo root.
"""
from __future__ import annotations
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "src"

FILES = [
    "modules/artwork/artworksilentloading.cpp",
    "modules/artwork/artworkmanagerpaths.cpp",
    "modules/artwork/artworkmanager.cpp",
    "modules/artwork/artworkmanagerprecache.cpp",
    "modules/database/databasemanager.cpp",
    "modules/database/databasemanagerworker.cpp",
    "modules/navigation/navigationmanagerfilter.cpp",
    "modules/navigation/navigationmanagertitle.cpp",
    "modules/navigation/navigationmanager.cpp",
    "modules/navigation/navigationmanagerload.cpp",
    "modules/navigation/navigationmanagerappearance.cpp",
    "modules/navigation/navigationmanageritemsloaded.cpp",
    "modules/navigation/navigationmanagerroute.cpp",
    "modules/query/querymanagercache.cpp",
    "modules/query/querymanagerfetchrange.cpp",
    "modules/query/querymanagerfetch.cpp",
    "modules/scroll/itemwidgetfactory.cpp",
    "modules/scroll/scrollmanager.cpp",
    "modules/scroll/scrollmanagerdata.cpp",
    "modules/scroll/virtualcontainermanager.cpp",
    "modules/scroll/scrollmanagervirtual.cpp",
    "modules/scroll/scrollmanagersetup.cpp",
    "modules/scroll/itemwidgetfactorycreate.cpp",
    "modules/search/searchmanager.cpp",
    "modules/search/searchmanagerquery.cpp",
    "utils/artworkutils.cpp",
]


def convert_text(src: str) -> tuple[str, dict]:
    stats = {"perftrace": 0, "searchdiag": 0, "scanflow": 0,
             "diaglog_macro_removed": 0, "envcheck_inlined": 0}
    out = src

    # 1) Strip per-file searchDiagEnabled() helper + diagLog macro blocks.
    # Pattern: namespace { inline bool searchDiagEnabled[F]() { ... } } #define diagLog(msg) ...
    # The qWarning prefix may be "[SearchDiag][Module]" or
    # "[SearchDiag][Module] " (trailing space inside literal). Sub-tag
    # content is bracket-free.
    diag_qwarning_re = (
        r'\s*qWarning\(\)\s*<<\s*"\[SearchDiag\]\[[^\]"]+\]\s*"\s*'
        r'<<\s*msg;\s*\\\s*\n'
    )
    helper_macro_patterns = [
        # Anonymous namespace form
        re.compile(
            r"namespace\s*\{\s*\n"
            r"(?://[^\n]*\n)*"
            r"inline bool searchDiagEnabled\w*\(\)\s*\{\s*\n"
            r"\s*return qEnvironmentVariableIsSet\(\"KARTEND_SEARCH_DIAG\"\);\s*\n"
            r"\s*\}\s*\n"
            r"\}\s*//\s*namespace\s*\n"
            r"\s*\n"
            r"#define diagLog\(msg\)\s*\\\s*\n"
            r"\s*do\s*\{\s*\\\s*\n"
            r"\s*if\s*\((?:::)?searchDiagEnabled\w*\(\)\)\s*\{\s*\\\s*\n"
            + diag_qwarning_re
            + r"\s*\}\s*\\\s*\n"
            r"\s*\}\s*while\s*\(0\)\s*\n",
            re.M,
        ),
        # static inline form (no namespace)
        re.compile(
            r"(?://[^\n]*\n)*"
            r"static inline bool searchDiagEnabled\w*\(\)\s*\{\s*\n"
            r"\s*return qEnvironmentVariableIsSet\(\"KARTEND_SEARCH_DIAG\"\);\s*\n"
            r"\s*\}\s*\n"
            r"\s*\n"
            r"#define diagLog\(msg\)\s*\\\s*\n"
            r"\s*do\s*\{\s*\\\s*\n"
            r"\s*if\s*\(searchDiagEnabled\w*\(\)\)\s*\{\s*\\\s*\n"
            + diag_qwarning_re
            + r"\s*\}\s*\\\s*\n"
            r"\s*\}\s*while\s*\(0\)\s*\n",
            re.M,
        ),
    ]
    for pat in helper_macro_patterns:
        new_out, n = pat.subn("", out)
        if n:
            stats["diaglog_macro_removed"] += n
            out = new_out

    # Replace diagLog(...) call sites with qCDebug(lcSearchDiag) << ...;
    # Use a paren-balancing scanner to handle multi-line QString(...).arg(...)
    # arguments correctly (a single non-greedy regex would cross statements).
    def replace_diaglog_calls(s: str) -> tuple[str, int]:
        result_parts: list[str] = []
        i = 0
        n = 0
        while True:
            j = s.find("diagLog(", i)
            if j < 0:
                result_parts.append(s[i:])
                break
            # Skip macro definition itself: "#define diagLog("
            line_start = s.rfind("\n", 0, j) + 1
            if s[line_start:j].lstrip().startswith("#define"):
                result_parts.append(s[i:j + len("diagLog(")])
                i = j + len("diagLog(")
                continue
            # Scan from after "diagLog(" balancing parens until depth 0.
            start_inner = j + len("diagLog(")
            depth = 1
            k = start_inner
            in_str = False
            esc = False
            while k < len(s) and depth > 0:
                c = s[k]
                if in_str:
                    if esc:
                        esc = False
                    elif c == "\\":
                        esc = True
                    elif c == '"':
                        in_str = False
                else:
                    if c == '"':
                        in_str = True
                    elif c == "(":
                        depth += 1
                    elif c == ")":
                        depth -= 1
                k += 1
            if depth != 0:
                # Malformed; bail out and emit unchanged.
                result_parts.append(s[i:k])
                i = k
                continue
            inner = s[start_inner:k - 1]
            # Expect a trailing semicolon after closing paren; consume it.
            after = k
            while after < len(s) and s[after] in " \t":
                after += 1
            if after < len(s) and s[after] == ";":
                after += 1
            else:
                # No semicolon; leave alone to avoid breaking syntax.
                result_parts.append(s[i:k])
                i = k
                continue
            result_parts.append(s[i:j])
            result_parts.append(f"qCDebug(lcSearchDiag) << {inner};")
            i = after
            n += 1
        return "".join(result_parts), n

    out, n = replace_diaglog_calls(out)
    stats["searchdiag"] += n

    # 2) Direct qWarning() << "[Tag] ..." conversions.
    # PerfTrace
    out, n = re.subn(
        r'qWarning\(\)\s*<<\s*"\[PerfTrace\]\s*',
        'qCDebug(lcPerfTrace) << "',
        out,
    )
    stats["perftrace"] += n

    # SearchDiag (with module sub-tag) -> keep module sub-tag.
    # Sub-tag content may not contain `]` or `"`. Trailing space inside the
    # quoted prefix is allowed (e.g., "[SearchDiag][Module] ").
    out, n = re.subn(
        r'qWarning\(\)\s*<<\s*"\[SearchDiag\]\[([^\]"]+)\]\s*',
        r'qCDebug(lcSearchDiag) << "[\1] ',
        out,
    )
    stats["searchdiag"] += n
    # SearchDiag (no sub-tag)
    out, n = re.subn(
        r'qWarning\(\)\s*<<\s*"\[SearchDiag\]\s*',
        'qCDebug(lcSearchDiag) << "',
        out,
    )
    stats["searchdiag"] += n

    # Other diagnostic tags fold into searchdiag (RangeDiag, ArtworkDiag,
    # SelectionRestore). They're all developer-focused traces.
    for tag in ("RangeDiag", "ArtworkDiag", "SelectionRestore"):
        out, n = re.subn(
            r'qWarning\(\)\s*<<\s*"\[' + tag + r'\]\s*',
            f'qCDebug(lcSearchDiag) << "[{tag}] ',
            out,
        )
        stats["searchdiag"] += n

    # ScanFlow (these stay visible; use qCWarning to match prior severity)
    out, n = re.subn(
        r'qWarning\(\)\s*<<\s*"\[ScanFlow\]\s*',
        'qCWarning(lcScanFlow) << "',
        out,
    )
    stats["scanflow"] += n

    # 3) Inline env-var-gated single-line wrappers around perftrace/searchdiag
    # logs: if (qEnvironmentVariableIsSet("KARTEND_PERF_TRACE")) { qCDebug(...) << ...; }
    # Convert single-statement bodies (multi-line streaming counts as single
    # statement when the only `;` is at the very end).
    def strip_envcheck(envvar: str, src: str) -> tuple[str, int]:
        pat = re.compile(
            r"^([ \t]*)if\s*\(qEnvironmentVariableIsSet\(\""
            + re.escape(envvar)
            + r"\"\)\)\s*\{\s*\n"
            r"((?:[ \t]*qC(?:Debug|Warning)\([^)]+\)[^;]*?;\s*\n)+)"
            r"[ \t]*\}\s*\n",
            re.M,
        )

        def repl(m: re.Match) -> str:
            return m.group(2)

        return pat.subn(repl, src)

    out, n = strip_envcheck("KARTEND_PERF_TRACE", out)
    stats["envcheck_inlined"] += n
    out, n = strip_envcheck("KARTEND_SEARCH_DIAG", out)
    stats["envcheck_inlined"] += n

    # 4) Inject include if any change happened.
    changed = any(v for v in stats.values())
    if changed and "loggingcategories.h" not in out:
        # Insert after the first existing #include line, preferring after
        # the file's primary header (first quoted include).
        m = re.search(r'^#include\s+"([^"]+)"\s*$', out, flags=re.M)
        if m:
            insert_at = m.end()
            out = (
                out[:insert_at]
                + '\n#include "loggingcategories.h"'
                + out[insert_at:]
            )
        else:
            # No quoted include? Fall back to after first <...> include.
            m = re.search(r'^#include\s+<[^>]+>\s*$', out, flags=re.M)
            if m:
                insert_at = m.end()
                out = (
                    out[:insert_at]
                    + '\n#include "loggingcategories.h"'
                    + out[insert_at:]
                )

    return out, stats


def main() -> int:
    grand: dict[str, int] = {}
    for rel in FILES:
        p = ROOT / rel
        if not p.exists():
            print(f"SKIP missing: {rel}", file=sys.stderr)
            continue
        src = p.read_text()
        new, stats = convert_text(src)
        if new != src:
            p.write_text(new)
            print(f"{rel}: " + ", ".join(
                f"{k}={v}" for k, v in stats.items() if v))
            for k, v in stats.items():
                grand[k] = grand.get(k, 0) + v
    print("\nTOTAL:", grand)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
