# Installing protoST

protoST 0.5.0 is a Smalltalk-syntax, actor-native runtime on protoCore. It is
a consumer of protoCore, never a bundler of it: `bin/protost` links
`libprotoCore.so.3`, and every package protoST produces declares a runtime
dependency on protoCore's own package instead of shipping a copy.

On a Debian or Ubuntu machine where protoCore's package is installed, the
library is `/usr/lib/x86_64-linux-gnu/libprotoCore.so.3`, a link to
`libprotoCore.so.<version>`. The 0.5.0 release was built and tested against
protoCore 2.6.1; the current sources need 2.11.0 or later (*Prerequisites*).
CI builds against protoCore 2.14.1 on Linux, macOS and Windows, and against
2.11.0, the declared minimum, in one Linux job (the floor job), both with
protoIO 0.2.2; the suite was last run locally (2026-10-04) against protoCore
2.14.1 installed in a private prefix.

---

## Prerequisites

- A **C++20** compiler (GCC or Clang).
- **CMake** 3.20 or newer.
- **libreadline** (`libreadline-dev` on Debian/Ubuntu, `readline-devel` on
  Fedora/RHEL, `brew install readline` on macOS). It is a hard requirement:
  `find_library(READLINE_LIBRARY NAMES readline REQUIRED)`, except on Windows,
  where the REPL uses the console's own line editing (*Windows (MSVC)*).
- **protoIO 0.2.2 or a later 0.2.x** at build time only (0.2.1 adds
  `process::shell`, which `OSProcess shell:` uses; 0.2.2 makes a listener on
  a host-less address dual-stack, so `localhost` clients that try `::1` first
  are not refused; protoIO's package is
  compatible within one minor version, so a 0.1 or 0.3 is refused): the I/O
  library shared by the protoCore
  runtimes (files, processes, TCP, UDP, TLS, HTTP), linked statically, so the
  installed `protost` does not depend on it. Either install its `protoio-dev`
  package (or pass `-DCMAKE_PREFIX_PATH=<prefix>` / `-DprotoIO_DIR=<its build
  tree>`), or check out <https://github.com/gamarino/protoIO> next to protoST as
  `../protoIO`, which the build then compiles as part of protoST's own tree.
- **OpenSSL** development files (`libssl-dev` on Debian/Ubuntu,
  `openssl-devel` on Fedora/RHEL), required by protoIO for TLS in the `net`
  and `http` modules. The Debian package of protoST depends on `libssl3`.
- **protoCore 2.11.0 or newer, below 3.0**, installed, with its CMake package
  configuration. 2.11.0 is required since instance-variable write groups:
  a run of assignments to instance variables is published as one version
  with `ProtoObject::setAttributes`, which first exists there. Before that,
  2.7.0 was required since the Windows port: the sources spell
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

The discovery is `find_package(protoCore 2.11.0 CONFIG)`, so the prefix must hold
`lib/cmake/protoCore/protoCoreConfig.cmake`. **A prefix holding only
`libprotoCore` and `protoCore.h` is no longer accepted**: without the package
configuration there is no way to tell protoCore 1.x from 2.x, and linking the
wrong major version is silent.

The version floor is `2.11.0` and the ceiling is the next major version, because
protoCore's major version and its soname move together. The floor is set by
`ProtoObject::setAttributes` (see Prerequisites); before it, by the portable
integer names of 2.7.0; before them it was 2.6.1, for
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

