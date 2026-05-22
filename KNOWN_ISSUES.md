# protoST — Known Issues

protoST 0.1.0 is an **initial release**. It is a working runtime — the full
`ctest` suite is green (751/751, each test in its own process) — but it is
young, and this file records its hard edges **honestly**, with their bounds,
so you know exactly when each one bites and when it does not.

For *language-level* gaps — features not yet implemented, and intentional
departures from Smalltalk-80 — see [`docs/STATUS.md`](docs/STATUS.md), its
"Intentional deviations" and "Not yet implemented" sections. Those are design
choices. This file lists the genuine hard edges that are **not** design
choices.

---

## K1 — One `STRuntime` per process

**What.** Constructing more than one `STRuntime` in a single OS process is not
supported. The `protost` CLI always constructs exactly one, so this never
affects normal use.

**Why.** Some process-global state — protoCore's UMD module provider and its
module cache — is not yet isolated per `ProtoSpace`. A second runtime can
mis-resolve `Import from:` against the first runtime's (already freed) space.

**Bounds.** Affects only an embedder that builds multiple runtimes in one
process. The larger half of this hazard — function-local `static` caches
holding per-`ProtoSpace` interned symbols, which dangled for every later
runtime — was **fixed in 0.1.0** (symbols are now resolved fresh per call).
The residual is the module-provider / cache layer only. The unit-test binary
builds many runtimes and is therefore run one-test-per-process by `ctest`;
run directly it shows 3 module-resolution failures, all of this single cause.

**Status.** Tracked — to be closed by making the UMD module provider and cache
per-runtime. See deviation D2 in `docs/STATUS.md`.

## K2 — Very large strings / ropes

**What.** A very large protoCore rope (`ProtoString`) — on the order of ~1M
nodes or multi-megabyte strings, typically built by a long chain of `,`
concatenations — can trigger a garbage-collector segfault, or a
"Non-tuple object in tuple node slot" error while traversing the rope.

**Bounds.** Normal-size string use is unaffected; the threshold is large. The
two protoCore stress tests that exercise it (`SwarmTest.OneMillionConcats` and
`SwarmTest.LargeRopeIndexAccess`) are disabled for this reason.

**Status.** protoCore-side — it requires garbage-collector / rope hardening,
not a surface fix. Tracked in protoCore.

## K3 — No `%` string formatting

**What.** protoCore's `ProtoString` does not implement `%`-style string
formatting.

**Bounds.** Narrow. Build strings with `,` concatenation and the conversion
selectors (`printString`, `asString`, …) instead.

**Status.** A small unimplemented feature; tracked in protoCore.

---

None of K1–K3 affects the shipped configuration — the `protost` CLI, one
runtime, normal-size data. They are recorded here, bounded and tracked, rather
than hidden: an honest initial release names its edges.
