# protoST on protoCore's object-shape model: instance templates and public API only

**Date:** 2026-11-17/18 · **Branch:** `feat/protocore-shapes` (from `main` at
`b7e206a`, 0.12.0) · **protoCore:** master `eced46bd`, `build_release`, unchanged.

## 1. Commits

| Commit | Content |
|---|---|
| `7620c78` | Public API only: the seven hand-copied pointer-tag tests replaced; `src_public_api_only` guard test |
| `e7f2aa1` | Instance templates (`newClone`), closure and Array templates, tests, `instances.st` benchmark, docs |
| `af83215` | `methodHolder`: public-API predicates ordered for the commonest receiver |
| (this commit) | Cell-count test, CHANGELOG "Unreleased", this report |

No fix-up commit was needed for the protoCore change itself: 0.12.0, unchanged,
built against master and passed **1467/1467** (attribute enumeration order and
mutable-cell assumptions did not bite).

Every tested binary was checked with `ldd`: `libprotoCore.so.5 =>
/home/gamarino/Documentos/proyectos/protoCore/build_release/libprotoCore.so.5`.

## 2. Tests (full suite, sequential `ctest`)

| Tree | Result |
|---|---|
| `b7e206a` (0.12.0) on protoCore master | 1467/1467 |
| `7620c78` public API only | 1468/1468 (+ `src_public_api_only`) |
| `e7f2aa1` first run | 1472/1473: `cli_gc_recursion_reclaims` failed (see below), fixed in the same commit |
| `af83215` final | **1473/1473**, the 45 `examples/*` programs included |
| after the cell-count test (test-only change) | unit binary `[templates]`: 6/6 cases pass |

