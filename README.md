# protoST

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
`Smalltalk exit: 1`, `OSProcess run:arguments:`). Sockets and HTTP are
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
non-blocking multiplexing, native Windows. The whole protocol is in
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

Measured on 2026-09-29 on a notebook with an AMD Ryzen 5 5500U (6 cores),
protoST 0.4.0 at commit `d3f7235` against CPython 3.14.0, with every benchmark
verifying its result; medians of five runs
([`benchmarks/reports/2026-09-29-release-0.4.0.md`](benchmarks/reports/2026-09-29-release-0.4.0.md)):

- Start-up: 27 ms to evaluate one expression (CPython: 29 ms).
- Single-threaded, same algorithm, same N and same verified result as the
  CPython twin. Whole-process time is 1.0× (string concatenation) to 23.6×
  (exception signalling) CPython's, geometric mean 3.45×; at these small N
  start-up is a large share of every time. With each runtime's start-up
  subtracted, the work itself takes about 6× CPython's time (integer loop),
  36× (`fib`, recursive method dispatch) and 64× (exception signalling),
  geometric mean 20.8× over the five workloads long enough to measure.
- Parallel: `saturation_big` (32 actors) runs 2.06× faster on four workers
  than on one and no faster on five or six; twelve CPU-bound actors run 2.1×
  faster with the default pool than with one worker. An independent re-run
  at lower load gave the same curve, so about 2.1× is the measured ceiling of
  this build on this machine.

protoST is not fast single-threaded code; what it offers is the same code on
several cores without locks.

## Status

Version 0.5.0, which requires protoCore 2.6.1 or newer. `ctest` runs 1068
cases, all passing: 537 conformance programs, 437 unit tests, 42 examples, 30
CLI tests (including the benchmark-harness self-test) and 22 documentation
checks (every example with a stated result in 21 documents, plus the
checker's self-test). The
known deviations from Smalltalk-80 and the open bugs are tracked in
[`docs/STATUS.md`](docs/STATUS.md); runtime hard edges in
[`KNOWN_ISSUES.md`](KNOWN_ISSUES.md); what changed in
[`CHANGELOG.md`](CHANGELOG.md).

## Getting started

protoST depends on [protoCore](https://github.com/numaes/protoCore) 2.6.1 or
newer (below 3.0), which must be built first, on
[protoIO](https://github.com/gamarino/protoIO) 0.1 at build time (a sibling
checkout `../protoIO`, or its `protoio-dev` package; it is linked statically),
and on OpenSSL (`libssl-dev`) and libreadline. 2.6.1 is a hard floor: the worker pool creates threads from
worker threads while actors block in I/O, which older protoCore releases did
not support safely. By default the build looks for a protoCore checkout next to
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

See [`docs/debugging.md`](docs/debugging.md) for debugging `.st` scripts in
VS Code.

## Installation

protoST can be packaged with CPack. No prebuilt packages are published; build
them from source as shown below. Every package depends on
[protoCore](https://github.com/numaes/protoCore) — install protoCore's package
first (it provides `libprotoCore`).

**Debian / Ubuntu** — install the `.deb`:

```bash
sudo apt install ./protoCore-<version>-Linux.deb # the protoCore dependency
sudo apt install ./protost-<version>-Linux.deb # protoST itself
# or, lower-level:
sudo dpkg -i protost-<version>-Linux.deb && sudo apt-get install -f
```

**Installing on Windows.** Native Windows is not supported: the I/O layer
uses POSIX calls and the runtime uses GCC builtins. Use WSL2 with Ubuntu
24.04 and the Linux packages attached to the GitHub releases (or built as
below):

```bash
wsl --install -d Ubuntu-24.04        # in PowerShell, once
# then, in the Ubuntu shell:
wget https://github.com/numaes/protoCore/releases/download/v2.6.2/protoCore-2.6.2-Linux.deb
wget https://github.com/gamarino/protoST/releases/download/v0.5.0/protost-0.5.0-Linux.deb
sudo apt install ./protoCore-2.6.2-Linux.deb ./protost-0.5.0-Linux.deb
```

**macOS — not built or verified.** `CMakeLists.txt` configures a `.dmg`
(DragNDrop) for macOS, and an NSIS installer and a `.zip` for Windows, but
none has ever been built: there is no macOS or Windows host in this project.
Since 0.5.0 the I/O layer uses Linux-only calls (`pipe2`, `accept4`), so a
macOS build is not expected to compile unchanged. See
[`docs/INSTALLATION.md`](docs/INSTALLATION.md).

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
cpack -G NSIS        # installer .exe (Windows, needs NSIS; never built)
```

The generators are selected per platform in `CMakeLists.txt` (Linux: DEB, RPM,
TGZ; macOS: DragNDrop; Windows: NSIS, ZIP); `cpack` with no `-G` builds every
generator enabled for the host OS. The `.deb` and `.tar.gz` packages have been
verified on Linux; the macOS and Windows generators are configured but have
never been run.

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
