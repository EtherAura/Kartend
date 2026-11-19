#!/usr/bin/env python3
"""Print the libarchive version a staged AppImage library carries, and fail below the floor.

    python3 packaging/appimage/check-libarchive.py STAGED_LIBARCHIVE_ELF
    python3 packaging/appimage/check-libarchive.py --appdir STAGED_APPDIR

The caller names the one staged libarchive file to check, or a staged (or
already unpacked) AppDir. In an AppDir the checked file is the entry
build-appimage.sh requires, usr/lib/libarchive.so.13, or what it links to.
Its links are followed only inside the AppDir: an absolute link, a link that
climbs out of the AppDir, a missing or dangling entry, or one that ends at
something other than a regular file fails. Nothing else is searched for, no
system library is used in its place, and no AppImage is unpacked. The version
is read from the library's own
bytes: libarchive keeps its version string ("libarchive 3.8.11") as a
NUL-terminated constant, the one archive_version_string() returns. The file
name, its SONAME and the host's pkg-config say nothing about which release was
built, so they are not used. The library is only read, never loaded or run.

The observed version is always printed, also when it is too old. Exit status:

  0  the version is at least MINIMUM_VERSION;
  1  the version is older than MINIMUM_VERSION;
  2  the file cannot be read, is not an ELF file, or does not carry exactly one
     libarchive version string; or, with --appdir, the entry cannot be
     resolved to a regular file inside the AppDir.

Versions are compared number by number (3.8.10 is older than 3.8.11). A
version with a suffix such as "dev" is a pre-release and counts as older than
the same version without one.

Standard library only.
"""
import argparse
import os
import re
import stat
import sys

# The oldest libarchive a package may bundle.
MINIMUM_VERSION = (3, 8, 11)

# The AppDir entry build-appimage.sh requires (its REQUIRED_LIBRARIES, under usr/lib).
APPDIR_ENTRY = "usr/lib/libarchive.so.13"

# More links than this on the way to the library is treated as a loop.
MAX_LINKS = 40

# "libarchive " then MAJOR.MINOR.PATCH, an optional letter suffix, then the NUL
# that ends the C string constant.
VERSION_PATTERN = re.compile(rb"libarchive (\d{1,4})\.(\d{1,4})\.(\d{1,4})([a-z]{0,8})\x00")

ELF_MAGIC = b"\x7fELF"


def find_versions(data):
    """Return the distinct (numbers, suffix) libarchive versions found in data."""
    found = set()
    for match in VERSION_PATTERN.finditer(data):
        numbers = tuple(int(part) for part in match.group(1, 2, 3))
        found.add((numbers, match.group(4).decode("ascii")))
    return sorted(found)


def format_version(version):
    numbers, suffix = version
    return ".".join(str(n) for n in numbers) + suffix


def meets_minimum(version):
    numbers, suffix = version
    if numbers != MINIMUM_VERSION:
        return numbers > MINIMUM_VERSION
    return suffix == ""


class AppDirError(Exception):
    """The AppDir entry does not resolve to a regular file inside the AppDir."""


