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
2. paths derived from the running executable's own location:
   `<dir-of-exe>/lib`, `<dir-of-exe>/../lib`, `<dir-of-exe>/../../lib`, and the
   installed layouts `<dir-of-exe>/../share/protoST/lib` and
   `<dir-of-exe>/share/protoST/lib`;
3. `<cwd>/lib`.

Step 2 is what makes an installed `bin/protost` resolve
`Import from: 'stream'` out of `share/protoST/lib` with nothing set in the
environment, and it is what keeps the installation relocatable.

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

| Platform | Packaging | Status |
|----------|-----------|--------|
| Linux | TGZ, DEB (needs `dpkg`), RPM (needs `rpmbuild`) | Built, installed to a scratch prefix and smoke-tested, including `Import from: 'stream'` resolving out of `share/protoST/lib` |
| macOS | DragNDrop | Configured and reviewed, **never built** — no macOS host |
| Windows | NSIS, ZIP | Configured and reviewed, **never built** — no Windows host |

RPM packaging is configured and reviewed but **never executed**: `rpmbuild` is
not installed on the host this was verified on.