`cli_gc_recursion_reclaims` is a two-direction fixture: with the GC safepoint
hook disabled the run must run out of memory. Arrays went from 6 cells to 3,
so two Arrays per call no longer filled the ceiling. The fixture now allocates
four (both directions verified: completes with the hook, exits 3 "out of
memory" without it). The script records the same adjustment made once before
(0.10.0).

The guard test was shown to work: run on the old tree it reports all seven
sites; on a probe directory holding `#include "proto_internal.h"` and a
`& 0x3F` tag test it fails.

The template tests were written first. With `newInstance` temporarily reverted
to `cls->newChild`, the sharing test fails (each variable write costs 3 cells,
not 1 cell as a replacement does).

## 3. Public API only (maintainer ruling)

The `grep -rnE '0x3F|kTag|POINTER_TAG|proto_internal' src` sites were 15
lines. Eight are UTF-8 masks in `string_prims.cpp` and `STRuntime.cpp` and are
legitimate. The seven real ones were all `(reinterpret_cast<uintptr_t>(p) & 0x3F)`
"is this an object cell" tests:

| Site | Replacement |
|---|---|
| `Bootstrap.cpp` `isMetaclass`, `ownsClassName` | gate dropped: `getOwnAttributeDirect` answers nullptr for non-objects |
| `Bootstrap.cpp` `methodHolder` (every send) | `isSmallInt`, `isStringTagFast`, ungated metaclass probe, then `isCellPointer` split + `isByteBuffer`/`isDouble`/`isInteger`; character test only for immediates |
| `Bootstrap.cpp` `isCharacterValue` | rejects cells with `isCellPointer` (an object, even a `Character` instance, is never a character; no `getPrototype` call) |
| `ExecutionEngine.cpp` actor fast path | `!proto::isSmallInt(recv)` |
| `ExecutionEngine.cpp` user-method probe | ungated `getOwnAttributeDirect(__bc_ptr__)` |
| `STRuntime.cpp` uncaught-exception class walk | gate dropped (`getParents` answers an empty list) |
| `object_prims.cpp` `Object>>class` | `isByteBuffer` directly |

Comments that taught tag values were reworded. A test comment that cited
`proto_internal.h` was reworded too. Tests outside `src/` are not covered by
the guard (`test_instvar_groups.cpp` indexes `getOwnAttributes`' sparse list
by symbol pointer, a test-only convention I left alone).

**Cost:** public header has no positive "is an object" predicate, so after
the metaclass probe a plain object runs out-of-line kind predicates. Measured:
+82 instructions per send before reordering, about +59 after `af83215`
(see §5). I did not add any predicate to protoCore.

## 4. Templates

- **Classes** (`src/runtime/InstanceTemplate.{h,cpp}`). The template is an
  immutable `cls->newChild(false)`. Every `_iv_<name>` of the class and its
  chain (metaclasses excluded) is set to nil through `setAttributes`, then
  `removeAttribute`d. The names stay in the `ProtoShape` and the slots are
  null, so reads fall through to the chain and reflection does not see them,
  as before.
  - `basicNew`/`new`, class-side `Exception signal`/`signal:` and
    `instantiateAs` (user subclasses of built-in collections) call
    `newInstance`, which is `template->newClone(ctx, true)`. Instances stay
    mutable, as protoST's model requires.
  - The template is cached on the class (`__instance_template__`,
    `__instance_template_epoch__`). `declareInstVarNames` advances a
    process-wide epoch, so a re-declared class and its subclasses rebuild.
  - A template whose first parent is not the class is rebuilt. This covers
    `addBehavior:`, which copies own attributes into the rebuilt class.
  - Staleness can only cost speed, never correctness: a missing name is
    appended on first write, and an extra one stays an invisible null slot.
    The parent chain cannot go stale, because protoST never re-parents a
    live class (D21 in STATUS.md now records that any future in-place
    re-parenting must call `noteInstanceLayoutChanged`).
- **Closures** (`Bootstrap::closureTemplate`, 4 slots): `newClosure` is one
  `setAttributes` replacement, so a closure is 1 cell.
- **Arrays** (`Bootstrap::arrayTemplate`, `__data__`): `makeArrayInstance` is
  1 replacement + `clone(true)`, so an Array is 3 cells instead of 6.
- **Tried and discarded:**
  - An immutable `{template, epoch}` record, to read the mutable class only
    once: no gain (4601 vs 4548 instructions per `new`).
  - Dropping the epoch read altogether: saves only 100–220 instructions per
    `new`, not worth eager per-subclass invalidation.

## 5. Benchmarks

Three binaries were run serially with every round in rotated order: 0.12.0
(`b7e206a`), public API only (`7620c78`) and final (`af83215`). Templates
alone (`e7f2aa1`) is shown where measured. Every program verifies its own
result.

**The machine was never quiet.** Other sessions were building and running
simulations, so the load average was 7–27 throughout. I waited 25 minutes for
a window below 3 and none came. **User-space instruction counts are the
load-independent figure.** Cycles and wall times are reported but noisy:
`attr_lookup` shows +16.8% cycles with identical instructions.

### 5.1 Per operation (instructions / cycles per op; N = 300,000 minus N = 0; `perf stat -r 3`)

| Operation | 0.12.0 | public API | + templates | final | final vs 0.12.0 |
|---|---|---|---|---|---|
| send | 1679 / 687 | 1761 / 654 | 1817 / 673 | 1794 / 657 | +6.9% / −4.4% |
| ivar write | 1147 / 524 | 1148 / 506 | 1147 / 434 | 1148 / 423 | 0.0% / −19.3% |
| `Array new:` | 3981 / 1798 | 4099 / 1953 | 3433 / 1483 | 3410 / 1634 | −14.3% / −9.1% |
| `Array at:put:` | 2445 / 1126 | 2546 / 1136 | 2549 / 1180 | 2526 / 1148 | +3.3% / +1.9% |
| `Array at:` | 1638 / 695 | 1739 / 755 | 1742 / 707 | 1719 / 665 | +5.0% / −4.3% |
| `new` (bare) | 4102 / 1847 | 4176 / 1896 | 4548 / 1919 | 4586 / 2016 | +11.8% / +9.2% |
| `new` + 4 ivars | 11812 / 7222 | 11863 / 8213 | 12173 / 9551 | 12048 / 8518 | +2.0% / +17.9% |
| subclass `new` + 8 ivars | 15512 / 11296 | 15952 / 11197 | 16377 / 9984 | 16372 / 11074 | +5.5% / −2.0% |
| 4 ivar reads, 1000 instances | 4256 / 1773 | 4438 / 1752 | 4376 / 1725 | 4330 / 1741 | +1.7% / −1.8% |
| closure + `value:` | 3163 / 1403 | 3164 / 1331 | 2704 / 1120 | 2704 / 1126 | −14.5% / −19.8% |
| `Error signal` + `on:do:` | 92973 / 44965 | 90543 / 46081 | 91570 / 45359 | 91508 / 44753 | −1.6% / −0.5% |

The cycle column is unstable between runs. For `new` + 4 ivars, an earlier
run gave −6% against 0.12.0, and the run above gives +17.9%.

### 5.2 Whole programs (median of 3 rounds of `perf stat -r 3`; M cycles / M instructions)

| Workload | 0.12.0 | public API | final | final vs 0.12.0 (cyc / instr) |
|---|---|---|---|---|
| `micro/instances.st` | 11264 / 14687 | 11467 / 14966 | 10269 / 14898 | −8.8% / +1.4% |
| `micro/vm_ops.st` | 3791 / 8980 | 3920 / 9162 | 3676 / 8905 | −3.0% / −0.8% |
| `comparable/fib.st` | 226 / 445 | 230 / 465 | 214 / 461 | −5.4% / +3.5% |
| `comparable/attr_lookup.st` | 130 / 231 | 129 / 231 | 151 / 230 | +16.8% (noise) / −0.1% |
| `comparable/exception_latency.st` | 1453 / 2704 | 1488 / 2655 | 1433 / 2654 | −1.4% / −1.9% |
| `comparable/list_append.st` | 79 / 110 | 84 / 111 | 81 / 111 | +3.4% / +0.5% |
| `micro/blocks_kindof.st` | 1946 / 3989 | 1984 / 4015 | 1712 / 3676 | −12.0% / −7.8% |

The first full run (load 13–27) agrees in direction: `blocks_kindof` −12.4%
cycles, `instances` −6.3%, `fib` −7.2% for templates vs public API.

### 5.3 Cells (deterministic; `[templates][cells]`)

Making an instance and assigning its variables in one group (an
initializer's write group, shard publication included) takes **4 cells from
the template against 5 as a plain child**, for both 2 and 4 variables.
A closure is 1 cell, down from 4. An Array is 3, down from 6. These last two
follow from protoCore's documented costs; the old `cli_gc_recursion_reclaims`
fixture's loss of its margin is the observable effect.

### 5.4 Regressions and anomalies, honestly

- **Public API: about +59 instructions per send** (+3.5% on `fib`, +5% on `Array at:`).
  There is no public positive object predicate.
- **Class instantiation: about +400 instructions per `new`** compared with
  `newChild`. The template lookup reads two own attributes of the (mutable)
  class and calls `getFirstParent`. This is +2% on `new` + 4 ivars and +11.8%
  on a bare `new`.
  - In exchange, each instance takes one cell fewer and all instances of a
    class share one shape.
  - The read-side gain on this workload is small: 4 reads over 1000
    instances is −1.4% instructions vs public API, because a read still goes
    through `getAttribute`'s per-object snapshot resolution.
  - I kept class templates as the cleaner model the maintainer asked for, and
    because they are neutral to slightly negative overall. This is the call I
    am least sure of. The numbers to revisit are above.
- **Send to a receiver with no assigned variable: +56 instructions.** The
  receiver now has its class's shape to probe before reaching the class.
  Under 0.12.0 an instance that had assigned any variable probed its own
  shape anyway.
- **`Array at:put:` inside `vm_ops` is +30–50% wall** (both runs).
  - It is not the operation itself: isolated it is +3% instructions and +2%
    cycles.
  - Run first in the program, it is on par (478–586 vs 491–517 ns).
  - Under a fixed `PROTOCORE_HEAP_LIMIT_CELLS=100000000` it is +5–10%.
  - So it is a collector-phase effect: smaller Arrays move a collection under
    adaptive heap control into that loop. I did not chase it further.
- Pre-existing, not introduced here: `#ab class` answers `String` (a short
  symbol is an inline value); `#abcdefghij class` answers `Symbol`. 0.12.0
  does the same.

## 6. What stays `newChild`, and why

About 95 call sites remain:

- **Class and prototype construction.** `Bootstrap.cpp` (46),
  `makeSubclass`/`__subclassNamed:`/mixins/`addBehavior:`/metaclasses in
  `object_prims.cpp`, module wrappers, and the template itself. A class must
  be a child (its own parent link), never a sibling clone.
- **The `newChild` selector.** It is a prototype operation on any object, and
  user code uses it (`Counter newChild`).
- **Objects with a varying parent or a dynamic name set.** De-optimisation
  scope dictionaries and boxes (parent is the enclosing captured dict),
  module-level captured dicts, registries.
- **Actor and future machinery.** Actor proxies, futures, resume tasks and
  timer tasks are left as they are. Their protocols CAS against *absent*
  attributes (`setAttributeIfEqual(expected = nullptr)`, "`__sched__` MUST
  exist as 0"), and a template's null slots would need a separate audit of
  every such site. The actor message envelope (`objectProto->newChild(false)`
  plus 3 attributes, one per actor send) is the strongest next candidate.
- **Cold or rare paths.** Runtime-signalled exceptions (MNU, ZeroDivide,
  protoCore's error factory), Interval, Set/Dictionary species results, and
  the Arrays built from lists in `io_prims`, `math_prims` and `string_prims`.
  The last three could route through `makeArrayInstance`, a small follow-up.

## 7. protoCore findings (reported, not changed)

1. **SOVERSION/version unchanged across an object-layout change: confirmed.**
   - Master builds `libprotoCore.so.3.6.0` with SONAME `libprotoCore.so.5`,
     identical to the v3.6.0 release. In `protoCore/build_release` the master
     build overwrote the release's `libprotoCore.so.3.6.0`: two different
     libraries carry one file name.
   - A binary built against v3.6.0 (`worktrees/rel360/protoST`, protoST
     0.9.1, RUNPATH to a v3.6.0 build) loads master's library silently under
     `LD_LIBRARY_PATH`. `ldd` resolves it, and the sampled programs (`fib.st`,
     a class with an instance variable) ran correctly.
   - That works only because the *public* header changed additively (only
     `newClone` added; `git diff v3.6.0 eced46bd -- headers/protoCore.h`).
     `proto_internal.h` changed by 172+/38− lines (new cell layouts), so any
     consumer that inlines internal layouts would silently misbehave.
   - In the other direction, a master-built binary on a v3.6.0 library fails
     loudly at start-up (`undefined symbol: ProtoObject::newClone`, BIND_NOW).
   - `find_package(protoCore 3.6)` cannot tell them apart; protoST's
     developer-tree probe now checks for `newClone(`.
   - Suggestion for the maintainer: release this as 3.7.0 (an additive public
     API). Bump SOVERSION if any consumer still includes `proto_internal.h`.
2. **No public way to observe an object's shape.** The template tests can only
   infer shape sharing from cell costs (replacement vs append). A read-only
   predicate such as "do these two objects share their attribute layout"
   would make embedder tests direct. This is a gap, not a defect.
3. **No public positive object predicate.** That is what the public-API
   `methodHolder` pays for (§5.4). `isCellPointer` is true for every cell
   kind. This is recorded only, since the ruling was public API as it stands.
4. No defect found. `newClone`, `setAttributes`, `removeAttribute` and
   `getFirstParent` behaved as documented. Null slots fall through to the
   chain, and `processOwnAttributes` skips them.

## 8. Environment notes

- The worktree needs `../protoCore` and `../protoIO` siblings. I created them
  as symlinks in `worktrees/protost-shapes/`, and configured with
  `-DCMAKE_DISABLE_FIND_PACKAGE_protoCore=ON
  -DPROTOCORE_LIBRARY=.../protoCore/build_release/libprotoCore.so`, as
  protoST's own `build_release` does. Otherwise `find_package` picks the
  dpkg-installed protoCore 3.5.0.
- Scratch data lives in `.superpowers/shapes/` (excluded through the
  repository's `.git/info/exclude`, not committed): the measured binaries,
  the harnesses (`bench.py`: whole programs, rotated rounds, `perf stat -r 3`;
  `ops.py`: per-operation instructions as N = 300,000 minus N = 0) and the
  raw data (`bench1.json`, `bench2.json`, `ops*.json`).
