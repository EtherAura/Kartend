#!/usr/bin/env python3
"""Make linuxdeploy's AppRun start usr/bin/kartend itself, not AppRun.wrapped.

    python3 packaging/appimage/apprun-handoff.py APPDIR

With apprun-hooks present, linuxdeploy writes AppRun as a small bash script
that sources each hook and ends in

    exec "$this_dir"/AppRun.wrapped "$@"

where AppRun.wrapped is a link to usr/bin/kartend. The kernel names a process
after the file that was executed, so Kartend ran as "AppRun.wrapped": in its
usage line, in process lists and as its X11 window class. This script replaces
that one line with

    exec -a kartend "$this_dir/usr/bin/kartend" "$@"

and leaves everything above it (set -e, this_dir, the hooks) and the argument
forwarding as they were. The path is the AppDir's own file, never looked up
through PATH. `-a kartend` only sets argument zero: Qt's usage line in --help
names argument zero, which would otherwise be the path inside the AppImage's
temporary mount directory. Qt finds its own files through /proc/self/exe, not
argument zero.

It changes nothing and exits 1 when AppRun is not the wrapper it expects: a
missing or non-regular file, no hook being sourced, the exec line absent,
present more than once or not the last command, any other mention of
AppRun.wrapped, or AppRun.wrapped not leading to usr/bin/kartend. A linuxdeploy
update that writes AppRun differently then stops the package build instead of
shipping the old launch. The file keeps its permissions.

Standard library only.
"""
import argparse
import contextlib
import os
import pathlib
import stat
import sys
import tempfile

EXECUTABLE = "usr/bin/kartend"
WRAPPED = "AppRun.wrapped"
OLD_EXEC = 'exec "$this_dir"/AppRun.wrapped "$@"'
NEW_EXEC = 'exec -a kartend "$this_dir/usr/bin/kartend" "$@"'
HOOK = "kartend-own-qt.sh"


class Unexpected(Exception):
    pass


def rewrite(text: str) -> str:
    lines = text.split("\n")
    if not lines[0].startswith("#!") or "bash" not in lines[0]:
        raise Unexpected("AppRun is not a bash script")
    matches = [i for i, line in enumerate(lines) if line == OLD_EXEC]
    if len(matches) != 1:
        raise Unexpected(f"AppRun has {len(matches)} lines '{OLD_EXEC}', expected exactly one")
    at = matches[0]
    if any(line.strip() for line in lines[at + 1 :]):
        raise Unexpected("the exec line is not the last command in AppRun")
    if any(WRAPPED in line for i, line in enumerate(lines) if i != at):
        raise Unexpected(f"AppRun mentions {WRAPPED} outside the exec line")
    if not any(
        line.startswith("source ") and "apprun-hooks/" in line and HOOK in line
        for line in lines[:at]
    ):
        raise Unexpected(f"AppRun does not source apprun-hooks/{HOOK} before the exec line")
    lines[at] = NEW_EXEC
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("appdir", type=pathlib.Path)
    args = parser.parse_args()
    apprun = args.appdir / "AppRun"
    executable = args.appdir / EXECUTABLE
    try:
        info = os.lstat(apprun)
        if not stat.S_ISREG(info.st_mode):
            raise Unexpected("AppRun is not a regular file (linuxdeploy writes a script there)")
        exe_info = os.lstat(executable)
        if not stat.S_ISREG(exe_info.st_mode) or not os.access(executable, os.X_OK):
            raise Unexpected(f"{EXECUTABLE} is not an executable regular file")
        wrapped = args.appdir / WRAPPED
        if not os.path.exists(wrapped) or not os.path.samefile(wrapped, executable):
            raise Unexpected(f"{WRAPPED} does not lead to {EXECUTABLE}")
        text = apprun.read_bytes().decode("utf-8")
        new_text = rewrite(text)
    except (OSError, UnicodeDecodeError, Unexpected) as error:
        print(f"apprun-handoff: {apprun}: {error}", file=sys.stderr)
        return 1

    fd, tmp = tempfile.mkstemp(prefix=".AppRun.", dir=args.appdir)
    try:
        with os.fdopen(fd, "wb") as out:
            out.write(new_text.encode("utf-8"))
        os.chmod(tmp, stat.S_IMODE(info.st_mode))
        os.replace(tmp, apprun)
    except OSError as error:
        with contextlib.suppress(OSError):
            os.unlink(tmp)
        print(f"apprun-handoff: {apprun}: {error}", file=sys.stderr)
        return 1
    print(f"apprun-handoff: AppRun now starts {EXECUTABLE}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
