# Two-runtime interop: reproducer

This directory reproduces what happens when protoST and a real second protoCore
runtime, protoScala, share one process. It is **not registered with CTest**: it needs
a protoScala build tree beside this one, and it records limits rather than guarding a
passing behaviour. The results below were measured on 2026-09-29. They are the
evidence behind the interop wording in the top-level `README.md` and in
[`docs/INTEROP.md`](../../docs/INTEROP.md).

## What works: protoScala imports a protoST module

This direction is exercised by protoScala's own test `umd/protost-interop`
(`protoScala/tests/unit/protost_interop.cpp`). CMake builds it only when protoST's
static libraries are found in `../protoST/build_release`. It is not part of protoST's
test suite. The prebuilt binary was run on 2026-09-29 from an empty directory:

```
$ protoScala/build_release/tests/unit/protoscala_protost_interop
[ RUN      ] ProtoSTInterop.AForeignValueIsTheSameObjectOnBothSides
NO-COPY PROOF
  protoScala space   = 0x7ffcbff972c0
  protoST space      = 0x5751cd5598c0
  Counter via Scala  = 0x76a373ba7680  getHash(scalaCtx) = 130444393346688
  Counter via ST     = 0x76a373ba7680  getHash(stCtx)    = 130444393346688
[ RUN      ] ProtoSTInterop.AForeignValueSurvivesACollectionOnBothSides
GC: protoScala's best cycle reclaimed 197723 cells (threshold 20000)
[ RUN      ] ProtoSTInterop.AProtoScalaProgramImportsAProtoSTModule
<object>
[ RUN      ] ProtoSTInterop.AProtoScalaProgramBindsAMemberOfAProtoSTModule
<object>
absent.scala:1:1: error: ImportError: counter_lib has no member named 'NotAClassThisModuleDefines'
[  PASSED  ] 7 tests.
```

A protoScala program's `import st.counter_lib` resolves, `import st.counter_lib.Counter`
binds, and the bound value is the protoST class cell itself. The Scala program can
only print it as `<object>`. The probe below shows why it can do nothing more.

The installed `protoscala` binary cannot do this: it does not link protoST, and
protoST ships no provider plug-in. `import st.counter_lib` there reports
`ImportError: no provider registered for 'st'`.

## What does not work

`repro.sh` builds `two_runtime_probe.cpp`, which links protoST and protoScala into one
executable, and runs three cases. Each compilation is one serial compiler invocation.

```
$ PROTOST_BUILD=... PROTOSCALA_DIR=... PROTOCORE_DIR=... tests/interop/repro.sh /tmp/work
== import, interpreted provider (mathlib.scala, no .so on the path)
PROBE: protoST program failed: module not found: mathlib
== import, compiled provider (so/mathlib.so, no .scala beside it)
PROBE: protoST program failed: doesNotUnderstand: add#2 (receiver class: Number)
== handoff
PROBE: protoScala module 0x7ea48397ec80
PROBE:   .add via protoScala ctx        = <protoCore method>
PROBE:   .add via protoST ctx           = <absent>
PROBE:   .__class_name__ via protoST ctx = 'Number'
PROBE:   symbol 'add' interned: protoScala 0xc8c8c2d01, protoST 0xc8c8c2d01
PROBE: protoST class Counter via provider:st from protoScala ctx = 0x7ea4891f5980
PROBE:   .__class_name__ via protoScala ctx = <absent>
PROBE:   .__class_name__ via protoST ctx    = 'Counter'
PROBE:   .increment via protoScala ctx      = <absent>
PROBE:   .increment via protoST ctx         = 0x7ea4891f6680
PROBE: protoST program failed: doesNotUnderstand: add#2 (receiver class: Number)
```

The cases show four things.

1. **protoST cannot import an interpreted protoScala module.** `provider:scala` finds
   its host from `ctx->space` (`ScalaModuleProvider::tryLoad`). protoST's space is not
   one it owns, so it answers "not mine". This is the defect protoST fixed on its own
   side as S17.

2. **An object owned by one runtime cannot be read through the other runtime's
   context, in either direction.** The symbol for `add` is the same pointer in both
   runtimes, so the key is not the problem. The problem is the state of a mutable
   object. protoCore resolves it through `context->space->mutableRoot`, keyed by the
   object's `mutable_ref` (`protoCore/core/ProtoObject.cpp`). Read through the other
   space's context:
   - the protoScala module's `add` is absent from protoST;
   - the protoST class `Counter` has no `__class_name__` and no `increment` from
     protoScala.

3. **A cross-space read can return another object's data without any error.**
   Through protoST's context, the protoScala module object answers
   `__class_name__ = 'Number'`, which is the name of a protoST class. The likeliest
   explanation is that the two spaces allocate `mutable_ref`s independently, so the
   lookup lands on an unrelated protoST object. The code is consistent with this, but
   it has not been confirmed by printing both refs.

4. **protoST cannot call a protoCore method.** `SEND_CALL`
   (`src/runtime/ExecutionEngine.cpp`) handles a protoST bytecode method and a protoST
   primitive marker, and nothing else. A `proto::ProtoMethod` attribute, which is what
   `protoscalac` exports, falls through to `doesNotUnderstand`. The failure above
   happens earlier, at the attribute read (point 2), but this path would fail even if
   the read succeeded.

## Files

| File | Purpose |
|------|---------|
| `two_runtime_probe.cpp` | Host linking both runtimes. It has two modes: `import` (UMD) and `handoff` (no UMD, object handed over by the host). |
| `repro.sh` | Builds the protoScala module with `protoscalac`, builds the probe, runs the three cases. |
| `mathlib.scala` | protoScala module exporting `add` and `greet`. |
| `counter_lib.st` | protoST module. It is the same fixture protoScala's test uses. |
| `import_scala.st`, `send_to_scala.st` | protoST programs the probe runs. |

## Build notes

- Link protoScala as its shared library, `libprotoScala.so`. With protoScala's object
  files linked statically, the process holds two copies of protoScala's runtime
  state. The generated module's initialiser then fails with
  `no protoScala call context is active`.
- `protoscalac --as-module --module-name mathlib` produced a module with **zero**
  exports on 2026-09-29. `repro.sh` omits those flags, as protoScala's own
  `tests/interop/foreign-call.sh` does.
