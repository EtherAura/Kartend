#!/usr/bin/env python3
"""Print the libarchive version a staged AppImage library carries, and fail below the floor.

    python3 packaging/appimage/check-libarchive.py STAGED_LIBARCHIVE_ELF

The caller names the one staged libarchive file to check; nothing is searched
for, and no AppImage is unpacked. The version is read from the library's own
bytes: libarchive keeps its version string ("libarchive 3.8.11") as a
NUL-terminated constant, the one archive_version_string() returns. The file
name, its SONAME and the host's pkg-config say nothing about which release was
built, so they are not used. The library is only read, never loaded or run.

The observed version is always printed, also when it is too old. Exit status:

  0  the version is at least MINIMUM_VERSION;
  1  the version is older than MINIMUM_VERSION;
  2  the file cannot be read, is not an ELF file, or does not carry exactly one
     libarchive version string.

Versions are compared number by number (3.8.10 is older than 3.8.11). A
version with a suffix such as "dev" is a pre-release and counts as older than
the same version without one.

Standard library only.
"""
import argparse
import re
import sys

# The oldest libarchive a package may bundle.
MINIMUM_VERSION = (3, 8, 11)

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


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Print the libarchive version a staged library carries; "
        "fail if it is older than %s." % ".".join(str(n) for n in MINIMUM_VERSION)
    )
    parser.add_argument("library", help="the staged libarchive ELF file to check")
    args = parser.parse_args(argv)

    name = "check-libarchive"
    try:
        with open(args.library, "rb") as handle:
            data = handle.read()
    except OSError as error:
        print("%s: %s: cannot read: %s" % (name, args.library, error.strerror), file=sys.stderr)
        return 2

    if not data.startswith(ELF_MAGIC):
        print("%s: %s: not an ELF file" % (name, args.library), file=sys.stderr)
        return 2

    versions = find_versions(data)
    if not versions:
        print("%s: %s: no libarchive version string found" % (name, args.library), file=sys.stderr)
        return 2
    if len(versions) > 1:
        listed = ", ".join(format_version(v) for v in versions)
        print(
            "%s: %s: more than one libarchive version string found (%s)"
            % (name, args.library, listed),
            file=sys.stderr,
        )
        return 2

    version = format_version(versions[0])
    minimum = ".".join(str(n) for n in MINIMUM_VERSION)
    if meets_minimum(versions[0]):
        print("%s: %s: libarchive %s (ok, at least %s)" % (name, args.library, version, minimum))
        return 0
    print("%s: %s: libarchive %s (too old, need at least %s)" % (name, args.library, version, minimum))
    return 1


if __name__ == "__main__":
    sys.exit(main())
