# protoST — embedder conformance

The normative rule table is `protoCore/docs/EMBEDDER-CONFORMANCE.md`.

- Static ratchet: `conformance-allow.txt` — **clean, exit 0**
- Runtime adaptor: **NOT WRITTEN IN P4.** See "What is not covered" below.
- Run the static half:
  `python3 ../protoCore/scripts/conformance/check_static.py --repo .`

## What is not covered, and why that matters more than what is

**protoST has no `proto::conformance::Host` yet, so none of the twelve runtime
cases has been run against it.** Rules 1, 2, 2b, 3, 5, 8, 9b, 9c and 11 are
therefore **UNVERIFIED for protoST by this suite** — not passed. The cases exist,
the Catch2 adapter that would wire them up exists
(`protoCore/headers/protoCoreConformanceCatch2.h`), and the work that remains is
one `Host` implementation over `STRuntime` + `Parser` + `Compiler`.

That gap is worth stating loudly, because protoST is the runtime this whole phase
came from: **S15** was protoST reclaiming 0 cells of 2,748,398 across its entire
history with **848** tests green — the suite size when S15 was measured
(`docs/STATUS.md`, "Test suite"; the 833 this paragraph used to quote was the
suite's size at the earlier S13 fix) — and `MailboxCursor::adopt` was a second bug
of the same class that those same 848 passing tests could not reach because the
first was still present. The suite that would have caught both does not yet run here.

What IS known about protoST, by reading rather than by measurement:

- `ProtoContext::safepoint()` is called at five sites including the loop
  back-edge (`src/runtime/ExecutionEngine.cpp:1799`), and there is a documented
  kill switch, `PROTOST_NO_GC_SAFEPOINT=1` — which is a **ready-made negative
  control** for rule 1 and the first thing a protoST `Host` should exploit.
- Both blocking joins go through `ProtoThread::join(ctx)` and both are wrapped in
  `UnmanagedScope` (`src/runtime/STRuntime.cpp:716`, `src/dap/DapServer.cpp:717`).
  The comment at `STRuntime.cpp:708-715` even anticipated the kernel fix: *"It
  nests harmlessly if ProtoThread::join opens one too."* As of protoCore 2.3 it
  does, and those guards are now redundant-but-harmless.
- Zero raw `std::thread` in `src/`: both thread kinds are created through
  `ProtoSpace::newThread`, and the worker's idle wait is inside an
  `UnmanagedScope` (`STRuntime.cpp:869-874`) — the shape protoClojure once got
  wrong and protoST got right.
- `Bootstrap::Symbols` (32 cached symbol pointers) is a **per-space member of
  `STRuntime::Impl`, not a file-scope static** (`src/runtime/Bootstrap.h:176`), so
  rule 9a's shape (a) does not apply. Its own comment records that this was
  deliberate, as deviation D2.
- `ProtoTuple` is never constructed: the only hit is a read in
  `ValueFormat.cpp:70`.

## Static check — 2026-09-25

**0 unjustified findings**, 2 justified, 1 known gap, 2 informational.

The three `fromExternalPointer` sites all pass `nullptr`, and all three are
legitimate: each wraps a pointer whose lifetime belongs to C++ and which protoCore
must not free — `this` (the `STRuntime`), `this` (the `DapServer`), and a
`BytecodeModule*` owned by the compiler's module vector. A finalizer on any of
them would be a double free. Justified individually in `conformance-allow.txt`.

**A known gap, recorded rather than papered over:** the third of those sites
(`src/runtime/ExecutionEngine.cpp:2452`) is **not seen by the checker at all**,
because the call is split across physical lines and the finalizer check declines
to guess at a continuation rather than risk reporting an argument it did not
read. It is unchecked, not checked-and-cleared.

## Harness hazard S18

`tests/unit/test_debugger.cpp`'s first case omits `setInputStream` where its three
siblings supply it, so it falls through to the real `std::cin`
(`src/debugger/DebuggerRuntime.cpp:75`) and blocks for about fourteen minutes.
**Every conformance invocation closes stdin** (`< /dev/null`). Fixing the test is
protoST's, not P4's — but note that a suite whose runner can hang makes rules 2,
2b, 8 and 11 undiagnosable, because a hang is their signature. The closed stdin
is not optional.

## Judgement items

**C3, C5 and C7 are unanswered**, and they must be answered by whoever writes the
`Host`, not before: C3's mechanised half is `gc.host_stress` under ASan and it has
not been run; C5 requires choosing which of protoST's sequences (`Array`,
`OrderedCollection`) is measured and saying why; C7 must answer for the three null
finalizers above and for whether protoST keeps any external byte total (it does
allocate outside protoCore — nlohmann/json in the DAP, `BytecodeModule` vectors).

## Informational

- protoST never calls `ProtoSpace::setHeapLimits` and installs no
  `outOfMemoryCallback`; it uses the `PROTOCORE_HEAP_LIMIT_CELLS` environment
  variable from its CLI fixtures instead. So no collection cycle starts by itself
  in an ordinary run, and rule 8 is unreachable as configured.
