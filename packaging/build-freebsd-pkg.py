#!/usr/bin/env python3
"""Create a native FreeBSD package from an existing Kartend build."""

import argparse
import json
from pathlib import Path
import re
from shutil import copyfile
import subprocess
import sys
from tempfile import TemporaryDirectory


PREFIX = Path("/usr/local")


def output(*command: str) -> str:
    return subprocess.check_output(command, text=True).strip()


def dependencies(executable: Path) -> dict[str, dict[str, str]]:
    # QSQLITE and SVG support are loaded by Qt at runtime, outside DT_NEEDED.
    packages = {"qt6-base_sqldriver-sqlite", "qt6-svg"}
    needed = re.findall(r"\(NEEDED\).*?\[([^]]+)\]", output("readelf", "-d", str(executable)))
    library_paths = dict(line.split("\t", 1) for line in
                         output("ldd", "-f", r"%o\t%p\n", str(executable)).splitlines())
    for name in needed:
        path = Path(library_paths.get(name, ""))
        if not path.is_absolute() or not path.is_file():
            raise RuntimeError(f"Cannot resolve Kartend runtime library {name}")
        if not path.is_relative_to("/lib") and not path.is_relative_to("/usr/lib"):
            packages.add(output("pkg", "which", "-q", str(path)))

    result = {}
    for package in sorted(packages):
        name, origin, version = output("pkg", "query", r"%n\t%o\t%v", package).split("\t")
        result[name] = {"origin": origin, "version": version}
    return result


def package(build_dir: Path, output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    with TemporaryDirectory(prefix="kartend-pkg-") as temp:
        root = Path(temp) / "stage"
        metadata = Path(temp) / "metadata"
        metadata.mkdir()
        subprocess.run(["env", f"DESTDIR={root}", "cmake", "--install", str(build_dir),
                        "--prefix", str(PREFIX)], check=True)
        installed = root / PREFIX.relative_to("/")
        executable = installed / "bin/kartend"
        license_dir = installed / "share/licenses/kartend"
        license_dir.mkdir(parents=True, exist_ok=True)
        copyfile(Path(__file__).resolve().parents[1] / "LICENSE", license_dir / "LICENSE")
        version = output(str(executable), "--version").removeprefix("kartend ")
        manifest = {
            "name": "kartend",
            "origin": "multimedia/kartend",
            "version": version,
            "comment": "Qt6 frontend for organizing and launching multimedia collections",
            "desc": "Organize and launch multimedia collections from a Qt6 desktop application.",
            "maintainer": "etheraura@protonmail.com",
            "www": "https://github.com/EtherAura/Kartend",
            "prefix": str(PREFIX),
            "abi": output("pkg", "config", "ABI"),
            "categories": ["multimedia"],
            "licenses": ["GPLv3"],
            "licenselogic": "single",
            "deps": dependencies(executable),
        }
        (metadata / "+MANIFEST").write_text(json.dumps(manifest, indent=2) + "\n")
        plist = metadata / "plist"
        files = sorted(path.relative_to(installed).as_posix() for path in installed.rglob("*")
                       if path.is_file() or path.is_symlink())
        plist.write_text("\n".join(files) + "\n")
        subprocess.run(["pkg", "create", "-r", str(root), "-m", str(metadata), "-p", str(plist),
                        "-o", str(output_dir)], check=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path, help="CMake build directory containing Kartend")
    parser.add_argument("--output-dir", type=Path, default=Path("dist"))
    args = parser.parse_args()
    if not sys.platform.startswith("freebsd"):
        parser.error("run this command on the FreeBSD build host")
    package(args.build_dir, args.output_dir)
