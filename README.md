# protoST

protoST is a runtime for Smalltalk-syntax programs built on protoCore: programs are plain files, objects are actors that run on native threads, and the runtime can live next to other protoCore runtimes in one process. It is a demonstrator of protoCore and a base for digital twins, not an implementation of Smalltalk-80 and not a replacement for an image-based environment.

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

`protost accounts.st` runs the file and prints the value of its last statement.
A blank line ends a method body; there is no image and no browser: the program
is the file.

## Objects become actors

Any object becomes an actor with `asActor`. Every message sent to an actor is
queued in its mailbox and answers a `Future` at once; the actor runs one
message at a time, on a pool of native worker threads shared by all actors, so
its instance variables need no locks.

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
a mutable object is an atomic reference to an immutable snapshot, and every
collection is an immutable value shared by structure. A message carries a
reference, not a copy, and what the receiver reads cannot change under it.

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

PERFORMANCE_PLACEHOLDER

## Status

Version 0.4.0. 977 `ctest` cases (conformance programs, unit tests, CLI tests,
the examples and every documented example with a stated result) pass. The
known deviations from Smalltalk-80 and the open bugs are tracked in
[`docs/STATUS.md`](docs/STATUS.md); runtime hard edges in
[`KNOWN_ISSUES.md`](KNOWN_ISSUES.md); what changed in
[`CHANGELOG.md`](CHANGELOG.md).

## Getting started

protoST depends on [protoCore](https://github.com/numaes/protoCore), which must
be built first. By default the build looks for a protoCore checkout next to
protoST (`../protoCore`) and uses the first of these directories that holds
`libprotoCore`: `build_release/`, then `build/`, then `build_check/`. The
choice is cached in `PROTOCORE_LIBRARY` on the first configure; pass
`-DPROTOCORE_LIBRARY=<path to libprotoCore.so>` to pick a library explicitly
(or remove that cache entry to search again), or
`-DPROTO_CORE_PREFIX=<prefix>` to use an installed protoCore.

```bash
git clone https://github.com/numaes/protoCore.git
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

**macOS** — open the `.dmg` and drag `protoST` to `Applications`.

**Windows** — run the NSIS installer (`protost-<version>-win64.exe`) and
follow the wizard, or unzip the portable `.zip`.

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
cpack -G DragNDrop   # .dmg (macOS)
cpack -G NSIS        # installer .exe (Windows, needs NSIS)
```

The generators are selected per platform in `CMakeLists.txt` (Linux: DEB, RPM,
TGZ; macOS: DragNDrop; Windows: NSIS, ZIP); `cpack` with no `-G` builds every
generator enabled for the host OS. The `.deb` and `.tar.gz` packages have been
verified on Linux.

## Documentation

| Document | What it covers |
|---|---|
| [docs/TUTORIAL.md](docs/TUTORIAL.md) | The dual-audience tutorial — teaches protoST from the ground up for Python/JavaScript developers, and catalogues every departure from Smalltalk-80 for Smalltalk programmers. 14 chapters under `docs/tutorial/`. |
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
