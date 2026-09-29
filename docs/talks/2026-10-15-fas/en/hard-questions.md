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
and the same verified result (`benchmarks/reports/2026-09-29-release-0.4.0.md`,
on a laptop with a 6-core AMD Ryzen 5 5500U):

- startup: 27 ms (CPython: 29 ms);
- whole-process time: from 1.0× to 23.6× that of CPython, geometric mean
  3.45×; with small N, startup weighs heavily in each time;
- subtracting each runtime's startup, the work itself takes about 6×
  (integer loop), 36× (`fib`, recursive method dispatch) and 64×
  (exceptions) as long as in CPython; geometric mean 20.8×;
- in parallel, the measured ceiling of this version is about 2.1× (four
  workers against one; five or six do not improve it), confirmed by a second
  independent run under lighter load.

In other words: on one thread protoST is considerably slower than CPython.
What we show is that the same code uses several cores, today with that
ceiling.

**What is the GC like?**
protoCore's: concurrent, with its own thread, deferring collection until it
is needed. It has stop-the-world phases; we have not measured how long they
last in protoST, so we do not give a figure. protoST sets a heap ceiling
(640 MB by default, configurable with PROTOCORE_HEAP_LIMIT_CELLS) and, if it
is exhausted, exits with a clear message instead of taking over the machine.

**Why prototypes?**
protoCore is a prototype model; protoST classes are named prototype objects.
To the Smalltalk programmer they look like classes (`subclass:`, class
methods, class variables and class-instance variables). The differences this
produces are in chapter 14.

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

**Does it run on Windows?**
Not natively: the I/O is POSIX. On Windows it runs under WSL2 with Ubuntu
24.04, installing the same `.deb` packages.

**Is it stable? What open bugs does it have?**
0.5.0 passes the 1068 `ctest` cases: conformance programs,
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
