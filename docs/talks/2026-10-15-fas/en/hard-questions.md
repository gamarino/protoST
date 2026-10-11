# Hard questions — honest answers

Each answer says what is verified and where. If the answer is "we do not
know" or "it does not work yet", it is said that way.

**Why not Pharo?**
Pharo is the live Smalltalk environment and protoST does not compete with it.
protoST explores something else: running Smalltalk code from files, on a core
(protoCore) with native threads with no global lock and other languages
alongside. If what you need is the live environment, use Pharo.

**Where is the image? And the IDE?**
There is no image, by design: the program is the file. There is a REPL
(`protost -i`), a command-line debugger (`protost -d`) and a DAP adapter for
VS Code (`protost --dap`, `docs/debugging.md`). There is no class browser.

**Performance compared with Pharo?**
We did not measure against Pharo and we will not give a figure we did not
measure. We measured against CPython 3.14 with the same algorithm, the same N
and the same verified result (`benchmarks/reports/2026-10-11-protocore-2.14.5.md`: synthetic benchmarks, on a laptop with
a 6-core AMD Ryzen 5 5500U, `main` with protoCore 2.14.5):

- startup: 32 ms (CPython: 36 ms);
- whole-process time: from 1.0× to 26× that of CPython, geometric mean 3.5×;
  with small N, startup weighs heavily in each time;
- subtracting each runtime's startup, the work itself takes about 5×
  (integer loop), 30× (`fib`, recursive method dispatch) and 80×
  (exceptions) as long as in CPython; the geometric mean is between 16× and
  27× depending on the run (with small N the subtraction is sensitive to
  noise);
- in parallel, twelve actors run 2.5–2.8× faster with the pool than with one
  worker, and `saturation_big` reaches about 2× with four workers; five or six
  do not improve it.

protoCore's October collector work (2.10 → 2.14.5) did not change these
figures: protoST executes the same instructions as 0.5.0, and its programs
collect little under a 640 MB ceiling. Allocation-heavy actor programs are
4–6 % slower on one worker; we do not know why yet, and the report says so.

In other words: on one thread protoST is considerably slower than CPython.
What we show is that the same code uses several cores, today with that
ceiling.

**What is the GC like?**
protoCore's: concurrent, with its own thread. protoST sets a heap ceiling
(640 MB by default, configurable with `PROTOCORE_HEAP_LIMIT_CELLS`) and, if it
is exhausted, exits with a clear message instead of taking over the machine.
Since protoCore 2.12 a cycle starts a little before that ceiling is reached
(pacing), and the sweep, the most expensive phase, walks several chains at
once and can add helper threads (2.14). It has stop-the-world phases:
protoCore measured them below 0.2 ms in its own tests; we have not measured
them in protoST, so we give no figure of our own. protoCore also offers an
adaptive heap-size controller; protoST does not enable it, because under it
`fib.st` took about three times as long (protoST's start-up live set sits
just below its initial limit). It is written down as a decision, with the
measurement, in `KNOWN_ISSUES.md` (K5).

**Why prototypes?**
protoCore is a prototype model; protoST classes are named prototype objects.
To the Smalltalk programmer they look like classes (`subclass:`, class
methods, class variables and class-instance variables). The differences this
produces are in chapter 14.

**What does the protoCore ecosystem compare with? What are its references?**
Its references are conceptual, not performance ones, because protoCore does
not optimise the current way of working: it tries to change the foundation. It
is a bridge between functional programming and mutable languages.

- **Rich Hickey and Clojure** are the closest reference. Their model of
  identity, state and value says that an identity points to a succession of
  immutable values. Clojure offers it *inside* a functional language, with
  explicit `atom`s and `ref`s. protoCore puts it *beneath* mutable languages:
  a mutable object in Python, JavaScript or Smalltalk is, by construction, an
  atomic reference to an immutable snapshot, and the programmer does not
  change how they write. The code stays imperative; the semantics underneath
  are those of values.
