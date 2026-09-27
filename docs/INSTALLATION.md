# Installing protoST

protoST is a Smalltalk-flavoured language on the protoCore runtime. It is a
consumer of protoCore, never a bundler of it: `bin/protost` links
`libprotoCore.so.2`, and every package protoST produces declares a runtime
dependency on protoCore's own package instead of shipping a copy.

---

## Prerequisites

- A **C++20** compiler (GCC or Clang).
- **CMake** 3.20 or newer.
- **libreadline** (`libreadline-dev` on Debian/Ubuntu, `readline-devel` on
  Fedora/RHEL, `brew install readline` on macOS). It is a hard requirement:
  `find_library(READLINE_LIBRARY NAMES readline REQUIRED)`.
- **protoCore 2.0.0 or newer**, installed, with its CMake package
  configuration. See protoCore's `docs/INSTALLATION.md`.
- Network access on the first configuration: Catch2 and nlohmann/json are
  fetched with `FetchContent` when they are not already available.

---

## Building against an installed protoCore

protoST prefers an installed protoCore CMake package:

```bash
# protoCore installed in a default prefix: nothing to pass.
cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release

# protoCore installed elsewhere.
cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release -DPROTO_CORE_PREFIX=$HOME/.local
# equivalently
cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$HOME/.local

cmake --build build_release -j4
ctest --test-dir build_release --output-on-failure
```

The discovery is `find_package(protoCore 2.0 CONFIG)`, so the prefix must hold
`lib/cmake/protoCore/protoCoreConfig.cmake`. **A prefix holding only
`libprotoCore` and `protoCore.h` is no longer accepted**: without the package
configuration there is no way to tell protoCore 1.x from 2.x, and linking the
wrong major version is silent.

The version floor is `2.0` and the ceiling is the next major version: protoST
uses no protoCore API newer than 2.0.0, and protoCore's major version and its
soname move together. protoST additionally asserts that the package's
`SOVERSION` is `2`.

## Building against a sibling developer tree

When no installed package is found *and* no prefix was named, protoST falls back
to the sibling source tree `../protoCore`, searching `build_release`, then
`build`, then `build_check` — the first directory holding `libprotoCore` wins,
and `build_release` comes first so a leftover `build/` cannot shadow it. The
fallback prints a `WARNING`: it performs no package version check (it does check
that the build carries `SOVERSION 2`) and must not be used to produce a
distributable package.

Pass `-DPROTOCORE_REQUIRE_PACKAGE=ON` to turn the fallback into a hard error.
**Every packaging build sets it.**

Switching a build directory between the two modes leaves a stale
`PROTOCORE_LIBRARY` cache entry; delete the build directory rather than
reconfiguring in place.

---

## Installing

```bash
cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=$HOME/.local -DCMAKE_PREFIX_PATH=$HOME/.local
cmake --build build_release -j4
cmake --install build_release --component protoST
```

Installed layout, relative to the prefix (`<libdir>` is CMake's
`CMAKE_INSTALL_LIBDIR`, usually `lib` or `lib64`):

| Content | Location |
|---------|----------|
| `protost` | `bin/` |
| Standard-library `.st` modules (`json`, `random`, `stream`, `time`) | `share/protoST/lib/` |
| `LICENSE` and the Markdown documentation | `share/doc/protoST/` |
| VS Code editor integration, when present in the source tree | `share/protoST/editor-integration/vscode/` |

protoST installs **no** copy of protoCore. `bin/protost` carries the install
RPATH `$ORIGIN/../<libdir>` (`@executable_path/../<libdir>` on macOS), so a
protoCore installed into the same prefix is found with no `LD_LIBRARY_PATH`.

---

## How `protost` finds its standard library

`discoverStdlibDir()` (`src/runtime/STRuntime.cpp`) probes, in order:

1. `$PROTOST_LIB`, as given;
2. paths derived from the running executable's own location, in this order:
   `<dir-of-exe>/../share/protoST/lib` (the installed layout),
   `<dir-of-exe>/share/protoST/lib` (a flat install directory, as on Windows),
   then `<dir-of-exe>/lib`, `<dir-of-exe>/../lib` and `<dir-of-exe>/../../lib`
   (development trees);
3. `<cwd>/lib`.

Step 2 is what makes an installed `bin/protost` resolve
`Import from: 'stream'` out of `share/protoST/lib` with nothing set in the
environment, and it is what keeps the installation relocatable. The installed
layouts are probed **before** the generic `../lib`, because in an installation
`<prefix>/lib` is the *library* directory — it holds `libprotoCore` and no `.st`
module at all.

The executable is located with `/proc/self/exe` on Linux,
`_NSGetExecutablePath` on macOS and `GetModuleFileNameA` on Windows. Only the
Linux branch has been executed: the macOS and Windows branches are compiled from
the same code but **never run here** (D-I6). On Linux the lookup is proved by a
positive test (an installed `protost` importing `stream` with `PROTOST_LIB`
unset) and a negative control (the same command with `share/protoST` moved away,
which must fail).

