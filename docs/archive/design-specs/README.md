# Design specifications (archive)

Historical design documents written during development. They are not maintained and may not match the current code; see the [README](../../../README.md) for current documentation.

- [2026-05-19-protost-design.md](2026-05-19-protost-design.md) — the original technical design specification for protoST (language, VM, actor model, tooling).
- [2026-05-20-closure-capture.md](2026-05-20-closure-capture.md) — closure capture inside methods: arguments, locals, `self` and instance variables seen from blocks.
- [2026-05-20-collections.md](2026-05-20-collections.md) — the collection hierarchy and iteration protocol on protoCore primitives (Track 2).
- [2026-05-20-exceptions.md](2026-05-20-exceptions.md) — the Smalltalk exception protocol (Track 1, slice 2).
- [2026-05-20-non-local-return.md](2026-05-20-non-local-return.md) — non-local return from blocks to their home method (Track 1, slice 1).
- [2026-05-20-stdlib.md](2026-05-20-stdlib.md) — the standard library modules (Track 4).
- [2026-05-21-cross-language-interop.md](2026-05-21-cross-language-interop.md) — consuming objects and modules from other protoCore runtimes through UMD (Track 5).
- [2026-05-21-object-model.md](2026-05-21-object-model.md) — the advanced object model: extensible module classes, multiple inheritance, mixins (Track 3).
- [2026-05-23-hardware-bound-plateau.md](2026-05-23-hardware-bound-plateau.md) — analysis showing the worker-scaling plateau was bound by physical cores, not the runtime.
- [2026-05-23-interpreter-perf-spec.md](2026-05-23-interpreter-perf-spec.md) — interpreter performance plan: threaded-goto dispatch and envelope/Future flattening.
- [2026-05-23-interpreter-perf-stage1-report.md](2026-05-23-interpreter-perf-stage1-report.md) — results of Stage 1 (threaded-goto dispatch).
- [2026-05-23-mt100a-anti-scaling.md](2026-05-23-mt100a-anti-scaling.md) — investigation of multi-actor anti-scaling and per-message allocation cost.
- [2026-05-23-multiproducer-blocker.md](2026-05-23-multiproducer-blocker.md) — the multi-producer benchmark, the cooperative-yield bug it exposed, and the scaling diagnosis.
- [2026-05-23-overnight-final-report.md](2026-05-23-overnight-final-report.md) — summary of a performance investigation into what serialised actor dispatch.
- [2026-05-23-ready-queue-mpmc-spec.md](2026-05-23-ready-queue-mpmc-spec.md) — replacing the ready queue with an intrusive lock-free stack.
- [2026-05-23-saturation-experiment.md](2026-05-23-saturation-experiment.md) — isolating worker-pool drain capacity from producer rate.
- [2026-05-23-stage2-overnight-findings.md](2026-05-23-stage2-overnight-findings.md) — the reverted Stage 2 attempt and the per-actor FIFO ordering problem it hit.
- [2026-05-24-doyielding-design.md](2026-05-24-doyielding-design.md) — design of `doYielding:`, the compiler-desugared yieldable iteration.
- [2026-06-13-protocore-call-syntax.md](2026-06-13-protocore-call-syntax.md) — protoCore-style call-form sends and method declarations with positional and named arguments.