def resolve_in_appdir(root, entry):
    """Return the path, relative to root, of the regular file entry leads to.

    Each component is looked at with lstat, and a link is followed by hand so
    that its target is read against root, never against the host. An absolute
    link target, or a ".." above root, is refused rather than followed.
    """
    pending = entry.split("/")
    resolved = []
    links = 0
    while pending:
        part = pending.pop(0)
        if part in ("", "."):
            continue
        if part == "..":
            if not resolved:
                raise AppDirError("%s leads outside the AppDir" % entry)
            resolved.pop()
            continue
        relative = "/".join(resolved + [part])
        try:
            info = os.lstat(os.path.join(root, relative))
        except FileNotFoundError:
            if links:
                raise AppDirError("%s is a dangling link (no %s)" % (entry, relative))
            raise AppDirError("the AppDir has no %s" % entry)
        except OSError as error:
            raise AppDirError("cannot look at %s: %s" % (relative, error.strerror))
        if stat.S_ISLNK(info.st_mode):
            links += 1
            if links > MAX_LINKS:
                raise AppDirError("%s: too many levels of links" % entry)
            target = os.readlink(os.path.join(root, relative))
            if os.path.isabs(target):
                raise AppDirError(
                    "%s links outside the AppDir (absolute link '%s' at %s)" % (entry, target, relative)
                )
            pending = target.split("/") + pending
            continue
        if pending and not stat.S_ISDIR(info.st_mode):
            raise AppDirError("%s is not a directory, on the way to %s" % (relative, entry))
        resolved.append(part)
    if not resolved:
        raise AppDirError("%s leads to the AppDir itself" % entry)
    relative = "/".join(resolved)
    if not stat.S_ISREG(os.lstat(os.path.join(root, relative)).st_mode):
        raise AppDirError("%s is not a regular file (it leads to %s)" % (entry, relative))
    return relative


def read_regular_file(path):
    """Read path without following a link at its last component."""
    fd = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    with os.fdopen(fd, "rb") as handle:
        if not stat.S_ISREG(os.fstat(handle.fileno()).st_mode):
            raise OSError(0, "not a regular file")
        return handle.read()


def check_data(name, label, data):
    """Print the version data carries and return the exit status."""
    if not data.startswith(ELF_MAGIC):
        print("%s: %s: not an ELF file" % (name, label), file=sys.stderr)
        return 2

    versions = find_versions(data)
    if not versions:
        print("%s: %s: no libarchive version string found" % (name, label), file=sys.stderr)
        return 2
    if len(versions) > 1:
        listed = ", ".join(format_version(v) for v in versions)
        print(
            "%s: %s: more than one libarchive version string found (%s)" % (name, label, listed),
            file=sys.stderr,
        )
        return 2

    version = format_version(versions[0])
    minimum = ".".join(str(n) for n in MINIMUM_VERSION)
    if meets_minimum(versions[0]):
        print("%s: %s: libarchive %s (ok, at least %s)" % (name, label, version, minimum))
        return 0
    print("%s: %s: libarchive %s (too old, need at least %s)" % (name, label, version, minimum))
    return 1


def check_appdir(name, appdir):
    root = os.path.realpath(appdir)
    if not os.path.isdir(root):
        print("%s: %s: not a directory" % (name, appdir), file=sys.stderr)
        return 2
    try:
        relative = resolve_in_appdir(root, APPDIR_ENTRY)
    except AppDirError as error:
        print("%s: %s: %s" % (name, appdir, error), file=sys.stderr)
        return 2
    label = os.path.join(appdir, APPDIR_ENTRY)
    if relative != APPDIR_ENTRY:
        label += " -> " + relative
    try:
        data = read_regular_file(os.path.join(root, relative))
    except OSError as error:
        print("%s: %s: cannot read: %s" % (name, label, error.strerror), file=sys.stderr)
        return 2
    return check_data(name, label, data)


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Print the libarchive version a staged library carries; "
        "fail if it is older than %s." % ".".join(str(n) for n in MINIMUM_VERSION)
    )
    target = parser.add_mutually_exclusive_group(required=True)
    target.add_argument("library", nargs="?", help="the staged libarchive ELF file to check")
    target.add_argument(
        "--appdir",
        metavar="STAGED_APPDIR",
        help="a staged or unpacked AppDir; checks its %s" % APPDIR_ENTRY,
    )
    args = parser.parse_args(argv)

    name = "check-libarchive"
    if args.appdir is not None:
        return check_appdir(name, args.appdir)

    try:
        with open(args.library, "rb") as handle:
            data = handle.read()
    except OSError as error:
        print("%s: %s: cannot read: %s" % (name, args.library, error.strerror), file=sys.stderr)
        return 2
    return check_data(name, args.library, data)


if __name__ == "__main__":
    sys.exit(main())