---

## Packages (CPack)

```bash
cmake -S . -B build_pkg -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=<protocore-prefix> -DPROTOCORE_REQUIRE_PACKAGE=ON
cmake --build build_pkg -j4
cd build_pkg && cpack -G DEB
```

Generators are chosen at configure time; the DEB and RPM generators are enabled
only when `dpkg` and `rpmbuild` are found, because `cpack` aborts the whole run
when a generator's tool is missing and would take the TGZ down with it. Each
configure prints whether a generator was enabled or disabled, and why.

Package names are pinned rather than left to each generator's default casing:
`protost` for DEB, `protoST` for RPM. Both declare a bounded dependency on
protoCore's own package:

| Format | Relation |
|--------|----------|
| DEB | `Depends: protocore (>= 2.0.0), protocore (<< 3.0.0)` |
| RPM | `Requires: protoCore >= 2.0.0, protoCore < 3.0.0` |

`libreadline` is a real runtime dependency of `protost` and is **not** declared
in the DEB; `CPACK_DEBIAN_PACKAGE_SHLIBDEPS` is not enabled for protoST.

### Platform verification status

Last verified 2026-09-27 against protoST 0.3.0 and protoCore 2.5.0
(`PROTOCORE_ABI_SOVERSION 3`), built with `-DPROTOCORE_REQUIRE_PACKAGE=ON` so the
sibling developer fallback was a hard error.

| Platform | Packaging | Status |
|----------|-----------|--------|
| Linux / Debian-Ubuntu | TGZ, DEB | **VERIFIED.** Installed with `dpkg -i` as root in a throwaway `ubuntu:24.04` container and run there from `/usr/bin/protost`, outside any repository, with no `LD_LIBRARY_PATH` and no `PROTOST_LIB` set; the stdlib was found under `share/protoST/lib` through the executable's own location. |
| Linux / Fedora-RHEL | TGZ, RPM | **VERIFIED.** `cpack -G RPM` executed in a throwaway `fedora:41` container (glibc 2.40, `rpm` 4.20.1); the RPM installed with `rpm -i` and `protost` ran correctly there. This closes the gap left by decision D-I2. |
| macOS | DragNDrop | **UNVERIFIED.** Configured and reviewed only; there is no macOS host here. The macOS and Windows branches added to `discoverStdlibDir()` under D-I5 compile but have never run. Review is not verification. |
| Windows | NSIS, ZIP | **UNVERIFIED.** Configured and reviewed only; there is no Windows host here. |

### Known defect: the DEB dependency floor does not encode the ABI

The `Depends` field is a *version range*, and on its own that range is not an ABI
check. `PROTOCORE_ABI_SOVERSION` went from `2` to `3` in protoCore **2.2.0**, so
protoCore 2.0.0 and 2.1.0 carry `libprotoCore.so.2` while 2.2.0 and later carry
`libprotoCore.so.3`. A floor of ``2.1.0`` therefore admits a protoCore whose
SONAME this package was not linked against.

This was demonstrated, not argued. A decoy `protocore` 2.1.0 package providing
only `libprotoCore.so.2` was installed in a container; `dpkg -i` then accepted
this package, and the installed binary failed to start with
`libprotoCore.so.3: cannot open shared object file`. The install succeeded and
the program did not run.

Two things limit the damage, and one closes it:

- At **build** time the failure is loud, not silent. `find_package(protoCore …)`
  alone does accept a SOVERSION-2 protoCore, but `CMakeLists.txt` follows it with
  an explicit `protoCore_SOVERSION` assertion against `PROTOCORE_ABI_SOVERSION`,
  which stops configuration with a `FATAL_ERROR` naming both numbers. Verified by
  configuring against a complete forged 2.1.0 / SOVERSION 2 prefix.
- The **RPM** does not have this hole. `rpm` generates
  `Requires: libprotoCore.so.3()(64bit)` automatically from the linked binary, and
  that requirement is on the SONAME rather than the version. Verified: the decoy
  protoCore 2.1.0 does not satisfy it and `rpm -i` refuses.
- Raising the DEB floor to `2.2.0`, the first protoCore that shipped SOVERSION 3,
  would make the DEB range agree with the ABI. That is a packaging change for the
  maintainer to take, and it is not made here.

### Known defect: the DEB does not refresh the shared-library cache

Neither this package nor protoCore's carries a `postinst` or an `ldconfig`
trigger, so `ldconfig -p` does not list `libprotoCore.so.3` after `dpkg -i`.
Programs still start, because each binary carries
`RUNPATH $ORIGIN/../${CMAKE_INSTALL_LIBDIR}` and because the library lands in a
directory the dynamic loader searches by default, but the cache is misleading.
Run `ldconfig` after installing. The RPM has no such defect.
