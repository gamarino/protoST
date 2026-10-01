# Installing protoST

protoST 0.5.0 is a Smalltalk-syntax, actor-native runtime on protoCore. It is
a consumer of protoCore, never a bundler of it: `bin/protost` links
`libprotoCore.so.3`, and every package protoST produces declares a runtime
dependency on protoCore's own package instead of shipping a copy.

On a Debian or Ubuntu machine where protoCore's package is installed, the
library is `/usr/lib/x86_64-linux-gnu/libprotoCore.so.3`, a link to
`libprotoCore.so.2.6.1` for protoCore 2.6.1 (the version this release was
built and tested against, and the oldest it accepts).

---

## Prerequisites

- A **C++20** compiler (GCC or Clang).
- **CMake** 3.20 or newer.
- **libreadline** (`libreadline-dev` on Debian/Ubuntu, `readline-devel` on
  Fedora/RHEL, `brew install readline` on macOS). It is a hard requirement:
  `find_library(READLINE_LIBRARY NAMES readline REQUIRED)`, except on Windows,
  where the REPL uses the console's own line editing (*Windows (MSVC)*).
- **protoIO 0.1** at build time only: the I/O library shared by the protoCore
  runtimes (files, processes, TCP, UDP, TLS, HTTP), linked statically, so the
  installed `protost` does not depend on it. Either install its `protoio-dev`
  package (or pass `-DCMAKE_PREFIX_PATH=<prefix>` / `-DprotoIO_DIR=<its build
  tree>`), or check out <https://github.com/gamarino/protoIO> next to protoST as
  `../protoIO`, which the build then compiles as part of protoST's own tree.
- **OpenSSL** development files (`libssl-dev` on Debian/Ubuntu,
  `openssl-devel` on Fedora/RHEL), required by protoIO for TLS in the `net`
  and `http` modules. The Debian package of protoST depends on `libssl3`.
- **protoCore 2.7.0 or newer, below 3.0**, installed, with its CMake package
  configuration. 2.7.0 is required since the Windows port: the sources spell
  protoCore's 64-bit integers `proto::proto_long` / `proto::proto_ulong`,
  which first exist there (`long` outside Windows, `long long` on Windows).
  0.5.0 itself was tested with protoCore 2.6.1, its floor: since 0.5.0 the
  actor worker pool grows while workers block in I/O, which creates threads
  from worker threads, and before 2.6.1
  protoCore's `newThread` called from a worker left the main program's
  variables unscanned by the collector. The build also requires the
  library's `SOVERSION` to be `3`. See protoCore's `docs/INSTALLATION.md`.
- Network access on the first configuration: Catch2 and nlohmann/json are
  fetched with `FetchContent` when they are not already available.
- **python3** only to run talk demo 4: its input generator,
  `docs/talks/2026-10-15-fas/demos/04-connected-twin.feed`, is a Python script
  piped into `protost`. Neither the build nor the installed `protost` needs
  it. (The test suite also uses Python 3: the documentation checks, which
  CMake registers only when it finds an interpreter, and the
  benchmark-harness self-test.)

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

The discovery is `find_package(protoCore 2.7.0 CONFIG)`, so the prefix must hold
`lib/cmake/protoCore/protoCoreConfig.cmake`. **A prefix holding only
`libprotoCore` and `protoCore.h` is no longer accepted**: without the package
configuration there is no way to tell protoCore 1.x from 2.x, and linking the
wrong major version is silent.

The version floor is `2.7.0` and the ceiling is the next major version, because
protoCore's major version and its soname move together. The floor is set by
the portable integer names (see Prerequisites); before them it was 2.6.1, for
the worker pool's growth under blocking I/O; the hashed
collections and the actor mailboxes need only 2.1.0 (`ProtoMap`, the
hashed-collection helper, `ProtoMPSCQueue`). protoST additionally asserts that
the package's `SOVERSION` is `3`.

## Building against a sibling developer tree

When no installed package is found *and* no prefix was named, protoST falls back
to the sibling source tree `../protoCore`, searching `build_release`, then
`build`, then `build_check` — the first directory holding `libprotoCore` wins,
and `build_release` comes first so a leftover `build/` cannot shadow it. The
fallback prints a `WARNING`: it performs no package version check (it does check
that the build carries `SOVERSION 3`) and must not be used to produce a
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
| Standard-library `.st` modules (`http`, `json`, `net`, `random`, `stream`, `time`) | `share/protoST/lib/` |
| Kernel classes written in protoST, loaded at start-up | `share/protoST/lib/kernel/` |
| `LICENSE` and the Markdown documentation | `share/doc/protoST/` |
| VS Code editor integration, when present in the source tree | `share/protoST/editor-integration/vscode/` |

protoST installs **no** copy of protoCore. `bin/protost` carries the install
RPATH `$ORIGIN/../<libdir>` (`@executable_path/../<libdir>` on macOS), so a
protoCore installed into the same prefix is found with no `LD_LIBRARY_PATH`.