Any OpenSSL 3 for Windows with headers, import libraries and its DLLs in
`bin/` works as `OPENSSL_ROOT_DIR` (CI uses the one preinstalled on GitHub's
`windows-2022` runners; the one PostgreSQL ships, `C:/Program Files/PostgreSQL/17`,
was used for the first verification). The configuration names the two DLLs of
that OpenSSL exactly -- `libcrypto-3-x64.dll` and `libssl-3-x64.dll` -- and
fails if either is missing, rather than picking up whatever a glob finds. The
build copies the DLLs protoST needs (protoCore's, taken from its imported CMake
target so its file name does not matter, and OpenSSL's) into `build/bin/`, so
`protost.exe` and the tests run in place; keep the build directory inside the
checkout, as on Linux, so that `build/bin/protost.exe` finds the checkout's
`lib/` (*How `protost` finds its standard library*).

`cmake --install` and the packages put everything `protost.exe` loads next to
it in `<prefix>/bin`: protoCore's DLL, the OpenSSL DLLs, and the Microsoft C++
runtime (`vcruntime140.dll`, `msvcp140.dll` and the rest that
`InstallRequiredSystemLibraries` names), deployed app-local so no Visual C++
Redistributable has to be installed first. OpenSSL's licence (Apache-2.0) is
installed as `share/doc/protoST/LICENSE-OpenSSL.txt`, the standard library in
`<prefix>/share/protoST/lib`. `cpack` produces `protost-<version>-win64.zip`,
and the NSIS installer `protost-<version>-win64.exe` too when `makensis` is
found at configure time (otherwise NSIS is left out, so the ZIP is still
built). The ZIP is self-contained: CI unpacks it into an empty directory and
runs `protost.exe --version`, a script that runs a shell command and
`venv create` from there with only the Windows directories on `PATH`.

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
  `cmd.exe /d /s /c "<command>"` (`cmd.exe` from the system directory) instead
  of `/bin/sh -c`, and the command line reaches cmd exactly as written, so it
  uses cmd's syntax and quoting (`%VAR%`, not `$VAR`; `echo "a b"` prints
  the quotes); `command:` drops a final CR LF. Programs run with `run:` are
  found as `CreateProcess` finds them, except that the working directory is
  never searched, and a batch file (`.bat`, `.cmd`) is refused with an
  `Error` (protoIO 0.2): cmd would re-parse its arguments by rules no quoting makes
  safe, so run `cmd.exe /c` explicitly (or `shell:`) for one. A child that
  crashes answers 128 + the matching POSIX signal as its exit code (139 for
  an access violation), as a shell reports a child a signal ended.
  `kill:signal:` supports only 0, 9 and 15. `FileSystem home` falls back to
  `USERPROFILE` when `HOME` is not set. `Smalltalk platform` answers
  `'windows'`.
- **Scripts from standard input.** `protost -` reads the script from standard
  input, as on every platform; there is no `/dev/stdin` and no process
  substitution (`<(...)`) for a native Windows program.
- **No readline.** The console edits the line and keeps a history itself, so
  the REPL reads whole lines from it (with `ReadConsoleW`, so non-ASCII input
  arrives intact whatever the console's code page); `:history` lists the
  session's lines and no history file is written. Ctrl-Z at the start of a
  line ends the session, as Ctrl-D does elsewhere.
- **Ctrl-C** cancels the line being typed at the REPL's prompt and interrupts
  an evaluation, as on Linux and macOS. A second Ctrl-C while the first is
  still pending (an evaluation blocked where it cannot be interrupted, such as
  a network wait) ends the process with Windows' own status for Ctrl-C,
  `STATUS_CONTROL_C_EXIT` (`0xC000013A`), as does Ctrl-C in a script, where
  Linux reports 130 (killed by SIGINT).
- **venv.** `venv create` writes activation scripts for every shell on every
  platform: `bin\activate.bat` and `bin\deactivate.bat` for cmd.exe,
  `bin\Activate.ps1` for PowerShell, and the POSIX `activate` and
  `activate.fish`; `venv activate` prints the cmd.exe command. The POSIX
  `activate` names the venv with a Windows path, so under Git Bash or MSYS2
  set `STENV` yourself instead.
- **Peak memory.** `PROTOST_REPORT_PEAK_RSS=1` reports the peak working set,
  where Linux and macOS report `getrusage`'s maximum resident set size.
- **Stacks.** Executables reserve 8 MiB stacks, the Linux default the engine's
  nesting limit is sized for, and Windows gives the same to protoCore's worker
  threads; recursion through blocks ends in a catchable `Error`, as on Linux.
  Every boundary an unwind crosses re-throws after its catch clause (MSVC
  keeps the frames below a catch clause alive while it runs).
- **Dispatch.** MSVC has no computed `goto`, so the bytecode loop dispatches
  every instruction through its `switch` there (GCC and Clang keep the
  threaded dispatch).

The test suite runs the script tests through Git for Windows' `bash` and the
documentation checks and the terminal test through the Python CMake finds
(pass `-DPython3_EXECUTABLE=...` to choose one), with Git's POSIX tools (`tr`,
`wc`, `printf`) added to their `PATH`. Every test measures and checks the same
thing on every platform, except for these, which differ on Windows:

