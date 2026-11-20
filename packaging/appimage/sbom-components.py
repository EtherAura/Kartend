#!/usr/bin/env python3
"""Write the part of the AppImage's SBOM that no file on disk can tell a scanner.

build-appimage.sh calls this as its last step. The release's SBOM is made by
syft (anchore/sbom-action) scanning the directory build-appimage.sh lays out,
`sbom-root/`. syft finds the bundled system libraries through the package
records copied there, and recognises Qt's base libraries and FFmpeg from the
files themselves. What it cannot see is written here, as a small CycloneDX
file that syft merges in (packaging/appimage/syft.yaml switches that on):

  - the AppImage file itself, with its SHA-256;
  - Kartend, the application inside;
  - QtKeychain, which is linked into the kartend executable;
  - libarchive, built from a pinned source archive and bundled as a shared library;
  - the Qt modules beyond qtbase, each only if one of its files is bundled;
  - the ICU that the Qt build ships with it;
  - the AppImage runtime, the small program at the front of the file.

Standard library only.
"""
import argparse
import hashlib
import json
import os
import sys
import uuid

# Qt module -> a file that is in the bundle exactly when the module is.
QT_MODULE_MARKERS = [
    ("qtmultimedia", "usr/lib/libQt6Multimedia.so.6"),
    ("qtsvg", "usr/lib/libQt6Svg.so.6"),
    ("qtwayland", "usr/lib/libQt6WaylandClient.so.6"),
    ("qtdeclarative", "usr/lib/libQt6Qml.so.6"),
    ("qtimageformats", "usr/plugins/imageformats/libqwebp.so"),
]


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def qt_icu_version(qt_lib_dir, appdir):
    """The version of the ICU that Qt ships, when it is in the bundle.

    Qt's lib directory holds libicuuc.so.73.2 behind the name libicuuc.so.73,
    and the bundle has the latter. The base's own ICU (another version,
    pulled in by libxml2) can be bundled next to it; that one is a package of
    the base and is listed from the package records, not here.
    """
    best = None
    for name in sorted(os.listdir(qt_lib_dir)):
        if name.startswith("libicuuc.so."):
            version = name[len("libicuuc.so."):]
            if best is None or version.count(".") > best.count("."):
                best = version
    if best is None:
        return None
    major = best.split(".")[0]
    if not os.path.exists(os.path.join(appdir, "usr", "lib", f"libicuuc.so.{major}")):
        return None
    return best


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--appimage", required=True, help="the finished AppImage file")
    parser.add_argument("--appdir", required=True, help="the AppDir it was made from")
    parser.add_argument("--version", required=True, help="Kartend's version")
    parser.add_argument("--qt-version", required=True)
    parser.add_argument("--qt-lib-dir", required=True, help="lib directory of the Qt that was bundled")
    parser.add_argument("--qtkeychain-tag", required=True)
    parser.add_argument("--qtkeychain-commit", required=True)
    parser.add_argument("--libarchive-version", required=True)
    parser.add_argument("--libarchive-url", required=True)
    parser.add_argument("--libarchive-source-sha256", required=True)
    parser.add_argument("--runtime-tag", required=True, help="type2-runtime release tag")
    parser.add_argument("--runtime-sha256", required=True)
    parser.add_argument("--output", required=True, help="CycloneDX JSON file to write")
    args = parser.parse_args()

    file_name = os.path.basename(args.appimage)
    file_sha256 = sha256_of(args.appimage)
    archive_sha256 = sha256_of(os.path.join(args.appdir, "usr", "lib", "libarchive.so.13"))

    components = [
        # syft keeps the purl of a component it reads from an SBOM file but
        # drops the hash list, so the SHA-256 is in the purl as well.
        {
            "type": "application",
            "name": file_name,
            "version": args.version,
            "purl": f"pkg:generic/{file_name}@{args.version}?checksum=sha256:{file_sha256}",
            "hashes": [{"alg": "SHA-256", "content": file_sha256}],
        },
        {
            "type": "application",
            "name": "kartend",
            "version": args.version,
            "purl": f"pkg:github/EtherAura/Kartend@v{args.version}",
        },
        {
            "type": "library",
            "name": "qtkeychain",
            "version": args.qtkeychain_tag,
            "purl": f"pkg:github/frankosterfeld/qtkeychain@{args.qtkeychain_commit}",
        },
        {
            "type": "library",
            "name": "libarchive",
            "version": args.libarchive_version,
            "purl": f"pkg:github/libarchive/libarchive@v{args.libarchive_version}",
            "hashes": [{"alg": "SHA-256", "content": archive_sha256}],
            "externalReferences": [
                {
                    "type": "distribution",
                    "url": args.libarchive_url,
                    "hashes": [{"alg": "SHA-256", "content": args.libarchive_source_sha256}],
                }
            ],
        },
        {
            "type": "application",
            "name": "type2-runtime",
            "version": args.runtime_tag,
            "purl": (
                f"pkg:github/AppImage/type2-runtime@{args.runtime_tag}"
                f"?checksum=sha256:{args.runtime_sha256}"
            ),
        },
    ]
    for module, marker in QT_MODULE_MARKERS:
        if os.path.exists(os.path.join(args.appdir, marker)):
            components.append(
                {
                    "type": "library",
                    "name": module,
                    "version": args.qt_version,
                    "purl": f"pkg:generic/{module}@{args.qt_version}",
                }
            )
    icu = qt_icu_version(args.qt_lib_dir, args.appdir)
    if icu:
        components.append(
            {"type": "library", "name": "icu", "version": icu, "purl": f"pkg:generic/icu@{icu}"}
        )

    document = {
        "bomFormat": "CycloneDX",
        "specVersion": "1.5",
        "version": 1,
        "serialNumber": "urn:uuid:" + str(uuid.uuid4()),
        "components": components,
    }
    with open(args.output, "w", encoding="utf-8") as out:
        json.dump(document, out, indent=2)
        out.write("\n")
    print(f"sbom-components: {len(components)} components -> {args.output}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