---

## Windows (MSVC)

protoST builds and runs natively on Windows with Visual Studio 2022 (MSVC
19.44 verified, Windows 11), using the CMake and Ninja that ship with it. Build
protoCore first (its `docs/INSTALLATION.md`, "Windows (MSVC)") and install it
into a prefix; protoIO is compiled from the sibling `../protoIO` as on Linux.
From an "x64 Native Tools Command Prompt", in the protoST checkout:

```bat
set PREFIX=%LOCALAPPDATA%\Programs\proto
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH=%PREFIX% -DCMAKE_INSTALL_PREFIX=%PREFIX% ^
      "-DOPENSSL_ROOT_DIR=C:/Program Files/OpenSSL-Win64"
cmake --build build
ctest --test-dir build -j8
cmake --install build
%PREFIX%\bin\protost --version
```

Any OpenSSL 3 for Windows with headers and import libraries works as
`OPENSSL_ROOT_DIR`; the one PostgreSQL ships (`C:/Program Files/PostgreSQL/17`)
was used for the verification. The build copies the DLLs protoST needs
(`protoCore.dll`, `libssl-3-x64.dll`, `libcrypto-3-x64.dll`) into `build/bin/`,
so `protost.exe` and the tests run in place; keep the build directory inside
the checkout, as on Linux, so that `build/bin/protost.exe` finds the checkout's
`lib/` (*How `protost` finds its standard library*). `cmake --install` puts
`protost.exe` and the OpenSSL DLLs in `<prefix>/bin` and the standard library
in `<prefix>/share/protoST/lib`; protoCore's own install adds `protoCore.dll`
to its prefix's `bin`. With those on `PATH`, `protost` runs scripts, `-e` and
the REPL from `cmd.exe` or PowerShell. `cpack -G ZIP` produces
`protost-<version>-win64.zip`.

How Windows differs, by design:

- **Same output bytes everywhere.** The standard streams are binary, so
  `displayNl` writes `\n` as on Linux, and the console is switched to UTF-8.
- **UTF-8 throughout.** `protost.exe` carries a manifest that makes UTF-8 the
  process code page (Windows 10 1903 or later), so arguments, environment
  variables and file names with non-ASCII characters work as on Linux.
- **Paths use `/`.** The file primitives answer with `/` separators
  (`FileSystem temp`, `FileSystem workingDirectory`, `fullName`), which every
  Windows file API accepts, because `basename`, `parent` and `extension` split
  a path at `/`. A path written with `\` still opens, but is not split.
- **The shell is `cmd.exe`.** `OSProcess shell:` and `command:` run
  `cmd.exe /c` instead of `/bin/sh -c`, so the command line uses cmd's syntax
  (`%VAR%`, not `$VAR`); `command:` drops a final CR LF. Programs are found on
  `PATH` as `CreateProcess` finds them. `FileSystem home` falls back to
  `USERPROFILE` when `HOME` is not set. `Smalltalk platform` answers
  `'windows'`.
- **No readline.** The console edits the line and keeps a history itself, so
  the REPL reads plain lines; `:history` lists the session's lines and no
  history file is written.
- **Ctrl-C** interrupts an evaluation in the REPL, as on Linux. A script
  stopped with Ctrl-C ends with Windows' own status for it,
  `STATUS_CONTROL_C_EXIT` (`0xC000013A`), where Linux reports 130.
- **Stacks.** Executables reserve 8 MiB stacks, the Linux default the engine's
  nesting limit is sized for, and Windows gives the same to protoCore's worker
  threads; recursion through blocks ends in a catchable `Error`, as on Linux.
- **Dispatch.** MSVC has no computed `goto`, so the bytecode loop dispatches
  every instruction through its `switch` there (GCC and Clang keep the
  threaded dispatch).

The test suite runs the script tests through Git for Windows' `bash` and the
documentation checks through the Python CMake finds (pass
`-DPython3_EXECUTABLE=...` to choose one), with Git's POSIX tools (`tr`, `wc`,
`printf`) added to their `PATH`. Of the 1067 tests registered on Windows (the
benchmark-harness self-test, whose fake binaries are `/bin/sh` scripts, is
not), 1065 pass. The two that do not are not defects of protoST on Windows:
`cli_sigint` needs `pgrep`, `kill -INT` and a FIFO, which Git Bash cannot aim
at a native program (Ctrl-C was checked by hand: the REPL prints
`Interrupted` and goes on); and the `env.st` example of tutorial chapter 15
runs `echo $PUMP_MODE`, POSIX shell syntax that `cmd.exe` does not expand.

---

## Installing on Windows (WSL2)

Instead of the native build, protoST can run under WSL2 with Ubuntu 24.04,
using the Linux packages:

```bash
# In PowerShell, once: install WSL2 with Ubuntu 24.04.
wsl --install -d Ubuntu-24.04

