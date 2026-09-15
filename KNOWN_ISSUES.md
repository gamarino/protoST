# protoST — Known Issues

This file records the hard edges of the runtime that are **not** language
design choices, with their bounds, so you know when each one applies. It was
first written for the 0.1.0 release and is kept current.

For *language-level* gaps — features not yet implemented, intentional
departures from Smalltalk-80, and open bugs — see
[`docs/STATUS.md`](docs/STATUS.md).

---

## K1 — One `STRuntime` per process (open)

**What.** Constructing more than one `STRuntime` in a single OS process is not
supported. The `protost` CLI always constructs exactly one, so this does not
affect normal use.

**Why.** Some process-global state — protoCore's UMD module provider and its
module cache — is not isolated per `ProtoSpace`. A second runtime can
mis-resolve `Import from:` against the first runtime's (already freed) space.

**Bounds.** Affects only an embedder that builds multiple runtimes in one
process. The larger half of this hazard — function-local `static` caches
holding per-`ProtoSpace` interned symbols — was fixed in 0.1.0. The residual
is the module-provider / cache layer. The unit-test binary builds many
runtimes and is therefore run one test per process by `ctest`.

**Status.** Open — to be closed by making the UMD module provider and cache
per-runtime. See deviation D2 in `docs/STATUS.md`.

## K3 — No `%` string formatting (open)

**What.** protoCore's `ProtoString` does not implement `%`-style string
formatting, and protoST adds none.

**Bounds.** Narrow. Build strings with `,` concatenation and the conversion
selectors (`printString`, `asString`, …) instead.

**Status.** Open; a small unimplemented feature in protoCore.

---

## Resolved

### K2 — Very large strings / ropes (resolved)

At 0.1.0, a very large protoCore rope (`ProtoString`) — on the order of ~1M
nodes, typically built by a long chain of `,` concatenations — could trigger a
garbage-collector segfault or a "Non-tuple object in tuple node slot" error,
and protoCore's two stress tests for it were disabled.

Fixed in protoCore by the concurrent-mark snapshot (`335ef608`, 2026-05-30)
and the chunked-freelist sweep series (`c3dac9de..11e287a1`); the stress tests
were re-enabled in `0ee42acf` (2026-06-13). Those tests,
`SwarmTest.OneMillionConcats` and `SwarmTest.LargeRopeIndexAccess`, are in
protoCore's
[`test/SwarmTests.cpp`](https://github.com/numaes/protoCore/blob/master/test/SwarmTests.cpp).
Build protoST against a protoCore checkout that includes those commits.
