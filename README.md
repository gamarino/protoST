# protoST

[![CI](https://github.com/gamarino/protoST/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/gamarino/protoST/actions/workflows/ci.yml)
[![Cross-platform](https://github.com/gamarino/protoST/actions/workflows/cross-platform.yml/badge.svg?branch=main)](https://github.com/gamarino/protoST/actions/workflows/cross-platform.yml)

protoST is a Smalltalk-syntax, actor-native runtime on protoCore: a demonstrator and a base for digital twins, not a Smalltalk-80 implementation or a replacement for an image environment.

Programs are plain files. An object is an ordinary object until it is sent
`asActor`, which makes it an actor served by a pool of native worker threads
(see "Objects become actors" below). Programs read and write files, work as
Unix filters, take arguments and set their exit status, run other programs,
and talk TCP, UDP and HTTP(S) (see "Input, output and the network" below).

If you write Smalltalk, most of what you know works as you expect: classes and
metaclass-side methods, blocks and non-local return, exceptions with `retry`,
`resume:` and `pass`, the collection and stream protocol, exact fractions and
large integers, `printOn:`. What differs is listed, with what to write instead,
in [tutorial chapter 14](docs/tutorial/14-for-the-smalltalk-programmer.md).

## A flavour of the language

```smalltalk
Object subclass: #Account instanceVariableNames: 'owner balance'.

Account class >> owner: aString
  ^ self new setOwner: aString

Account >> initialize
  balance := 0

Account >> setOwner: aString
  owner := aString

Account >> deposit: amount
  balance := balance + amount

Account >> balance
  ^ balance

| accounts |
accounts := #('Ana' 'Luis') collect: [:name | Account owner: name].
accounts do: [:each | each deposit: 100].
accounts inject: 0 into: [:sum :each | sum + each balance]   "=> 200"
```

`protost accounts.st` runs the file and shows only what the program prints;
`protost --print-last accounts.st` also prints the value of its last statement
(`200`).
A blank line ends a method body; there is no image and no browser: the program
is the file.

## Objects become actors

Any object becomes an actor with `asActor`. A message sent to an actor is
queued in its mailbox and answers a `Future` at once; the actor runs one
message at a time, on a pool of native worker threads shared by all actors, so
its instance variables need no locks. The exception is a message about the
reference itself, which the proxy answers at once without queueing it:
identity and equality (`==`, `~~`, `=`, `~=`, `hash`, `identityHash`,
`yourself`), the nil tests (`isNil`, `notNil`, `ifNil:` and its variants),
`isActor`, and printing (`printString`, `printOn:`, `displayString`,
`printNl`, `displayNl`, which show `a Counter (actor)`).

```smalltalk
Object subclass: #Counter instanceVariableNames: 'n'.

Counter >> initialize
  n := 0

Counter >> increment
  n := n + 1.
  ^ n

| c |
c := Counter new asActor.
1 to: 1000 do: [:i | c increment].
c increment wait   "=> 1001"
```

`wait` blocks until the reply arrives; `thenDo:`, `catch:`, `Future whenAll:`
and `whenAny:` compose replies without blocking. An error inside an actor
rejects that message's future, and `wait` re-signals the same exception in the
caller. An actor waiting inside a method handles no other message until the
reply arrives; a cycle of actors waiting on each other is reported as an
error.

What makes passing objects between threads safe is protoCore's object model:
a mutable object is an atomic reference to an immutable snapshot of its state,
and the structures that hold a collection's elements are immutable and shared
by structure. From Smalltalk code, `Array>>at:put:` and
`OrderedCollection>>add:` still change the object: each installs a new
snapshot. A message carries a reference, not a copy, so an object sent to an
actor is shared with the sender; each read sees one consistent snapshot, and a
change made on one side is seen by the other at its next read.

## Input, output and the network

Files, standard streams, the command line and other programs are in the
kernel, with Pharo's names where Pharo has them (`'data.txt' asFileReference
contents`, `writeStreamDo:`, `Stdio stdin nextLine`, `Smalltalk arguments`,
`Smalltalk exit: 1`, `OSProcess run:arguments:`). A script whose first line
is `#!/usr/bin/env protost` runs as a command and chains in pipelines
([`examples/unix/`](examples/unix/)). Sockets and HTTP are
modules: `Import from: 'net'` (TCP with a TLS client, UDP) and
`Import from: 'http'` (an HTTP/1.1 client for http and https, and a small
server that handles each connection on its own actor):

```smalltalk
Import from: 'http'.
server := HTTPServer on: 0 handler: [:request |
  Dictionary new at: 'pump' put: (request query at: 'id'); at: 'flow' put: 42; yourself].
server startInBackground.
reply := HTTPClient get: 'http://127.0.0.1:' , server port printString , '/state?id=p1'.
server stop.
reply json at: 'flow'   "=> 42"
```

Calls block the thread that makes them; concurrency comes from actors, one
per connection, and the worker pool adds threads while workers are blocked in
I/O (up to 256). Failures are classed errors (`FileDoesNotExist`,
`ConnectionRefused`, …). Not provided: HTTP/2, WebSockets, a TLS server,
non-blocking multiplexing. The whole protocol is in
[tutorial chapter 15](docs/tutorial/15-input-and-output.md). The operating-system
layer underneath is [protoIO](https://github.com/gamarino/protoIO), the I/O
library protoST shares with protoScala and protoClojure.

## Why digital twins

A digital twin mirrors a physical system — a pump, a line, a substation — as a
set of state machines that react to events. With one actor per component, each
event is handled atomically by its component, components interact by
messages, and the runtime spreads the work over the cores. The talk demo
[`docs/talks/2026-10-15-fas/demos/03-digital-twin.st`](docs/talks/2026-10-15-fas/demos/03-digital-twin.st)
reads three sensors concurrently every cycle and stops the pump on an alarm;
[`examples/pump_twin.st`](examples/pump_twin.st) is the fuller version.

protoCore also hosts other runtimes (JavaScript, Python, Clojure, Scala). A
process that mixes them — a twin in protoST, a model in Python — is the
direction of the project, not what it does today: see
[`docs/INTEROP.md`](docs/INTEROP.md) §0 for exactly what works across runtimes
now.

## Performance

**protoCore's unified exception model (measured 2026-10-22).**
- Branch `feature/protocore-3-calling-convention` at `b8b71b3`, against
  `c0ba168` (protoCore `cf52f9f5`). Both Release builds, interleaved, three rounds. The machine had 2-4 busy CPUs, so only the ratios count, not the milliseconds.
- A Smalltalk exception is now a `proto::ProtoException` selected by
  prototype. `on: Exception do:` catches every value, including errors that a
  C++ module or another language defines.

| | AFTER / BEFORE |
|---|---:|
| geomean, all 17 benchmarks | 0.998 (comparables 1.049; actor cells 0.964) |
| worst cells, `str_concat` 1.14 / `list_append` 1.12 (wall time) | 0.99-1.02 in a second pass counting CPU cycles: noise |
| `on:do:` entered, nothing signalled | 1.00 |
| signal and catch in the same method | 0.99 |
| signal caught 5 frames up | 1.04 |
| `1 / 0` caught | 0.97 |

- The port is neutral.
- A finding the port did not cause: entering `on:do:` costs about 60,000
  cycles even when nothing is signalled, 150 times an empty loop iteration.
  That is the next target for the exception path.
- No Pharo was available, so CPython 3.14 is the only comparable.
- Raw results: [`benchmarks/reports/2026-10-22-unified-exceptions/`](benchmarks/reports/2026-10-22-unified-exceptions/).

Synthetic workloads on one notebook (AMD Ryzen 5 5500U, 6 cores / 12
threads), every benchmark verifying its result. The current figures come
from
[`benchmarks/reports/2026-10-20-send-path-inline-captures.md`](benchmarks/reports/2026-10-20-send-path-inline-captures.md):
`main` after the send-path and inlined-block changes, Release, on protoCore
2.14.8, three rounds of `benchmarks/run_benchmarks.py` on a fresh copy of the
library each, against CPython 3.14.0 on the same machine; the baseline is
0.6.6 on protoCore 2.14.7 (2026-10-18). Earlier reports in
`benchmarks/reports/` give the history.

- Start-up: 25 ms to evaluate one expression; CPython 31 ms.
- Single-threaded against CPython, same algorithm, same N and same verified
  result, whole-process time (ratio > 1: protoST is slower):

  | Benchmark | protoST ms | CPython ms | Ratio | 0.6.6 / 2.14.7 |
  |---|---:|---:|---:|---:|
  | `int_sum_loop` | 47.7 | 33.7 | 1.41× | 1.41× |
  | `fib` | 89.9 | 41.9 | 2.15× | 8.91× |
  | `list_append` | 47.1 | 32.0 | 1.51× | 1.66× |
  | `str_concat` | 28.1 | 30.6 | 0.92× | 0.95× |
  | `attr_lookup` | 54.9 | 42.6 | 1.28× | 4.16× |
  | `range_iterate` | 35.8 | 34.4 | 1.03× | 2.24× |
  | `exception_latency` | 863.5 | 49.6 | 17.29× | 20.65× |
  | **Geomean** | | | **1.92×** | **3.25×** |

  The gains come from the compiler: the bodies of `ifTrue:`, `whileTrue:`
  and `to:do:` that it inlines no longer box the temporaries they use in
  the method's mutable captured dictionary, so `fib` runs 4× faster,
  `attr_lookup` 2.9× and `range_iterate` 2×. At these small N start-up is
  a large share of every time; with each runtime's start-up subtracted the
  work itself takes 2.3× (`attr_lookup`, `range_iterate`, `str_concat`) to
  4.7-4.9× (`int_sum_loop`, `fib`) and 12× (`list_append`) CPython's time,
  and 42× for exception signalling (C++ unwinding, by design).
- Parallel: twelve CPU-bound actors (`parallel_speedup`) run 2.6× faster
  with the default pool than on one worker (2.05× before). `saturation_big`
  (32 actors) runs 3.2× faster on four workers than on one and 3.5× on
  five (2.7× and 2.8× before); six workers measured 3.3× on a desktop with
  other work running, so the peak is not settled.
- protoCore 2.14.7 against 2.14.6 (2026-10-19, the same binary on a fresh copy of
  each library per run, three rounds; the write path changed): the comparable
  suite's ratios to CPython move by -1.3 % (geomean of 7), all within the A/A
  spread; an instance-variable write costs 852 cycles against 935 (-9 %, A/A
  5 %).  Details: [protoCore's report](https://github.com/numaes/protoCore/blob/master/docs/reports/2026-10-18-write-path.md).
- protoCore 2.14.6 against 2.14.5 (idle machine, 2026-10-12, late evening): demo 2's
  one-thread half 47 % faster and its twelve actors 38 % faster (integer
  comparison and modulo no longer allocate), so the ratio it prints *falls*
  from 2.8× to 2.4× (the earlier 4.4× and 3.8× figures came from a busy
  machine, which slows the one-thread half more);
  `exception_latency` 7 % faster and `parallel_speedup` with the pool 8 %;
  everything else unchanged within the noise, with 1-3.5 % fewer
  instructions. Under a fixed 640 MB cell limit, `(1 to: 500000) asArray`
  now peaks at 690 MB of RSS instead of 1,165 MB (`KNOWN_ISSUES.md` K5).
- Since 0.6.2 protoST lets protoCore's adaptive controller size the heap:
  against 0.6.1's fixed limit
  ([`2026-10-12-adaptive-heap-default.md`](benchmarks/reports/2026-10-12-adaptive-heap-default.md)), actor workloads ran 16-43 % faster with a
  fifth to a half of the peak memory and pauses of at most 0.27 ms, but
  **peak memory is no longer capped** (an allocation-bound loop peaked at
  0.88-3.5 GB). Set `PROTOCORE_HEAP_LIMIT_CELLS` when a process must stay
  within a fixed size (see "Status" below and `KNOWN_ISSUES.md` K5).

protoST is not fast single-threaded code; what it offers is the same code on
several cores without locks.

## Status

Version 0.7.0 (2026-10-22); built from source or as a `.deb` it requires
protoCore 3.0.0 or newer, and CI builds it against protoCore 3.0.0 (tag
`v3.0.0`). On 0.6.6 (2026-10-17), `ctest` ran 1105 cases on Linux, all passing: 544
conformance programs, 462 unit tests, 44 examples, 33 CLI tests (including
the benchmark-harness self-test) and 22 documentation checks (every example
with a stated result in 21 documents, plus the checker's self-test). Since 0.6.2 protoST lets protoCore's adaptive heap
controller size the heap (budget: 75 % of physical memory or of the process
memory limit); `PROTOCORE_HEAP_LIMIT_CELLS` forces a fixed limit and
`PROTOCORE_ADAPTIVE_HEAP=0` restores 0.6.1's fixed 640 MB limit (precedence
in [`KNOWN_ISSUES.md`](KNOWN_ISSUES.md) K5; measurements in
`benchmarks/reports/2026-10-12-adaptive-heap-default.md`). The
known deviations from Smalltalk-80 and the open bugs are tracked in
[`docs/STATUS.md`](docs/STATUS.md); runtime hard edges in
[`KNOWN_ISSUES.md`](KNOWN_ISSUES.md); what changed in
[`CHANGELOG.md`](CHANGELOG.md).

## Getting started

protoST depends on [protoCore](https://github.com/numaes/protoCore) 3.0.0 or
newer (below 4.0), which must be built first, on
[protoIO](https://github.com/gamarino/protoIO) 0.2.2 or a later 0.2.x at build time (a sibling
checkout `../protoIO`, or its `protoio-dev` package; it is linked statically),
and on OpenSSL (`libssl-dev`) and libreadline. 3.0.0 is a hard floor (a
thread's method receives its arguments as `ProtoArguments`); 2.11.0 already was
one before it: runs of
instance-variable assignments are published as one version with
`ProtoObject::setAttributes`, which protoCore introduced there; the sources
also use the portable 64-bit integer names (`proto::proto_long`) of 2.7.0, and
since 2.6.1 the worker pool creates threads from worker
threads while actors block in I/O, which older protoCore releases did not
support safely. By default the build looks for a protoCore checkout next to
protoST (`../protoCore`) and uses the first of these directories that holds
`libprotoCore`: `build_release/`, then `build/`, then `build_check/`. The
choice is cached in `PROTOCORE_LIBRARY` on the first configure; pass
`-DPROTOCORE_LIBRARY=<path to libprotoCore.so>` to pick a library explicitly
(or remove that cache entry to search again), or
`-DPROTO_CORE_PREFIX=<prefix>` to use an installed protoCore.

```bash
git clone https://github.com/numaes/protoCore.git
git clone https://github.com/gamarino/protoIO.git
git clone https://github.com/gamarino/protoST.git

cmake -S protoCore -B protoCore/build_release -DCMAKE_BUILD_TYPE=Release
cmake --build protoCore/build_release --target protoCore

cd protoST
cmake -B build -S .
cmake --build build -j

./build/protost script.st [args...]   # run a .st script
./build/protost -e 'expr'             # evaluate an expression and print the result
./build/protost -i                    # interactive REPL (history, multi-line)
./build/protost -d script.st          # run under the CLI debugger
./build/protost --dap                 # Debug Adapter Protocol server (VS Code)
./build/protost --dump-ast script.st  # parse a script and print its AST
./build/protost venv create .venv     # create an isolated environment
./build/protost --help                # usage (also -h)
./build/protost --version             # print the version (also -v)

ctest --test-dir build                # run the test suite
```


On **Windows**, protoST builds natively with Visual Studio 2022 (MSVC) against
an installed protoCore (`-DCMAKE_PREFIX_PATH=<its prefix>`) and an OpenSSL 3
for Windows (`-DOPENSSL_ROOT_DIR=...`); readline is optional there. The
commands, and what differs on Windows, are in
[docs/INSTALLATION.md — Windows (MSVC)](docs/INSTALLATION.md#windows-msvc).

## Installation

protoST can be packaged with CPack. The 0.7.0 release has a Linux `.deb`
attached to its GitHub release (below, with protoCore 3.0.0); other packages
are built from source as shown below. Every package depends on
[protoCore](https://github.com/numaes/protoCore) — install protoCore's package
first (it provides `libprotoCore`).

**Debian / Ubuntu** — install the `.deb`:

```bash
sudo apt install ./protoCore-<version>-Linux.deb # the protoCore dependency
sudo apt install ./protost-<version>-Linux.deb # protoST itself
# or, lower-level:
sudo dpkg -i protost-<version>-Linux.deb && sudo apt-get install -f
```

**Installing on Windows.** protoST builds natively with Visual Studio 2022
(MSVC) and runs scripts, `-e` and the REPL from `cmd.exe` or PowerShell; the
steps, and what differs there (no readline, `/` in the paths the file
primitives answer, `cmd.exe` as `OSProcess`'s shell, batch files refused by
`run:`), are in
[docs/INSTALLATION.md — Windows (MSVC)](docs/INSTALLATION.md#windows-msvc).
Alternatively, use WSL2 with Ubuntu 24.04 and the Linux packages attached to
the GitHub releases (or built as below):

```bash
wsl --install -d Ubuntu-24.04        # in PowerShell, once
# then, in the Ubuntu shell:
wget https://github.com/numaes/protoCore/releases/download/v3.0.0/protoCore-3.0.0-Linux.deb
wget https://github.com/gamarino/protoST/releases/download/v0.7.0/protost-0.7.0-Linux.deb
sudo apt install ./protoCore-3.0.0-Linux.deb ./protost-0.7.0-Linux.deb
```

These are the 0.7.0 release packages. Built from source, protoST 0.7.0 needs
protoCore 3.0.0 or later; the `.deb` was built against 3.0.0, the release
to install with it, and declares `protocore (>= 3.0.0)` (an older protoCore package, such as the 2.14.6 that went with 0.6.6, is
not enough: upgrade it with the command above).

**macOS — built and tested in CI, not packaged.** The cross-platform CI job
builds protoST on macOS (arm64, Apple clang) and runs the whole suite there;
the `.dmg` (DragNDrop) that `CMakeLists.txt` configures has not been built.
On Windows the `.zip` is self-contained -- `protost.exe` with protoCore's DLL,
the OpenSSL DLLs (and OpenSSL's licence) and the MSVC runtime next to it --
and CI unpacks it into an empty directory and runs it from there; the NSIS
installer is built in CI too (when `makensis` is present) but has not been
installed and run. See [`docs/INSTALLATION.md`](docs/INSTALLATION.md).

The installed `protost` lands on your `PATH`; the standard library is installed
to `<prefix>/share/protoST/lib`, so `Import from: 'stream'` resolves with no
`PROTOST_LIB` set.

### Building the packages from source

After a normal build, run CPack from the build directory:

```bash
cmake -B build -S . && cmake --build build -j
cd build
cpack -G DEB    # Debian/Ubuntu .deb (Linux)
cpack -G RPM    # RPM (Linux, needs rpmbuild)
cpack -G TGZ    # portable .tar.gz (Linux)
cpack -G DragNDrop   # .dmg (macOS; never built)
cpack -G ZIP         # .zip (Windows; self-contained)
cpack -G NSIS        # installer .exe (Windows; enabled when makensis is found)
```

The generators are selected per platform in `CMakeLists.txt` (Linux: DEB, RPM,
TGZ, each only when its tool is found; macOS: DragNDrop; Windows: ZIP, plus
NSIS when `makensis` is found); `cpack` with no `-G` builds every generator
enabled for the host OS. The `.deb` and `.tar.gz` packages have been verified
on Linux and the `.zip` on Windows (in CI, from a clean directory); the NSIS
installer is built in CI but has not been installed and run, and the macOS
generator has never been run.

## Debugging in VS Code

The repository ships a VS Code extension in
[`editor-integration/vscode/`](editor-integration/vscode/): syntax highlighting
for `.st` files, and debugging through the `protost --dap` adapter built into
`protost` itself — line breakpoints, stepping (over, into, out of a method or
a block), call stack, locals, Watch and the Debug Console (the script's output
appears there), exception breakpoints (Raised / Uncaught), and the actor
threads: each worker thread is a thread in VS Code, and a thread that stops
stops alone while the others keep running.

1. Install protoST (above) so `protost` is on your `PATH`, or note where it is.
2. Package and install the extension (needs Node.js for `npx`):

   ```bash
   cd editor-integration/vscode
   npx @vscode/vsce@3.2.1 package --skip-license
   code --install-extension protost-debug-0.3.0.vsix
   ```

   Then run **Developer: Reload Window** in VS Code.
3. Open a folder with `.st` scripts (for instance `examples/`, which has a
   ready-made `.vscode/launch.json`), click in the gutter to set a breakpoint
   and press **F5**. Without a `launch.json`, F5 debugs the active `.st` file.

The extension runs the `protost` named in **Settings → Extensions → protoST →
Protost: Path**; if that is empty, the one in the default install location
(`%LOCALAPPDATA%\Programs\proto\bin\protost.exe` on Windows,
`~/.local/bin/protost` elsewhere); otherwise `protost` from `PATH`. See
[`docs/debugging.md`](docs/debugging.md) for what works in detail. Current
limitations:

- values are flat: an object prints as `a Counter` and is not expandable;
- an expression in the Debug Console sees the frame's variables by value (an
  assignment to one does not reach the frame);
- a thread blocked in native code (a Future's `wait`, I/O, an idle worker) is
  listed as running, and Pause stops it only once it runs script code again;
- Raised Exceptions also stops where kernel code signals and handles an
  exception itself;
- launch only: no attach, restart, conditional or data breakpoints.

## Documentation

| Document | What it covers |
|---|---|
| [docs/TUTORIAL.md](docs/TUTORIAL.md) | The dual-audience tutorial — teaches protoST from the ground up for Python/JavaScript developers, and catalogues every departure from Smalltalk-80 for Smalltalk programmers. 15 chapters under `docs/tutorial/`, the last on input and output. |
| [docs/LANGUAGE.md](docs/LANGUAGE.md) | The language reference — lexical structure, grammar, semantics, the full built-in protocol. |
| [docs/STATUS.md](docs/STATUS.md) | The living status tracker — implemented features, intentional deviations, open bugs. |
| [KNOWN_ISSUES.md](KNOWN_ISSUES.md) | Runtime hard edges that are not language design choices. |
| [docs/ROADMAP.md](docs/ROADMAP.md) | The roadmap — remaining tracks and how to contribute. |
| [docs/INTEROP.md](docs/INTEROP.md) | Cross-language UMD interop. §0 lists what is verified with a real second runtime, what is tested only with a simulated provider, and what does not work. It also covers how protoST consumes and serves modules, the type mapping and the multi-runtime follow-up plan. |
| [docs/debugging.md](docs/debugging.md) | Debugging `.st` scripts in VS Code via the `protost --dap` Debug Adapter Protocol adapter. |
| [CHANGELOG.md](CHANGELOG.md) | Release notes and unreleased changes. |
| [benchmarks/README.md](benchmarks/README.md) | The benchmark suite and harness; dated reports live in `benchmarks/reports/`. |
| [Design specifications (archive)](docs/archive/design-specs/README.md) | Historical design documents, including the original technical design; not maintained. |

## Related projects

Five language runtimes (protoJS, protoPython, protoClojure, protoScala and protoST) and protoCpp's C++ examples are built on protoCore.

| Project | Role | Repository |
|---|---|---|
| protoCore | C++20 object model and runtime kernel: immutable structures, concurrent GC, GIL-free threads | https://github.com/numaes/protoCore |
| protoJS | JavaScript runtime on protoCore | https://github.com/gamarino/protoJS |
| protoPython | Python 3 runtime (protopy) and ahead-of-time compiler (protopyc) on protoCore | https://github.com/gamarino/protoPython |
| protoST | Smalltalk-syntax programs with actors on protoCore | https://github.com/gamarino/protoST |
| protoClojure | Clojure dialect on protoCore (early stage) | https://github.com/gamarino/protoClojure |
| protoScala | Scala-syntax runtime on protoCore | https://github.com/gamarino/protoScala |
| protoCpp | Examples and benchmarks using protoCore directly from C++ | https://github.com/gamarino/protoCpp |
| protoIO | Shared input and output for the runtimes: files, processes, TCP, UDP, TLS and HTTP/1.1 (used by protoST, protoScala and protoClojure) | https://github.com/gamarino/protoIO |

Its actor model was informed by protoJS's `Deferred` / `CPUThreadPool` design
(see the references of the
[original design specification](docs/archive/design-specs/2026-05-19-protost-design.md)).

## Why "protoST"?

`proto` — built on protoCore. `ST` — Smalltalk. The name signals what it is and where it lives in the family.

## The Swarm of One

protoST is designed and maintained by a single architect, Gustavo Marino,
working with AI coding agents that draft code, tests and documentation under
human review.

## License

Copyright (c) 2023-2026 Gustavo Marino. Released under the MIT License; see [LICENSE](LICENSE).