# Then, in the Ubuntu shell: the two packages of the GitHub releases.
wget https://github.com/numaes/protoCore/releases/download/v2.6.2/protoCore-2.6.2-Linux.deb
wget https://github.com/gamarino/protoST/releases/download/v0.5.0/protost-0.5.0-Linux.deb
sudo apt install ./protoCore-2.6.2-Linux.deb ./protost-0.5.0-Linux.deb
protost --version
```

The packages are attached to the GitHub releases
[protoCore v2.6.2](https://github.com/numaes/protoCore/releases/tag/v2.6.2)
and [protoST v0.5.0](https://github.com/gamarino/protoST/releases/tag/v0.5.0)
(built on Ubuntu 24.04, x86_64). The same commands install them on Ubuntu
24.04 itself. To build them instead, see §Packages below and protoCore's own
`docs/INSTALLATION.md`.

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
`_NSGetExecutablePath` on macOS and `GetModuleFileNameW` on Windows (the
UTF-16 name, converted to UTF-8). The Linux and Windows branches have been
executed; the macOS branch is compiled from the same code but **never run
here** (D-I6). On Linux the lookup is proved by a
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
| DEB | `Depends: protocore (>= 2.7.0), protocore (<< 3.0.0)` |
| RPM | `Requires: protoCore >= 2.7.0, protoCore < 3.0.0` |

`CPACK_DEBIAN_PACKAGE_SHLIBDEPS` is enabled, so `dpkg-shlibdeps` adds the
dependencies of the system libraries `protost` links (`libc6`, `libstdc++6`,
…). It does **not** add a dependency on `libprotoCore.so.3`: protoCore's own
package ships no shlibs or symbols file, so `dpkg-shlibdeps` has nothing to
emit for it (see the known defect below).

### Platform verification status

Last verified 2026-09-27 against protoST 0.3.0 and protoCore 2.5.0
(`PROTOCORE_ABI_SOVERSION 3`), built with `-DPROTOCORE_REQUIRE_PACKAGE=ON` so the
sibling developer fallback was a hard error. The 0.4.0 packages have not yet
been through the same container verification. Since then the installed files
gained `share/protoST/lib/kernel/`, which an installed `protost` needs at
start-up, so that is what a re-verification must check first.

| Platform | Packaging | Status |
|----------|-----------|--------|
| Linux / Debian-Ubuntu | TGZ, DEB | **VERIFIED.** Installed with `dpkg -i` as root in a throwaway `ubuntu:24.04` container and run there from `/usr/bin/protost`, outside any repository, with no `LD_LIBRARY_PATH` and no `PROTOST_LIB` set; the stdlib was found under `share/protoST/lib` through the executable's own location. |
| Linux / Fedora-RHEL | TGZ, RPM | **VERIFIED.** `cpack -G RPM` executed in a throwaway `fedora:41` container (glibc 2.40, `rpm` 4.20.1); the RPM installed with `rpm -i` and `protost` ran correctly there. This closes the gap left by decision D-I2. |
| macOS | DragNDrop | **UNVERIFIED.** Configured and reviewed only; there is no macOS host here. The macOS branch added to `discoverStdlibDir()` under D-I5 compiles but has never run. Review is not verification. Since 0.5.0 a macOS build is not expected to compile unchanged: `src/primitives/io_prims.cpp` uses Linux-only calls and flags (`pipe2`, `accept4`, `SOCK_CLOEXEC`, `SOCK_NONBLOCK`). |
| Windows | NSIS, ZIP | **PARTLY VERIFIED** (2026-10-01, Windows 11, MSVC 19.44, protoCore 2.6.2). Built and tested natively (see *Windows (MSVC)*); `cmake --install` into a user prefix, then `protost --version`, a script, `-e` and the REPL run from `cmd.exe` with only that prefix's `bin` and protoCore's `bin` on `PATH`, the standard library found under `share/protoST/lib` with no `PROTOST_LIB` set; `cpack -G ZIP` builds the ZIP. The NSIS installer has not been built (no NSIS on that host). |

### Former defect: the DEB dependency floor did not encode the ABI

Since 0.5.0 the floor is protoCore 2.6.1, above 2.2.0, so the DEB range no
longer admits a protoCore with the old SONAME. The account below is kept for
the 0.4.x packages, whose floor was 2.1.0; the decoy test has not been re-run
against the 0.5.0 package.

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
- Raising the DEB floor to `2.2.0` or later, the first protoCore that shipped
  SOVERSION 3, makes the DEB range agree with the ABI. 0.5.0 raised it to
  2.6.1, for the reason given under Prerequisites.

### Known defect: the DEB does not refresh the shared-library cache

Neither this package nor protoCore's carries a `postinst` or an `ldconfig`
trigger, so `ldconfig -p` does not list `libprotoCore.so.3` after `dpkg -i`.
Programs still start, because each binary carries
`RUNPATH $ORIGIN/../${CMAKE_INSTALL_LIBDIR}` and because the library lands in a
directory the dynamic loader searches by default, but the cache is misleading.
Run `ldconfig` after installing. The RPM has no such defect.