| Test | On Windows |
|------|------------|
| `cli_sigint` | Not run: it needs `pgrep`, `kill -INT` and a FIFO, which Git Bash cannot aim at a native program. `cli_repl_console` covers Ctrl-C there instead, in a console of its own with a keyboard Ctrl-C: cancelling a line, Ctrl-C at an empty prompt, and a second Ctrl-C while blocked. A script stopped with Ctrl-C is not tested on Windows. |
| `docs/docs/tutorial/15-input-and-output.md` | Not run: the `env.st` example runs `echo $PUMP_MODE`, POSIX shell syntax that `cmd.exe` does not expand. |
| `bench_harness_selftest` | Not registered: its fake `protost` binaries are `/bin/sh` scripts. |
| `cli_inputs` | Reads a script through `-` only: the `/dev/stdin` and process-substitution cases need POSIX files. |
| `cli_venv` | Activates through `activate.bat` and `deactivate.bat` in `cmd.exe` instead of sourcing `activate` in `sh`. |
| `cli_repl_console` | A console of its own instead of a pseudo-terminal: keys are written to the console's input buffer and Ctrl-C is pressed on the keyboard (`keybd_event`). ctest starts each test in a new process group, which ignores Ctrl-C and passes that on to its children, so the test clears the flag before starting `protost`, as a console a person types in has it. A `protost` started with Ctrl-C ignored still cancels the line at the prompt (the console aborts the read), but cannot be interrupted or stopped with Ctrl-C. |
| `cli_memory_bounded` | The same 1 GB bound, on the peak working set instead of the maximum resident set size. |
| `cli_kernel` | Unchanged: on every platform it bounds what `-e '1'` costs beyond starting the process (`--version`), because under Git Bash starting a native program costs about as much as the whole budget. |

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
UTF-16 name, converted to UTF-8). All three branches run in CI: Linux in
`ci.yml`, macOS (arm64) and Windows in `cross-platform.yml`, where every
script test starts the build tree's `protost`, which finds the checkout's
`lib/` through its own location. On Linux the lookup is proved by a
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
| DEB | `Depends: protocore (>= 2.11.0), protocore (<< 3.0.0)` |
| RPM | `Requires: protoCore >= 2.11.0, protoCore < 3.0.0` |

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
| macOS | DragNDrop | **BUILT AND TESTED, NOT PACKAGED.** The cross-platform CI job (`macos-14`, Apple clang, arm64) builds protoST against an installed protoCore and runs the whole suite; the `.dmg` has not been built. |
| Windows | ZIP, NSIS | **ZIP VERIFIED IN CI.** The cross-platform CI job (`windows-2022`, MSVC) builds and tests protoST, runs `cpack`, unpacks the ZIP into an empty directory and runs `protost.exe --version`, a script that runs a shell command, and `venv create` from there with only the Windows directories on `PATH`, after checking that the ZIP holds the MSVC runtime DLLs and OpenSSL's licence. The NSIS installer is built there whenever the runner has `makensis` (see the job summary); it has not been installed and run. Before CI, the ZIP and `cmake --install` were checked by hand on Windows 11 (2026-10-01, MSVC 19.44). |

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
