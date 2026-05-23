# Reproducing CI locally

Kartend's CI runs on `ubuntu-24.04` GitHub-hosted runners, whose
`qt6-base-dev` package is **Qt 6.4.2** — the project's
`KARTEND_MIN_QT_VERSION`. Developer machines almost always have a
newer Qt; that newer Qt silently accepts Qt 6.5+ APIs the CI build
then fails on. The same drift bites `clang-format` version, libc
version, and the various sanitizer suppressions.

The fix is to build inside a Docker image that mirrors the CI
runner. The image, the wrapper script, and the local-CI conveniences
all live under [.scripts/](../.scripts/).

## The `kartend-ci` image

[.scripts/Dockerfile.ci](../.scripts/Dockerfile.ci) builds an
`ubuntu-24.04`-based image with every package the CI workflow
installs:

| Package set | Why |
|-------------|-----|
| `clang`, `g++`, `cmake`, `ninja-build`, `lld`, `ccache` | Build toolchain |
| `qt6-base-dev`, `qt6-multimedia-dev`, `qt6-tools-dev`, `libqt6sql6-sqlite` | Qt 6.4.2 (Ubuntu 24.04's pinned version) |
| `libzstd-dev`, `qtkeychain-qt6-dev` | Optional dependencies that flip features on |
| `clang-tidy`, `clang-format-19`, `cppcheck`, `jq` | Maintenance / static-analysis tooling |
| `libtsan2`, `libasan8`, `libubsan1` | Sanitizer runtime libs |
| `pulseaudio` | QtMultimedia tests SIGILL under TSan without a running PulseAudio session |

The Dockerfile also symlinks `clang-format-19` → `clang-format` so
the pre-commit hook and `--format-check` runner pick up the pinned
v19 binary even on a host without it.

### Build it

```bash
docker build -f .scripts/Dockerfile.ci -t kartend-ci .
```

Re-build whenever the Dockerfile changes (or once a quarter to pick
up Ubuntu security updates). The local-CI subcommands check that the
image exists and prompt to build it on first use.

## `.scripts/ci-local.sh`

[.scripts/ci-local.sh](../.scripts/ci-local.sh) is the wrapper.
Three Docker-backed subcommands plus `docker:all`:

| Command | What it does | Approx duration |
|---------|--------------|-----------------|
| `docker:tidy` | `--maintenance --format-check`-equivalent (clang-tidy, format-check, the works) | ~10 min |
| `docker:build` | Release+clang build + `ctest` | ~12 min |
| `docker:tsan` | Debug+TSan build + `ctest` under TSan | ~10 min |
| `docker:all` | `docker:tidy` → `docker:build` → `docker:tsan` in sequence | ~30 min |

Run them from the repo root:

```bash
.scripts/ci-local.sh docker:tidy
.scripts/ci-local.sh docker:build
.scripts/ci-local.sh docker:tsan
.scripts/ci-local.sh docker:all
```

The script:

- Mounts the repo at `/src` (read-only for `docker:tidy`, RW for
  the build subcommands so they can write `build/` artifacts).
- Sets `QT_QPA_PLATFORM=offscreen` so widget tests run headless.
- Wires up `TSAN_OPTIONS` with the
  [`.tsan_suppressions.txt`](../.tsan_suppressions.txt) file
  inside the container.
- Inherits a host `ccache` directory (when present) so re-builds
  are fast.

The non-Docker subcommands of `ci-local.sh` (`build`, `tsan`,
`maintenance`) run **on the host**, not in the image. Use those when
you trust your host environment matches CI; use the `docker:*`
variants when you're about to push something that needs to clear CI.

## Gotchas — host vs CI drift

The whole reason the kartend-ci image exists is to surface drift
between developer machines and CI. The known gotchas:

### Qt 6.4 vs 6.5+

Several Qt6 APIs landed in 6.5 (e.g. `QHash::insert` returning an
iterator was clarified, several QML / QPainter helpers, and any
`Qt::AnyKey`-related changes). The local Arch / Fedora installs ship
6.6+; the CI image is pinned to 6.4.2. If your code compiles on the
host but fails on the image, that's the most common culprit.

The minimum is enforced by `KARTEND_MIN_QT_VERSION` in
[CMakeLists.txt](../CMakeLists.txt). Bumping it is a deliberate
choice; don't ratchet it up to silence a CI failure.

### Silent `QHash::insert` copy

Qt 6.5+ changed `QHash::insert` to return an iterator; on 6.4 it
returns `void`. Code that relied on the iterator return value
**compiles cleanly on the host** (since 6.5 took the change) but
gets a use-of-void-result error on the image. Always wrap inserts
that need the iterator with a separate lookup pass for portability.

### clang-format version drift

The repo formats with **clang-format 19**. Newer system clang-
formats (21+) drift in subtle ways (alignment around lambdas,
some macros). The CI image pins v19; the pre-commit hook also
expects v19 to be the binary called `clang-format`.

On Arch:

```bash
sudo pacman -S clang-tools-extra-19   # if available, else aur/llvm19-bin
export PATH=/usr/lib/llvm/19/bin:$PATH
```

(See the [clang-format v19 pin](../README.md) feedback note.)

### Stale `.profraw` files

LLVM PGO / coverage runs litter `*.profraw` files in the build
directory. If a host and a docker build share the same `build/`
tree, the next coverage run merges *both* sets and produces
nonsense. Always run `docker:*` against a fresh subdir
(`build/docker-release`, `build/docker-tsan`) or clean before
switching.

### ctest cwd

Critical: **always run `ctest` from the build directory the
`build.sh` script just emitted**, not from a parent / shared dir.
Stale binaries pass silently when the working tree drifts past
them. The build script prints the directory name at the end of its
run; copy-paste that.

### Sanitizer suppressions

[.tsan_suppressions.txt](../.tsan_suppressions.txt) and
[.lsan_suppressions.txt](../.lsan_suppressions.txt) carry workaround
entries that only kick in for the CI configuration (specific Qt /
Pulse stack versions). Local sanitizer runs on a newer Qt may not
need them, but **leave them in place** — they don't fire on clean
code, and stripping entries that are "no longer needed locally"
breaks CI on the still-affected Qt 6.4.

When adding a new suppression, see
[sanitizer-suppressions.md](sanitizer-suppressions.md).

## Quick "is my change safe to push?" checklist

```bash
.scripts/build.sh                  # host build, ~3 min
.scripts/ci-local.sh docker:tidy   # ~10 min — clang-tidy + format
.scripts/ci-local.sh docker:build  # ~12 min — release + ctest
# push
```

`docker:tsan` is slow enough to skip routinely; run it before merging
anything threading-related or touching the database / scraper code.

## Related code

| Concern | File |
|---------|------|
| Dockerfile | [.scripts/Dockerfile.ci](../.scripts/Dockerfile.ci) |
| Wrapper script | [.scripts/ci-local.sh](../.scripts/ci-local.sh) |
| Build script (called inside the image) | [.scripts/build.sh](../.scripts/build.sh) |
| Sanitizer suppressions | [.tsan_suppressions.txt](../.tsan_suppressions.txt), [.lsan_suppressions.txt](../.lsan_suppressions.txt) |
| Minimum Qt version | [CMakeLists.txt](../CMakeLists.txt) (`KARTEND_MIN_QT_VERSION`) |
| Build docs | [building.md](building.md) |