- **Persistent data structures** (Okasaki; Bagwell's trees) make it cheap for
  a "modification" to create a new version that shares almost everything with
  the old one.
- **Self and Lieberman's prototypes** give an object model with no imposed
  classes, which lets very different languages share objects.
- **Erlang/BEAM, as a contrast,** reaches safe concurrency the opposite way:
  it shares nothing and copies messages. protoCore shares everything without
  copying, because what is shared is immutable.

The result is ordinary imperative code with safe concurrency, native threads
without a global lock, and objects that cross from one language to another in
the same process, by construction rather than by discipline.

**What about GraalVM? And the JVM?**
They are not comparable in what matters. GraalVM is a general-purpose platform
optimised for existing languages and built entirely on the JVM's object model.
It does not propose another way of working: it optimises the current one to
the extreme, and does it very well. protoCore changes the foundation. If the
question is speed, they win without discussion: protoCore has no JIT and does
not compete on single-thread performance (see the performance question).

**Who uses it?**
Nobody in production. It is an experiment and a protoCore demonstrator; we
are looking for people to try it with real programs.

**What is missing?**
`thisContext`, `outer` (today it behaves like `pass`), the full metaclass
hierarchy, `become:`, image and browser (by design), `Process`, `Semaphore`,
`Delay` and `fork` (concurrency is actors), classes such as
`IdentityDictionary`, `ByteArray` or `ScaledDecimal`, and installed runtimes
loading each other (the core allows it since protoCore 2.6: embedded in one
process, protoScala reads and writes protoST objects without copying them;
`docs/INTEROP.md` §0). What we found is in chapter 14 of the tutorial and in
`docs/STATUS.md`; it is not a guarantee that there are no other differences.

**Can it talk to the outside world?**
Since 0.5.0, yes: files and directories (`'data.csv' asFileReference`),
standard input and output (`Stdio stdin nextLine`: a script is a Unix
filter), arguments and environment variables, exit codes, other programs
(`OSProcess`), TCP and UDP sockets, TLS, and HTTP client (https too) and
server, with one actor per connection. An I/O wait inside an actor does not
hold up the GC, and the worker pool grows while it lasts, so many actors
waiting for replies do not starve the ones that produce them. HTTP/2,
WebSockets and a TLS server are missing. Demo 4 shows it.
The operating-system layer is a shared library, protoIO, which protoScala and
protoClojure use too: the same input and output exists, under each language's
own names, in the three runtimes.

**Does it run on Windows?**
The published package (0.5.0) is for Linux; on Windows it runs under WSL2
with Ubuntu 24.04, installing the same `.deb` packages. The current sources
build natively with Visual Studio 2022, and continuous integration runs the
whole suite on Windows (except two tests that depend on POSIX tools: Ctrl-C
sent with `kill` and an example in `sh` syntax; `docs/INSTALLATION.md`, "Windows (MSVC)"); there is no published
Windows package yet.

**Is it stable? What open bugs does it have?**
0.5.0 passed its 1068 `ctest` cases; the current sources (main, with
protoCore 2.14.5) pass 1093, on Linux, macOS and Windows: conformance programs,
unit tests, the examples, command-line tests and the documentation examples.
While preparing this talk, an adversarial audit of about 1,100 programs, in two rounds, found
silent wrong results and hangs; every fix for a wrong result, a crash or a
hang has its regression test. The intermittent hang S19 was caught with the
process still alive and turned out to be a lost wakeup in libstdc++ 13's
`std::counting_semaphore`; it was replaced. What is open is in
`docs/STATUS.md`.

**Can actors block each other?**
An actor that does `wait` inside a method does not handle other messages
until it receives the answer. If two actors wait on each other nobody makes
progress: protoST detects it and signals an `Error` ("deadlock") instead of
hanging. Inside actors it is better to chain with `thenDo:` or `whenAll:`.

**License?**
MIT.

**How do I contribute?**
Write programs the way you would write them and report what surprises you
(issues in the repository). `docs/ROADMAP.md` lists the open work.
