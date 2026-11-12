# Changelog

All notable changes to protoST are recorded here. The living, item-by-item
state of the language is tracked in [`docs/STATUS.md`](docs/STATUS.md).

## Unreleased

## 0.10.0 — protoBaseST groundwork (unreleased)

**Requires protoIO 0.5.0** (or a later 0.5.x; a 0.4.x checkout is refused at
configure time). The planned floor was 0.4.1; protoIO numbers new API as a
minor release, so the two services arrive in 0.5.0.

### Added

- **`ByteArray randomBytes: n` and `Time utcMicroseconds`.**
  `randomBytes:` answers `n` bytes from the operating system's
  cryptographically secure generator (protoIO's `random::bytes`: `getrandom`
  on Linux, `arc4random_buf` on macOS and the BSDs, `BCryptGenRandom` on
  Windows), as a frozen `ByteArray` like every other, so it is shared without
  a lock. `n` is a SmallInteger from 0 to 1048576 (1 MiB); a negative or
  larger size, a LargeInteger or a non-Integer signals `InvalidArgument`. The
  call runs outside the collector's quorum (`unmanaged`), since `getrandom`
  may wait for the kernel pool at boot. `Time utcMicroseconds` answers the
  wall clock as microseconds since 1970-01-01T00:00Z, a SmallInteger
  (protoIO's `clock::utcMicroseconds`); `Time microsecondClockValue` stays
  monotonic, and the wall clock has its own name so that neither reading is
  mistaken for the other (Pharo's `microsecondClockValue` is a wall clock).
  `DateTime nowUTC` (`lib/calendar.st`) is built on it, and
  `13-stdlib/calendar-now-utc.st` now runs instead of skipping. Docs:
  LANGUAGE.md §12.12.1 and the new §12.15 (clocks); tutorial §9.7 and §15.11.
  Tests: `15-binary/random_bytes.st`, `random_bytes_bounds.st`,
  `random_bytes_sizes.st`, `random_bytes_actors.st` (four actors drawing at
  once; in the `tsan` label), `12-builtins/time_utc_microseconds.st` (bounded
  by two wall-clock readings taken around it, never by elapsed time). The
  version banner in the REPL transcripts of tutorial chapters 1 and 12 and
  the `User-Agent` in `14-io/http_request_shape.st` read 0.10.0.
- **`lib/uuid.st`: `UUID` values (RFC 4122).** `Import from: 'uuid'` binds
  `UUID`: `fromString:` (32 hexadecimal digits or the 36-character form, in
  either case), `fromBytes:` (16 bytes in RFC 4122 order) and `new`, a
  version-4 UUID from `ByteArray randomBytes: 16` with the version and
  variant bits set; `bytes`, `hex`, `printString` (the 36-character
  lowercase form) and `asInteger` (the unsigned big-endian value). `=`,
  `hash` and `<`, `<=`, `>`, `>=` are Python's `uuid.UUID` (`hash` is
  `asInteger hash`, Python's `hash(uuid)`), which makes `UUID` the value of
  ProtoBase's `@uuid`. Immutable, pure protoST. Docs: LANGUAGE.md §12.16,
  tutorial §9.10, `lib/README.md`. Tests: `13-stdlib/uuid-parse-print.st`,
  `uuid-compare.st`, `uuid-v4.st`.
- **Python's numeric hash and `raisedTo:modulo:` (R3).** `hash` of an
  `Integer`, `LargeInteger`, `Float`, `Fraction` and `Decimal` is now
  CPython's numeric hash (modulo 2^61 - 1, Python's "Hashing of numeric
  types"): equal values of any kind hash alike, and to the value `hash()`
  answers in Python, which ProtoBase relies on. The values change: `0.5 hash`
  was a hash of the double's bits and is now 2^60, `-1 hash` is `-2`, and a
  hash can be negative or reach 2^61 - 2 (the hashed collections take any
  Integer a `hash` method answers; a user `hash` outside 0..2^53 used to share
  one slot). `Decimal >> hash` no longer builds the exact value, so a huge
  exponent hashes at once (STATUS D49); a signaling NaN cannot be hashed, and
  a NaN hashes by identity. `b raisedTo: e modulo: m` is Python's
  `pow(b, e, m)`: modular exponentiation of Integers, a negative exponent
  through the modular inverse (`ArithmeticError` when there is none),
  `ZeroDivide` for `m = 0`, the result with the sign of `m`. Docs:
  LANGUAGE.md §12.2 and §12.13. Tests: `12-builtins/numeric-hash-cpython.st`
  (55 values, each against python3.14's `hash()`),
  `numeric-hash-across-kinds.st`, `number-raised-to-modulo.st` (against
  `pow`). Hashed collections fold the bits above 54 of a key's hash into the
  slot word protoCore keys a slot by (a SmallInteger's slot is unchanged), and
  `Point`, `Association`, `SequenceableCollection` and `MessagePackMap`
  combine hashes modulo 2^61 - 1 with the multiplier 1000003 instead of
  reducing modulo 2^30: a Float's hash keeps its fraction bits high, and
  without both changes `i / 64.0` keys shared one slot per 64 keys and a grid
  of `i / 4.0 @ (j / 4.0)` Points a slot per 16. The combining step is one
  primitive, `a __hashMix: b` (`(a hash * 1000003 + b hash) mod (2^61 - 1)`
  on machine words), so no LargeInteger is built (measured against 0.9.0's
  `4b47115`, `perf stat` user-space: Fraction keys 4.8x fewer instructions,
  Point keys 24 % fewer instructions and 22 % fewer cycles, Integer keys 11 %
  fewer instructions and 21 % fewer cycles, Float keys 12 % more
  instructions and the same cycles). `Fraction >> hash` and
  `raisedTo:modulo:` run on machine words when the operands fit one. Test:
  `12-builtins/composite-hash-values.st` (Point, Association, Array and
  nested values, each against the Python expression it mirrors).
  Tests: `R3: keys differing in high hash bits take distinct slots` (slot
  count per workload, `tests/unit/test_hashed_collections.cpp`),
  `12-builtins/class-objects-as-hashed-keys.st`.
- **`Decimal`, an exact decimal value** (`lib/decimal.st`, `Import from:
  'decimal'`): Python's decimal model (sign, coefficient, exponent; Infinity,
  NaN and sNaN with payloads) with its text in both directions,
  `fromString:` for `Decimal(str)` and `printString` for `str(Decimal)`, which
  is ProtoBase's `@dec` text. `asExactFraction`, exact comparison with any
  `Number`, in both directions (`(Decimal fromString: '1.00') = 1` and
  `1 = (Decimal fromString: '1.00')` are true, `= 0.1` is false),
  `hash` consistent with `=`, and `sameRepresentationAs:` (`1.0` and `1.00`
  differ). No arithmetic, context or rounding (STATUS D49). Written in
  protoST; no new primitive, so it runs on the 0.9.0 binary. Docs:
  LANGUAGE.md §12.13, tutorial §9.8. Tests:
  `tests/conformance/13-stdlib/decimal-*` (8 files).
- **Calendar values with fixed offsets** (`lib/calendar.st`, `Import from:
  'calendar'`): `CalendarDate` (proleptic Gregorian, years 1-9999, `ordinal`
  and `fromOrdinal:` as Python's `toordinal`), `TimeOfDay`, `DateTime` (naive,
  at a `ZoneOffset`, or the instant form in a `ZoneName`: the fields are the
  UTC instant), `TimeSpan` (normalised as `timedelta`), `ZoneOffset`,
  `ZoneName` and the error `TimeZoneUnavailable`. Two epochs, each with its own
  selectors: `localOrdinalMicroseconds`, `utcOrdinalMicroseconds` and
  `utcOrdinalMicroseconds:zone:` count from 0001-01-01 (the format's epoch),
  `unixMicroseconds` and `fromUnixMicroseconds:zone:` from 1970, with
  `DateTime unixEpochOrdinalMicroseconds` between them; `DateTime nowUTC`
  (needs `Time utcMicroseconds`, 0.10.0). Comparison and `hash` as Python's
  `datetime` (aware values by instant, naive never `=` to aware, ordering them
  refused); `printString` as `str()`. No IANA database: a wall clock in a
  named zone signals `TimeZoneUnavailable` (STATUS D50). Written in protoST;
  runs on the 0.9.0 binary. Docs: LANGUAGE.md §12.14, tutorial §9.9. Tests:
  `tests/conformance/13-stdlib/calendar-*` (9 files).
- **`Fraction` defers to a non-Number that adapts** (`lib/kernel/fraction.st`):
  `=`, `<` (and so `>`, `<=`, `>=`), `+`, `-`, `*`, `/`, and `Integer >> /`,
  send `adaptToNumber:andSend:` to an argument that is not a Number but
  understands it, as the numeric primitives already do. This is what makes
  `(5/4) = aDecimal` answer `true`; numbers and Fractions are unaffected.

### Fixed

- **`Exception >> outer` is Pharo's `outer` (D7).** It behaved as `pass`:
  when the enclosing handler resumed, the protected block continued at the
  `signal` and the inner handler never continued. `outer` now evaluates the
  enclosing handler in place, on top of the handler that sends it; if that
  handler resumes, `outer` answers the resumption value inside the inner
  handler, which continues: `[[Warning signal: 'w'. 'body'] on: Warning do:
  [:e | e outer printNl. 'inner continued']] on: Warning do: [:e | e resume:
  5]` prints `5` and answers `'inner continued'` (it answered `'body'`). If
  the enclosing handler returns, retries or falls off its end, the unwind
  passes through the inner handler. With no enclosing handler a resumable
  exception's default action value is answered (`nil` for a `Warning`); an
  `Error`'s default action ends the activation. For a non-resumable
  exception `outer` is `pass`, as ANSI states. The pattern of protoBaseST's
  `newTransactionDo:` (a catch-all handler that sets a flag, asks `outer`,
  clears it and resumes) now leaves the flag set only when the enclosing
  handler unwinds. Handlers stay per thread: `outer` never crosses an actor
  message. LANGUAGE.md §8.4; tutorial §14.5. Tests: 15 new programs in
  `08-exceptions/` (`outer-*.st`, `pass-runs-enclosing-handler-before-
  unwinding.st`, `nesting-limit-in-outer-resuming-chain.st`).
- **`pass` runs the enclosing handler before unwinding the passing one**, as
  Pharo's `Exception>>pass` does. It unwound the passing handler first, so
  that handler's `ensure:` blocks ran before the enclosing handler instead of
  after it. A resumption still resumes the `signal`, or the `outer` an inner
  handler is waiting in (Pharo's `outerContext`).
- **A class's `isResumable` override is honoured (D54); `resume:` of a
  non-resumable exception signals `IllegalResumeAttempt`.** `resume:` and
  the default action read only the class-derived marker, so
  `MyException >> isResumable ^ false` (the ANSI / Pharo way to declare a
  non-resumable exception) answered false and could still be resumed, and an
  unhandled one resumed with nil. Both now ask the method. A `resume:` that
  is not allowed signals the new kernel class `IllegalResumeAttempt` inside
  the handler, as Pharo does: an `Exception` (not an `Error`), itself not
  resumable, with the text it always had (`cannot resume a non-resumable
  exception`). Through `outer`, an enclosing handler's illegal `resume:` now
  unwinds through the inner handler instead of answering it (protoBaseST's
  `newTransactionDo:` aborts). Tests:
  `08-exceptions/resume-honours-isresumable-override.st`,
  `unhandled-non-resumable-user-exception-aborts.st`; the umd tests catch
  `IllegalResumeAttempt`.
- **`retryUsing:` is Pharo's (D55).** It evaluated the replacement block
  inside the handler, with the handler disabled, and returned its value from
  the `on:do:`, so an exception the replacement signalled went to an
  enclosing handler. The replacement now becomes the protected block of the
  handler's `on:do:` and runs under the same handlers
  (`handlerContext restartWithNewReceiver:`); a later `retry` runs it again.
  Test: `08-exceptions/retry-using-replaces-protected-block.st`.
- **Another actor's message run while an actor waits no longer sees the
  waiting actor's running handlers (S36).** While a waiting actor's thread
  ran another actor's message (`wait` nested in a block), the handler stack
  was disabled but the stack of running handler blocks was not: `resume:`,
  `outer` or `pass` of an exception instance the two actors shared resolved
  to the waiting actor's handler, and the stray resumption rejected the
  helped message's Future with `exception resume: no active signal to
  resume`. One barrier (`HandlerBarrier`, `HandlerStack.h`) now hides both
  for that turn, for a send from a foreign runtime's frame and for a
  debugger evaluation. Test: `cli_helped_message_handler_actions` (one
  worker, so the interleaving is deterministic; in the `tsan` label).
- **A `thenDo:` / `catch:` callback no longer runs under the handlers of the
  code that settles the future (S36).** `resolve:` inside a protected block
  ran the callbacks at once with the settler's handlers visible: the
  settler's `on: Error` handler caught the callback's error, and its
  `return:` was then dropped, so a handler took effect without its unwind; a
  callback's `resume:` of the settler's exception reached the settler's
  running handler. Callbacks now run behind the same `HandlerBarrier`; their
  errors are reported on stderr as callback errors. Tests:
  `cli_future_callback_barrier`,
  `10-actors/future-callback-not-caught-by-settler-handler.st`.
- **The debugger's Uncaught Exceptions breakpoint honours an `isResumable`
  override (D54).** An unhandled non-`Error` exception whose class answers
  false to `isResumable` ends the activation but did not stop; the
  classification now sends `isResumable` in that case. Test:
  `cli_dap_exceptions` (case `uncaught3`).

- **`Array new:` with a size the heap can never take signals an Error
  (reviews R3-2, R4-1, R5-1, R6-1, R7-1).** `Array new: 3000000000` (a typo,
  say) built an 8n-byte vector and then the list, and ended in protoCore's
  out-of-memory exit or the system's OOM killer. Before allocating, `new:`
  now refuses a size beyond protoCore's list limit (2^32 - 1), or one whose
  build cannot fit even in an empty heap. protoCore builds an n-element list
  by path-copying appends, about n log2 n cells until a collection
  (measured: under a 400,000-cell limit, 22,500 elements build and 23,000 do
  not; at 4,000,000 cells, 200,000 and 202,000); the estimate, n times the
  bit width of n, stays below that cost, and a size is refused only when it
  exceeds the whole heap ceiling: the limit (`STRuntime::heapCeilingCells`),
  or the heap protoCore already reserved when that is larger, since
  protoCore does not enforce a limit below the 262,144 cells it reserves
  while bootstrapping (review R7-1: `Array new: 8000` was refused under a
  100,000-cell limit and builds). Nothing that fits in an empty heap is
  refused: whether it fits now depends on what is live now, which protoST
  cannot know without a collection. Subtracting the cells handed out before
  the first collection (garbage included), or the live set of the last one
  (structures dropped since included), refused sizes that build (reviews
  R5-1, R6-1). So the check errs only towards letting sizes through: those
  that do not fit still end in protoCore's out-of-memory exit, as before
  (from 202,000 elements up to the refusal at about 222,000 at a
  4,000,000-cell limit, and smaller sizes when other data is live). An exact
  answer is queued as protoCore follow-ups (a query of the cells allocatable
  after a collection, a catchable out-of-memory). A failing C++ allocation
  is an Error too, not a terminate. Pharo signals `OutOfMemory`; protoST has
  no such class and signals `Error`. Tests
  `12-builtins/array-new-unholdable-size-signals.st`,
  `cli_array_new_headroom` (under a small fixed limit and a small adaptive
  budget, and sizes that fit once garbage, or a structure dropped after a
  collection, is reclaimed, and a size that fits the reserved heap under a
  smaller limit).
- **An unhandled error inside an inlined nil-test arm has one frame less in
  its trace.** The arm is no longer a block, so the `at <block>` line is gone
  (tutorial 15 updated).
- **The closure analysis no longer treats a block literal receiving `and:`,
  `or:`, `ifTrue:` and the like as inlined (protoBaseST review L3).** Only
  `whileTrue:` / `whileFalse:` inline their receiver block; for the others
  it is a real closure and is now analysed as one. No program observed the
  difference (its variables were captured through another path), so this
  is a robustness fix. Test, which passes before and after:
  `06-blocks/block-receiver-of-inlined-send-captures.st`.
- **An inlined conditional with a non-Boolean receiver is now the send it
  replaces.** `ifTrue:`, `ifFalse:`, `ifTrue:ifFalse:`, `ifFalse:ifTrue:`
  (since 0.1) and `and:` / `or:` (new in 0.10.0) with literal blocks are
  compiled inline. Until now a receiver that was not a Boolean got
  `doesNotUnderstand:` with a Message that had no arguments: a class that
  defines `ifTrue:` (or `and:`, as three-valued-logic and query-builder
  classes do) was never reached, a forwarding proxy could not forward the
  message, and a resumed `doesNotUnderstand:` was taken as the condition, so
  `[ 3 ifTrue: [ #body ] ] on: MessageNotUnderstood do: [ :e | e resume:
  42 ]` answered `#body`. The receiver now gets the message the source
  wrote, with closures for the block literals: its own method, or
  `doesNotUnderstand:` with the whole Message, whose resumed value is the
  value of the expression. The closures are compiled from the same literals
  without inlining (a second copy of each, used only on this path); the
  method's variables they use are carried by a box for the duration of the
  send and copied back when it returns, so a write in the block is seen by
  the method, and `^` in the block returns from the method. A closure the
  receiver keeps and evaluates *after* the send returned signals an `Error`
  (the method owns its variables again by then; a block held in a variable
  has no such limit). A Boolean receiver costs what it did: the guard is a
  new opcode `JUMP_IF_NOT_BOOL` (45) to the out-of-line path, which uses
  four more (`DEOPT_ENTER`, `PUSH_DEOPT_BLOCK`, `DEOPT_EXIT`, `DEOPT_CHECK`,
  46-49; `docs/VM.md` 1.4). Pharo differs here: since Pharo 10 a
  non-Boolean receiver of an inlined conditional signals
  `NonBooleanReceiver` ("proceed for truth") and the receiver's own method
  is not run; Pharo 9 re-sent the expression without inlining
  (`mustBeBooleanDeOptimizeIn:`), which is what protoST does. Tests:
  `08-exceptions/inlined-conditional-non-boolean-like-send.st` (failed
  before: 5 of 11 rows), `inlined-conditional-reaches-receiver-method.st`
  (failed before), `inlined-conditional-kept-block-is-refused.st`.
- **One rule for a loop condition that is not a Boolean.** The four loop
  forms disagreed: an inlined `whileTrue:` / `whileFalse:` sent
  `doesNotUnderstand: whileTrue:` to the condition's value (which is not the
  receiver of `whileTrue:`), a sent `[3] whileTrue: aBlock` stopped and
  answered nil, a sent `[3] whileFalse: aBlock` ran its body, without end
  unless something else stopped it, and so did `whileTrue` / `whileFalse`.
  Every form now signals `NonBooleanReceiver`, a new resumable `Error` whose
  `object` is the value, and takes the resumed value as true unless it is
  `false`: Pharo's class and rule (`ProtoObject>>mustBeBooleanIn:`). The
  inlined guard is renamed `ASSERT_LOOP_BOOL` (still 38; it was
  `ASSERT_BOOL_OR_DNU`). Test:
  `08-exceptions/loop-non-boolean-condition-one-rule.st` (16 rows, inlined
  and sent forms side by side).
- **`lib/random.st`: `nextInt:` and `between:and:` alternated parity.** Both
  took the generator's state modulo the range. The generator is a 32-bit LCG
  with a power-of-two modulus, in which bit k of the state has period
  2^(k+1): over an even range successive draws alternated between odd and
  even, and `nextInt: 4` repeated with period 4. A draw is now
  `floor(state * size / 2^32)`, decided by the high bits, in exact integer
  arithmetic. `next` and `next:` are unchanged; the integer sequences a seed
  gives change (no protoST or protoBaseST test pinned them: protoBaseST's
  `AvlOps` draws with `next`). The kernel `Random` (Park-Miller, no import)
  already scaled `next` and is unchanged. Found by protoBaseST's review
  (P1). Test: `13-stdlib/random-draws-use-high-bits.st` (failed before:
  0 of 999 successive `nextInt: 2` draws were equal, 517 after).
- **The literal `5e-324` was a compile error (K16).** Every float literal
  whose value is subnormal or underflows (`5e-324`, `2.2e-308`, `1e-400`) was
  refused as *out of range*, so `printString` of such a `Float` did not read
  back. A float literal is now the correctly rounded `Float` over the whole
  range: a subnormal is kept, an underflow is `0.0` / `-0.0`, and only a
  literal beyond the largest `Float` stays an error (STATUS D51, a recorded
  decision; LANGUAGE.md §2.5). The lexer and `String >> asNumber` read the
  digits with `std::from_chars`, which ignores the C locale. Tests:
  `02-lexical/float-literal-full-range.st`, `float-literal-round-trip.st`,
  `float-literal-overflow-is-error.st`, and the `[k16]` cases of
  `tests/unit/test_lexer.cpp` (ties to even, 4,000 random doubles' shortest
  forms, a comma-decimal `LC_NUMERIC`).
- **One selector on both sides of a class kept only the later method
  (K14).** `Foo >> x` and `Foo class >> x` wrote one slot of the class object:
  instances got `doesNotUnderstand`, or the class-side method was lost
  silently, and a subclass's class-side method hid an instance method the
  subclass inherits. Every class now has a metaclass (`Foo class`), an object
  of its own holding the class-side methods, and a send to a class looks up
  the metaclass chain only: `Foo class` -> ... -> `Object class` -> `Class` ->
  `Behavior` -> `Object`, as in Pharo (STATUS D52). The same one lookup serves
  `super`, the call form (`Foo class >> make(x)` then `Foo make(1)`),
  `perform:`, the kernel's dynamic sends, foreign sends, actor messages,
  `doesNotUnderstand:` and `respondsTo:`. Tests: the former `XFAIL`
  `04-object-model/class-and-instance-method-same-selector.st` passes; new:
  redefinition of one side, `super` on each side, class-side inheritance and
  override, reflection, `doesNotUnderstand:` on both sides, the class-side
  call form, a class object is not an instance, the metaclass chain, and
  `kernel-class-protocol-not-hidden.st` (`tests/conformance/04-object-model/`).
  The metaclass protocol is Pharo's where the semantics hold: `isMeta`,
  `Foo class soleInstance` (and `instanceClass`) answer `Foo`,
  `lookupSelector:` answers the class or metaclass that holds the method (a
  deviation: Pharo answers the CompiledMethod, STATUS D52),
  `instanceVariableNames:` answers the receiver, `Metaclass new` and
  `Foo class new` signal an Error (a metaclass is made by `subclass:` only),
  so do `Behavior new` and `Class new` (protoST has no anonymous classes),
  and `Foo copy` gets a metaclass of its own, so a class-side method added to
  the copy does not reach `Foo`. Tests: `metaclass-reflection-selectors.st`,
  `class-instance-variable-names.st`, `class-copy-has-own-metaclass.st`,
  `metaclass-new-is-refused.st`, `behavior-and-class-new-are-refused.st`
  (`tests/conformance/04-object-model/`).
- **`isKindOf:` and `inheritsFrom:` ignored mixins that `on:do:` honours.**
  `[MixErr signal] on: Tagged do: [...]` caught an error whose class uses
  `Tagged`, while `MixErr new isKindOf: Tagged` answered false. Both now
  count every ancestor, superclasses and `uses:` / `addBehavior:` mixins
  alike, the set method lookup and handler selection use (LANGUAGE.md
  §4.11); `allSuperclasses` stays the primary superclass chain. Test:
  `04-object-model/is-kind-of-counts-mixins.st`.
- **`perform:` with a selector nothing understands bypassed
  `doesNotUnderstand:`.** It signalled the error directly, so a class with
  its own `doesNotUnderstand:` (a proxy) was not asked. It now sends
  `doesNotUnderstand:` with the Message, as an ordinary send does. Test:
  `05-messages/perform-unknown-selector-goes-through-dnu.st`.
- **An unhandled error caught after its runtime was destroyed crashed the
  embedder (K20).** `try { STRuntime rt; rt.runTopLevel(m); } catch (const
  std::exception&)` died with SIGSEGV: the `proto::ProtoException` pinned its
  value in the space the runtime had just freed. At the embedder boundary of
  a runtime that owns its space the error now leaves as the self-contained
  `protoST::STUncaughtError` (`include/protoST/STUncaughtError.h`: the
  description, the report the binary prints, the trace and the class chain).
  A runtime in a host's space, and every call nested in protoST or another
  runtime's frames, still raise the `proto::ProtoException`. The `protost`
  binary's output is unchanged. Test: `tests/unit/test_k20_embedder_boundary.cpp`.

- **Deeply nested `collect:` (and every other pinning primitive) crashed
  (K15).** A `TransientPin` lived only in a fixed 256-slot scratch region; the
  257th live pin hit `assert(false)` and, in a release build, returned without
  pinning. Nesting reaches that easily: each `collect:` / `select:` /
  `inject:into:` keeps its accumulator pinned while its block runs, and
  protoBaseST's codec walked values 400-509 deep. The unpinned list was freed
  by the next collection and the primitive appended to a recycled cell
  (SIGSEGV in `ProtoList::appendLast`, or "stack smashing detected" when the
  recycled cell's size overflowed appendLast's stack buffer). A pin that finds
  the region full now goes on a per-context overflow chain (one list cell per
  pin, its head in a reserved scratch slot, released without allocating), so
  the number of live pins is bounded only by memory. The chain code is out of
  line, so a pin in the region costs what it did (+0.07 % instructions on a
  pin-heavy loop, cycles unchanged). A chain pin released out of LIFO order
  aborts the process with a message in every build. A `TransientPin` made with
  a null object pointer now claims its place too, so a later
  `TransientPin::reset` roots its object. Not a protoCore bug. Tests:
  `tests/unit/test_transient_pin.cpp` (1000 live pins, deterministic; unwinding,
  two contexts, out-of-order release) and
  `tests/conformance/09-collections/collect-deep-nesting-gc.st` (0.9.0 crashed
  10 runs out of 10).
- **A send or an import into protoST from another runtime's frame context
  wrote into that frame's slots (K18).** `__st_send__` and UMD module imports
  (in protoST's own space, and with protoST a guest in a host's space) ran
  protoST's engine on the caller's context whenever it belonged to protoST's
  space, so the engine grew another runtime's frame context to its own
  geometry and put transient pins and frames at the same indices as the
  frame's locals. Work for a caller now runs on a child context of any context
  that is not protoST's own, with the frame and pin cursors started afresh for
  it; the guard is at every entry that runs protoST code on a caller-supplied
  context (`__st_send__`, `runTopLevel(m, ctx)`, `loadModuleFromFile`,
  `importModuleFile`). Tests: `tests/unit/test_foreign_frame_context.cpp`,
  `tests/unit/test_k18_module_entry.cpp`.
- **A non-local return or a Future yield could leave `__st_send__` through
  the caller's frames (K19).** A caller in protoST's own space skipped the
  runtime boundary; now every `__st_send__` crosses it, and a control-flow
  unwind is reported as an error, as for a caller in another space. Test:
  `tests/unit/test_foreign_frame_context.cpp`.

- **`lib/json.st` crashed on every `\uXXXX` escape (K17).** The parser sent
  `and:ifTrue:` (an unparenthesised `and:` before `ifTrue:`) in its hex-digit
  reader, and the writer concatenated a `Character` when escaping control
  characters. Both are fixed; `\uXXXX` now parses in either hex case, a
  surrogate pair (`\ud83d\ude00`) becomes one code point, and a lone or
  mismatched surrogate or an invalid or truncated hex escape signals the
  library's parse error. Tests: `tests/conformance/13-stdlib/json-*unicode*`,
  `json-*surrogate*`.

### Performance

- **Interpreter round 3 (driven by the protoBaseST/ProtoBase gap
  analysis).** Per operation, in ns beyond the empty loop (the analysis's
  own `ops.st`, 2x10^6 iterations in a method, minimum of three runs),
  round-3 base -> final, CPython 3.14 in brackets: `i * 2` 129 -> 21
  [26], `i \\ 7` 420 -> 24 [37], `i // 7` 378 -> 23 [24], `bitAnd:` 135 -> 20 [29], `Array at:` 237 -> 133 [~0], `Array new:
  4` 2710 -> 292 [113], send 130 -> 114 [42], `block value:` 116 -> 104
  [29]. `perform:with:` 3800 -> 375 ns per call, loop included
  (`benchmarks/micro/vm_ops.st`, self-checking). An instance-variable write
  did not improve measurably (79 -> 112 in the full `ops.st` order, not
  reproduced alone, where it runs 10 % fewer instructions; it is
  protoCore's `setAttribute` and the collector). The changes:
  - `*`, `\\`, `//`, `bitAnd:`, `bitOr:` on two SmallIntegers are computed
    in the engine (opcodes `BIN_INT_MUL` 53, `BIN_INT_MOD` 54, `BIN_INT_QUO`
    55, `BIN_INT_AND` 57, `BIN_INT_OR` 58; overflow, a non-SmallInteger
    operand and a zero divisor fall back to the send). These are Pharo's
    special selectors, and as there (and as for `+`), a method a program
    adds to `SmallInteger` for them is not reached by a SmallInteger
    receiver. `rem:` is not a special selector in Pharo, so it stays a send
    and such a method is reached (review R3-1; 56 is reserved). Tests
    `12-builtins/integer-fast-paths-match-the-send.st`,
    `04-object-model/smallinteger-rem-override-is-reached.st`.
  - An assignment whose value is discarded compiles without `DUP`/`POP`.
    Test `05-messages/assignment-statement-values.st`.
  - The dispatch core indexes the frame's slots directly (bounds checks
    become debug assertions) and pushes SmallInteger and Symbol constants
    without materialising them. The threaded-dispatch jump table is built
    at compile time instead of being filled (256 stores) on every engine
    entry, which every block a primitive evaluates pays; GCC needs
    `--param=max-goto-duplication-insns=32` on that file to keep one
    indirect jump per handler (review R3-4). Against the round-3 base, in
    cycles: `int_sum_loop` (`inject:into:`, 10^5) 106M -> 89M, the same at
    3x10^6 1.63G -> 1.08G. **Measured regression:** a loop whose body is an
    inlined `true ifTrue: [s := i]` (2x10^7) takes about 8 % more cycles
    (4.8-5.0G -> 5.3-5.5G) with 13 % fewer instructions; its cost is the
    shared loop-head dispatch that `JUMP_BACK` returns through, and no
    layout under protoST's control was found that removes it without giving
    back the gain above.
  - `Array new:` builds its storage in one `newList` call and publishes no
    intermediate mutable object.
  - `perform:` to `perform:with:with:with:` are primitives (they built an
    Array for `perform:withArguments:`). Test
    `05-messages/perform-family-primitives.st`.
  - A send no longer probes an immediate receiver or a primitive marker for
    actor and method attributes, and no object at all for the actor
    attribute until the process has created an actor.
  The protoBaseST AVL workload runs 9.5 % fewer user instructions (10.15G
  -> 9.18G) and 3 % less CPU time; its ratios to ProtoBase moved from
  14-25x to 13-22x per operation in one interleaved run and barely in
  another, because its cycles are mostly protoCore's (allocation,
  sparse-list lookup and update, the collector's sweep), not dispatch. A
  send remains about 3x CPython's: the method lookup in protoCore and the
  frame push.
- **`==` and the nil tests never send (protoBaseST review P4).** As in
  Pharo, `==` is compiled to an identity test (`IDENTICAL`, opcode 52) and
  never looked up (Pharo's `ProtoObject>>==`: "No Lookup. Do not override"),
  and `ifNil:`, `ifNotNil:`, `ifNil:ifNotNil:`, `ifNotNil:ifNil:` with
  literal blocks (the nil arm without arguments, the non-nil arm with none
  or one) are compiled inline (`JUMP_IF_NIL` 50, `JUMP_IF_NOT_NIL` 51) under
  the conditions of Pharo's `RBMessageNode>>isInlineIfNil`. An inlined
  `ifTrue:` after `==` needs no Boolean guard. 10^6 iterations in a method
  (`benchmarks/micro/nil_identity.st`, three runs against 4bae623):
  `x == #a ifTrue:` 247-300 -> 121-192 ms, `ifNil:` 1.80-1.97 ->
  1.12-1.20 s, `ifNotNil: [:v|...]` 1.92-2.22 -> 0.97-1.00 s,
  `ifNil:ifNotNil:` 2.39-2.61 -> 0.96-1.07 s. The protoBaseST AVL workload
  runs 4.6 % fewer instructions than at 4bae623 (8.69G -> 8.30G; this
  includes the startup cost of the de-optimised copies, see Fixed); its
  wall-clock per-operation figures are within noise.
  **Semantic change:** a class's own `==` or `ifNil:` is no longer reached by
  these forms; `perform:` and non-literal blocks still send. No class in
  `lib/` or protoBaseST defines either. `ifNotNil:ifNil:` is now also bound
  as a method (it was listed as a nil test but had none). Tests:
  `06-blocks/inlined-nil-tests-values.st`,
  `04-object-model/identity-and-nil-tests-are-not-looked-up.st`; VM.md
  example 19.
- **`and:` and `or:` with a literal block are compiled inline (protoBaseST
  review P2).** Like `ifTrue:`, a zero-argument block without temporaries
  is no longer a closure evaluated by a primitive in a nested engine: its
  body is compiled into the enclosing method. 10^6 iterations of
  `((i > 10) and: [ i < 900000 ]) ifTrue: [...]` in a method went from
  8.2-8.6 s to 0.21 s, `or:` from 9.8-10.4 s to 0.19 s, the cost of the
  nested `ifTrue:` they mean (0.19-0.23 s); `benchmarks/micro/and_or.st`.
  The value is the primitive's: `true and: [3]` is 3, the block's value is
  not checked. A non-Boolean receiver gets the real send (see Fixed); a
  non-literal argument (`x and: aBlock`) is still a send. protoST has no
  `and:and:` / `or:or:`, so there are none to inline. Tests:
  `06-blocks/inlined-and-or-values.st`,
  `07-non-local-return/nonlocal-return-from-inlined-and-or.st`, a debugger
  single-step case in `tests/unit/test_debugger.cpp`; `docs/VM.md` 1.4 and
  the opcode table; vm-examples 03, 05, 14 and 18 regenerated.

- **A block costs a quarter of what it did; `isKindOf:` is a primitive; a
  send does less work (protoBaseST review P3).** Profiled with `perf`:
  - A closure was a *mutable* object written by four `setAttribute` calls,
    four publications into protoCore's mutable table, and most of the
    garbage of every block (the sweep was the top symbol of a block loop).
    `PUSH_BLOCK` now builds an immutable child of `BlockClosure` and writes
    its four attributes as one version (`setAttributes`); nothing writes a
    closure afterwards, and a method, which is a closure, is immutable too.
    Two closures of one literal remain distinct objects (test
    `06-blocks/block-closures-are-distinct-objects.st`). A block has no
    instance variables to set: `aBlock instVarNamed: 'x' put: v`, which
    stored the value on the mutable closure of 0.9.x, now signals
    `ModificationForbidden`; without the check the write would have been
    lost silently (test `06-blocks/block-instvar-write-is-refused.st`).
    Instance-variable assignment in a method refuses an immutable receiver
    the same way (the compiler already rejects it in `BlockClosure`
    methods; the plain `STORE_INSTVAR` handler skipped the check until
    16b940d, the grouped write had it).
  - `isKindOf:` was a kernel method (`self class`, then `or:` with a block,
    then `__hasAncestor:`); it is now one primitive over the same ancestor
    test. It uses the receiver's real class, as Pharo's (where `class` is a
    bytecode): a `class` method that answers another class no longer
    changes it (test `04-object-model/is-kind-of-uses-the-real-class.st`,
    which answered `false` for the first case before).
  - Per send: the block fast path tests for a `value`-family selector
    before it probes the receiver for `__bc_ptr__` (every send to an object
    paid that probe); a keyword selector's argument count is counted once
    when the constant is interned, not on every send; `push` / `pop` /
    `peek` are forced inline into `runLoop` (GCC had left them as calls);
    a new frame clears its header and locals but not its operand stack,
    which is never read above its top (protoCore scans every slot as a
    root, so an old value there cannot dangle).

  10^5 iterations in a method, ns per iteration including the loop
  (`benchmarks/micro/blocks_kindof.st`, 0.9.x -> 0.10.0): create a block
  2,200-2,400 -> 600-650; `[ 1 ] value` 2,050 -> 780-870; a block that
  captures the loop variable 4,500-4,700 -> 1,760-1,990; `at:ifAbsent: [0]`
  4,300 -> 790-830; `[ 1 ] on: Error do: [...]` 12,000-15,000 -> 1,280;
  `isKindOf:` 9,500-14,000 -> 260-400 (the `or:` block was most of it). The
  comparable suite (`perf stat -r 3`, instructions / cycles): `fib` -16 % /
  -12..-20 % (two machines' runs; instructions are the stable figure), `attr_lookup` -11 % / -17 %, `exception_latency` -24 % / -27 %,
  `int_sum_loop` -9 % / -2 %, `list_append` -4 % / -5 %. protoBaseST's
  AVL on 1000-element trees: `posAt:` 209-222 -> 151 us, `keyAt:` 128-154
  -> 85 us, `keyPut:` 1,088-1,167 -> 831-841 us; the rest of that cost is
  the number of sends its code makes, at about 0.15 us each.

### Breaking changes

- **`outer` no longer behaves as `pass`** (see *Fixed*): a program whose
  enclosing handler resumes now continues in the inner handler after
  `outer`, not at the `signal`. `[[(Warning signal: 'w') + 1] on: Warning do:
  [:e | e outer]] on: Warning do: [:e | e resume: 4]` answers 4 (it answered
  5). Code that wanted the old effect sends `pass`.
- **`pass` runs the enclosing handler before unwinding the passing one**
  (see *Fixed*): the passing handler's `ensure:` and `ifCurtailed:` blocks
  now run after the enclosing handler, not before it.
- **A chain of handlers that pass or `outer` outward reaches the engine
  nesting limit at about half the former depth.** Each enclosing handler
  now runs on top of the one that passes (Pharo nests contexts the same
  way). Measured on a Release build, a chain of `Warning` handlers that each
  pass (or `outer`) to a resuming outermost handler completes up to 497
  levels; f799ab9 completed 995. Past that the limit's catchable `Error`
  arrives once, at the handler that could not start (S23); `Error` chains
  still complete, because the limit `Error` goes to the next `on: Error`
  handler. LANGUAGE.md §8.4.
- **`resume:` of a non-resumable exception signals `IllegalResumeAttempt`,
  which is not an `Error`.** It raised a plain `Error`, so `on: Error do:`
  caught it; catch it with `on: IllegalResumeAttempt do:` or `on: Exception
  do:`. Unhandled, it still ends the program with `cannot resume a
  non-resumable exception`.
- **A class's `isResumable` override decides resumability**, and
  **`retryUsing:` runs the replacement under the same handler** (see
  *Fixed*, D54 and D55).
- **MessagePack nesting depth is 1024 (was 512)**, encode and decode, from
  protoIO 0.5.0's default: msgpack-python 1.2's unpacker limit and the record
  depth ProtoBase format 4 requires every reader to accept since ProtoBase
  v2.0.0rc2 (a value nests up to 1022 containers, the record envelope adds 2).
  A structure 513 to 1024 deep that signalled `Error` now encodes. Test
  `15-binary/msgpack_depth_boundary.st` pins 1024 / 1025.
- **A structure nested too deep signals `MessagePackDepthExceeded`**, a new
  `Error` subclass, on encode and decode (protoIO 0.5.0's
  `Error::Kind::DepthLimit`), where it signalled a plain `Error`. `on: Error
  do:` still catches it; protoBaseST reports ProtoBase's `MAX_VALUE_DEPTH`
  text from the class instead of matching the message.

- **An `Array` of Integers is no longer bytes (D45, ruling R11).** As the
  0.9.0 deprecation warning announced, every write that takes bytes or a
  String refuses an `Array`: `FileReference >> contents:`, `appendContents:`
  and `binaryContents:`, `IOStream >> nextPutAll:` and `nextPutBytes:`
  (standard streams, file streams and sockets, TLS included) and UDP `send:`.
  The `Error` names the selector that was sent, says "an Array is not bytes"
  and names the replacement, `ByteArray withAll: anArray` (or a literal
  `#[1 2 3]`); nothing is written. The warning and its code path are
  gone. Tests: `14-io/files_bytes_array_refused.st` (was
  `files_bytes_array_deprecated.st`), `14-io/net_udp_array_refused.st`;
  `files_bytes_bad_array.st` now expects the Array refusal, and
  `http_chunked_utf8.st` passes a `ByteArray` literal.
- **A class no longer answers its instances' methods (D52, superseding part
  of D5).** Until now a class object reached its own instance-side protocol:
  `Foo greet` ran `Foo >> greet` on the class, `Foo = Foo` ran an
  instance-side `=`, `anInstance new` made a sibling, and `Array at: 1` reached
  the Array primitive. Each of these is now `doesNotUnderstand` (or, for `=`,
  Object's identity), as in Pharo. Code that relied on it must define the
  method on the class side (`Foo class >> greet`). Related changes:
  - The class protocol (`new`, `basicNew`, `newChild`, `subclass:`..., `name`,
    `superclass`, `selectors`, `canUnderstand:`, `addBehavior:`, and the
    `reflection.st` methods `instVarNames`, `inheritsFrom:`, `subclasses`,
    ...) moved from `Object` to the new class `Behavior`; `Class` and
    `Metaclass` are new globals. An instance no longer understands `new`.
  - `canUnderstand:` describes the receiver's instances, as in Pharo
    (`Array canUnderstand: #at:put:` true, `Array class canUnderstand:
    #at:put:` false); `respondsTo:` describes the receiver.
    `Foo class class` is `Metaclass`, `Object class superclass` is `Class`.
  - The built-in constructors are bound on their metaclasses only (the
    double binding of the first K14 fix is gone), and `Exception signal` /
    `signal:` stay class-side messages that build the instance.
  - The kernel's 37 `isClassObject` guards against a class running an
    instance method are removed (`calendar.st`, `decimal.st`, `printing.st`,
    `reflection.st`, `fraction.st`, `string.st`, `character.st`);
    `Random seed:`, `Association key:value:` and `Notification signal:` are
    class-side methods.
  - A send to a class costs one lookup again (the first K14 fix added a
    second).
- **An embedder of a runtime that owns its space catches `STUncaughtError`
  (K20).** `STRuntime()`'s `runTopLevel`, `loadModuleFromFile`,
  `importModuleFile` and `loadModule` called by the embedder raise
  `protoST::STUncaughtError` (a `std::runtime_error`) for an unhandled error
  instead of `UnhandledSTException`; code that caught `proto::ProtoException`
  there must catch `STUncaughtError` (or `std::exception`), and code that
  needs the exception object must run the runtime in its own space
  (`STRuntime(proto::ProtoSpace&)`).

## 0.9.0 — bytes, binary files, encodings, MessagePack and timers (2026-11-10)

Released 2026-11-10.

**Requires protoIO 0.4.0** (tag `v0.4.0`; the declared minimum for a build
from source, `PROTOIO_MIN_VERSION`; a 0.3.x checkout is refused at configure
time): the `BinaryFile` end guard is protoIO's `file::EndGuard`. **Built and
tested against protoCore 3.5.2** (SOVERSION 5), which fixes a race in
`ProtoSpace::newThread` that protoST reaches by starting workers from worker
threads (`KNOWN_ISSUES.md` K10). The declared protoCore minimum for a build
from source stays 3.4.0, which CI's floor entry builds; the `.deb` is built
against the 3.5.2 package, so it depends on `protocore (>= 3.5.2)` through
protoCore's shlibs file, besides the declared `protocore (>= 3.4.0),
protocore (<< 4.0.0)`. CI pins protoCore `v3.5.2` and protoIO `v0.4.0` on
Linux, macOS and Windows. Design record:
`docs/superpowers/specs/2026-11-09-bytes-files-timers-design.md`; the language
reference is `docs/LANGUAGE.md` §12.12, the tutorial is chapter 15 (§15.11)
and chapter 10 (§10.12), and the deviations are STATUS D41-D48.

### Breaking changes

- **`binaryContents`, `IOStream >> nextBytes:` and UDP `receiveBytesTimeout:`
  answer a `ByteArray`, not an `Array` of Integers.** A program that compares
  the result with `#(0 1 255)` must compare with `#[0 1 255]`, or call
  `asArray`. `printString` of the result is `#[0 1 255]`.
- **An `Array` of Integers as a binary *argument* is deprecated** (`binaryContents:`,
  `nextPutBytes:`, UDP `send:`): it still works in 0.9.0 and prints one line on
  stderr per process; it is refused from **0.10.0**.
- **`binaryContents: nil` and `binaryContents: 42` now fail** with
  "a String or a ByteArray was expected". They used to write an empty file
  without a word. A non-byte in an Array fails as before.
- **`on:do:on:do:` searches its pairs in written order**, the first pair
  first, as the clauses of a try/catch: `[1/0] on: ZeroDivide do: [:e | 1]
  on: Error do: [:e | 2]` answers `1`; 0.8.1 answered `2` (it searched the
  second pair first). And the two pairs of one construct no longer catch each
  other: an exception signalled, or passed, by one pair's handler goes to an
  enclosing handler; in 0.8.1 the other pair could take it. Pharo has no
  `on:do:on:do:`; this is protoST's extension, now with the order of
  `try`/`except` (LANGUAGE.md §8.3).
- **Indices** (details under *Fixed*):
  - an Integer index out of range signals `SubscriptOutOfBounds` everywhere,
    however large: the indexing methods written in Smalltalk (`Interval`,
    `OrderedCollection>>removeAt:` and `add:beforeIndex:`,
    `ByteArray>>copyReplaceFrom:to:with:`) signalled a plain `Error`, and an
    index of 2^63 or more `ArithmeticError`;
  - a non-Integer index signals `Error` "... the index: an Integer was
    expected": a Float index raised protoCore's `InvalidArgument` (so a
    handler for `InvalidArgument` no longer catches it), and
    `NumericInterval>>at:` accepted one;
  - a `Character` index is refused, as in Pharo; it was read as its code
    point (`#(10 20 30) at: (Character value: 2)` answered `20`).
- **`BinaryFile`**: `truncateTo:` is refused while any `writeAt:bytes:` runs
  on the same object (the end guard covers in-file writes too), and `append:`
  on a file that is not a regular file (a FIFO, a pipe, a device) signals
  `FileSystemError` and writes nothing (details under *Changed*).
- `NUL` is kept in strings now (see below), so a program that relied on text
  being cut at a NUL sees the whole text; diagnostics (the uncaught error, an
  unhandled `Warning`, an unobserved actor error, parse and compile errors)
  print it whole too.
- **A string handed to the OS that contains NUL signals `Error`** ("... must
  not contain NUL", as CPython's `ValueError`): a path, an argv element, a
  shell command line, an environment name or value, a host name, a module
  name. The OS reads such a string only up to the NUL, so `BinaryFile open:
  'victim', NUL, '.evil'` used to create `victim` (`KNOWN_ISSUES.md` K12).

### Added

- **`ByteArray`**: an immutable value with the literal `#[1 2 255]` (also
  nested in `#(...)`), `new:`, `withAll:`, `fromHex:`, `hex`, `,`,
  `copyFrom:to:`, `copyReplaceFrom:to:with:`, `=`, `hash` (usable in `Set` and
  as `Dictionary` key), `readStream`, `writeStream` and `WriteStream on:
  ByteArray new` (`nextPut:`, `nextPutAll:` and `<<` taking a `ByteArray`
  only, `contents`);
  `String >> utf8Encoded` and strict `ByteArray >> utf8Decoded`. Deviations
  from Pharo: D41, D42.
- **Numbers in bytes**: `unsignedByteAt:`, `unsignedShortAt:bigEndian:`,
  `unsignedLongAt:bigEndian:`, `unsignedLong64At:bigEndian:`, the signed
  versions (with `signedByteAt:`), `float32At:bigEndian:`,
  `float64At:bigEndian:`, and on a byte stream `nextPutUnsigned:width:bigEndian:`,
  `nextPutSigned:width:bigEndian:`, `nextPutFloat32:bigEndian:`,
  `nextPutFloat64:bigEndian:`. A value that does not fit signals `Error`
  (D43). All 168 vectors of `protoIO/tests/vectors/packing.json` pass.
- **Checksums and digests**: `crc32`, `crc32Continuing:`, `CRC32 combine:with:length:`,
  `blake2b:`, `blake2b:key:`, `sha256`, `HMAC sha256Key:message:`,
  `base64Encoded`, `base64Decoded` (strict, D44). The vector files of protoIO
  pass in full; the generator is `tests/conformance/15-binary/gen_vectors.py`.
- **`BinaryFile`**: a positionless file with `open:options:`, `append:`,
  `readAt:size:`, `writeAt:bytes:`, `size`, `truncateTo:`, `sync`, `dataSync`,
  `tryLock`, `lock`, `unlock`, `close`; `FileSystem replace:with:` and
  `syncDirectory:`; the new `InternalError`. The lock is the one ProtoBase's
  `OwnerLock` takes. One appender at a time per file: a concurrent `append:`
  signals `Error` (D46, `KNOWN_ISSUES.md` K8).
- **`MessagePack`**, `MessagePackMap`, `MessagePackExt`: `encode:`, `decode:`,
  `decodeFirst:`, `decodeOrdered:`; all 70 vectors pass, and documents are
  exchanged with CPython's `msgpack` in both directions (D47).
- **Future deadlines**: `Future after:`, `within:`, `waitWithin:` and
  `TimeoutError`, on one timer thread per runtime that never runs Smalltalk
  code (D48).
- **A conformance test can be skipped visibly**: a program whose last value
  starts with `SKIP: ` makes the runner exit 77 and CTest report *Skipped*
  (`tests/conformance/README.md`).
- New CLI tests `cli_binary_lock`, `cli_binary_lock_protobase` and
  `cli_binary_interop` (the last two skip visibly without `python3`).

### Fixed

- **Strings keep NUL**: text read from a file or a stream, `Character asString`
  (`(Character value: 0) asString` used to be empty), `,`, `copyFrom:to:`,
  `asUppercase`, `reversed`, `substrings`, `trimBoth`, `copyReplaceAll:with:`,
  `asSymbol`, `perform:` and a string literal that contains a NUL were all
  cut at the first NUL through `c_str()`. They are built with an explicit
  length now. So are the texts of native errors: protoST throws `NativeError`,
  which keeps the whole message, and every site that turns one into a
  Smalltalk error reads it whole (`nativeErrorText`), so `ByteArray fromHex:
  '0', NUL, 'zz'` no longer reports `ByteArray fromHex: '0`. The command line
  and the REPL print diagnostics (the uncaught error, an unhandled
  `Warning`'s text, the unobserved actor error, parse and compile errors,
  REPL commands) and printed values whole as well
  (`KNOWN_ISSUES.md` K7, resolved).
- **Every index handed to a protoCore list goes through `listIndex`**, which
  refuses an index past `INT_MAX` with an `Error`; about 30 sites narrowed it
  with `static_cast<int>`, which would have read or replaced the wrong
  element of a list with more than 2^31 - 1 elements. An index past the int
  range on a small collection (`#(1 2 3) at: (2 raisedTo: 32) + 1`) was
  already refused by the bounds check and still is.
- **One rule for every index: an Integer out of range, however large,
  signals `SubscriptOutOfBounds`; anything else signals `Error`** "... the
  index: an Integer was expected".
  - An index of 2^63 or more (or a negative `LargeInteger`) signals
    `SubscriptOutOfBounds`, with its exact digits; it raised `ArithmeticError`
    ("LargeInteger value exceeds long long range").
  - The primitives read their index through one reader
    (`src/primitives/IndexArg.h`): `at:` of `Array`, `OrderedCollection`,
    `Interval` (the native one, with SmallInteger bounds and an exact
    stride) and `String`; `at:put:` of `Array` and `OrderedCollection`;
    `copyFrom:to:` of `String` and `ByteArray`; `ByteArray at:`; and the
    packed reads of `ByteArray` (`unsignedByteAt:`, `signedByteAt:`, and
    `unsignedShortAt:bigEndian:`, `signedShortAt:bigEndian:`,
    `unsignedLongAt:bigEndian:`, `signedLongAt:bigEndian:`,
    `unsignedLong64At:bigEndian:`, `signedLong64At:bigEndian:`,
    `float32At:bigEndian:`, `float64At:bigEndian:`).
  - The indexing methods written in Smalltalk follow the same rule
    (`Object>>__checkIndex:between:and:for:`): `at:` of every other
    `Interval` (a `NumericInterval`: empty or single-element, an inexact
    stride, Float, Fraction or LargeInteger bounds; and so its `first` and
    `last`), `OrderedCollection>>removeAt:` and `add:beforeIndex:`, and
    `ByteArray>>copyReplaceFrom:to:with:`. They signalled a plain `Error`
    for an index out of range (`(1 to: 0) at: 1` reported "Interval>>at::
    index 1 out of range 1..0", with a doubled colon), and
    `NumericInterval>>at:` accepted a Float index (`(1 to: 5 by: 0.5) at:
    1.0` answered `1.0`). `copyFrom:to:` of `Array`, `OrderedCollection` and
    `Interval` reads its elements with `at:`, so it follows the rule too.
  - **A Character index is refused**, as in Pharo: `asLong` read it as its
    code point, so `#(10 20 30) at: (Character value: 2)` answered `20` and
    `'abcdef' copyFrom: (Character value: 2) to: 3` answered `'bc'`. A
    Float index, refused before by protoCore's `InvalidArgument`, is now
    the same `Error`, so a handler for `InvalidArgument` no longer catches
    a non-Integer index.
- **`STRuntime::enqueueTask` had no consumer**: a task put on the live
  registry's list was never run and the list grew for ever. `drainOne` now runs
  queued tasks first (`runOneTask`); the timer service uses it.
- **A worker at the helping cap no longer starves timers**: a `waitWithin:`
  nested in a block of 200 actors waited for ever, because every worker had
  reached the cap of buried actors and nobody ran the settles. A worker past
  the cap still runs queued tasks.
- **A wait inside a `thenDo:` callback** (no current actor, on a pool worker)
  no longer uses the main thread's single wait slot: it helps other work like
  a nested wait does.
- **An actor error that arrives after a `within:` deadline is reported**:
  `within:` observed its receiver, so such an error reached nobody and was
  never reported as unhandled.
- Argument errors name the selector once ("`Future class>>after: the
  milliseconds is out of range`", not "`Future after:: ...`"), and a
  `LargeInteger` where a `SmallInteger` is needed is "out of range", not "an
  Integer was expected". A descriptor, port, pid or signal beyond a C `int` is
  refused instead of being narrowed silently.
- Iterating a collection while a block replaces its storage kept raw pointers
  across allocation in `forEachElement`, `dictForEachEntry` and
  `dictionaryEntries`; the snapshot is rooted now.
- **A data race in the debugger**, found by the ThreadSanitizer job: two
  threads resuming from a DAP stop at once both wrote the debugger's
  `lastCommand_`, a single debugger-wide copy of the resume command that
  nothing read. The command is applied to the resuming thread's own stepping
  state, so the shared field is gone. Every debugger test (`cli_debugger`,
  `cli_repl` and all `cli_dap*`) now carries the `tsan` label.

### Changed

- `lib/kernel/io.st`'s binary selectors and docs speak of `ByteArray`;
  `docs/STATUS.md:323` ("Binary data is an Array of integers") is corrected.
- Version 0.9.0 in the REPL banner and the HTTP client's `User-Agent`.
- Digests, CRC-32 and base64 over 64 KiB or more run outside the collector's
  quorum, so they never hold up a collection. The decision is one function
  (`runDigest`, `src/primitives/DigestRunner.h`) that every digest primitive
  calls. A unit test calls `runDigest` directly and observes the unmanaged
  state from inside the digest on both sides of the threshold; that each
  primitive calls it, with its input's size, is shown by the code, not by a
  test.
- **The `BinaryFile` end guard is protoIO's** (`file::EndGuard`, protoIO
  0.4.0), and it now covers in-file writes too: `truncateTo:` is refused while
  any `writeAt:bytes:` runs, so a write judged inside the file can no longer
  land past the end of a file a concurrent `truncateTo:` cut (that race was
  the caller's before). The refusals name their selector
  (`BinaryFile>>append:`, `writeAt:bytes:`, `truncateTo:`). A unit test
  holds each use of a `BinaryFile`'s own guard and runs the three primitives
  from Smalltalk, so it shows which use each takes and every refusal text.
  `append:` on a file that is not a
  regular file (a FIFO, a pipe, a device) signals `FileSystemError` and
  writes nothing.

- **ThreadSanitizer build and label.** `-DPROTOST_TSAN=ON` builds protoST,
  and the sibling protoIO, with `-fsanitize=thread`; `ctest -L tsan` runs the
  580 tests that exercise more than one thread, with TSan's reports written
  to files and checked by a fixture at the end (`docs/INSTALLATION.md`,
  "Running the tests under ThreadSanitizer"). CI runs it in a `tsan` job
  that gates, against a TSan build of protoCore 3.5.2 (3.5.0 has a race in
  `newThread`, K10).

### Known limitations (not defects of this release)

K8 to K10 in `KNOWN_ISSUES.md`: what the `BinaryFile` end guard cannot see
(another `BinaryFile` object, another process), timers and exit, what the
tests do not prove (a clean ThreadSanitizer run covers the interleavings it
saw, not all of them; `InternalError` cannot be forced from a program, so its
mapping is tested from C++; the macOS and Windows behaviour of the visible
skip is confirmed only by CI), and K13, a mailbox batch of more than 2^31 - 1 messages, which no program can
reach and which would be lost. K12 (protoIO did not refuse a NUL in an OS
string) is resolved by protoIO 0.4.0, and K11 (no structural list split) by
protoCore 3.5.1. The `Array` argument deprecation ends
in 0.10.0 (D45; it did).

## 0.8.1 — on protoCore 3.5.0 (2026-10-31)

**Built and tested against protoCore 3.5.0** (SOVERSION 5, unchanged from 3.4.0). The declared minimum for a build from source stays **protoCore 3.4.0**, which CI's floor entry still builds; the `.deb` is built against the 3.5.0 package, so it depends on `protocore (>= 3.5.0)` through protoCore's shlibs file, besides the declared `protocore (>= 3.4.0), protocore (<< 4.0.0)`. CI pins protoCore tag `v3.5.0` on Linux, macOS and Windows.

protoCore 3.5.0 changed `ProtoSet` and `ProtoMultiset` (collision buckets, pointer-identity equality, a multiset iterator that returns elements). protoST uses neither: `Set` and `Dictionary` keep a `ProtoMap` through protoCore's hashed-collection helper with Smalltalk's `=` and `hash`, and `Bag` a `ProtoList` with one slot per occurrence. No source change was needed.

### Changed

- Comments and docs no longer claim that a `ProtoMultiset` cannot be iterated to recover its elements, which is false from protoCore 3.5.0; they give the reason that still holds (`ProtoMultiset` and `ProtoSet` compare by pointer, a `Bag` and a `Set` by `=`), and no longer describe `Set` as ProtoSet-backed or `Bag` as ProtoMultiset-backed (#9).
- Version 0.8.1 in the REPL banner, the HTTP client's `User-Agent` and the install guide.

## 0.8.0 — on protoCore 3.4.0 (2026-10-31)

**Requires protoCore 3.4.0** (SOVERSION 5), the declared minimum for a build from source; the `.deb` depends on `protocore (>= 3.4.0)`. protoCore 3.4.0 removed `ProtoContext::pendingRoot`, which changed `ProtoContext`'s layout and so the library's SOVERSION: a protoST built against 3.4.0 does not load an older `libprotoCore`, nor the reverse. protoST needed a rebuild, no source change, for it. CI pins protoCore tag `v3.4.0` on every job; the floor job builds the same commit for now (no older protoCore satisfies SOVERSION 5) and stays on 3.4.0 when the pin moves on. protoIO's floor is 0.2.3.

This release brings a guest mode in another runtime's ProtoSpace, the Debug Adapter Protocol server grown into a usable debugger (exception, conditional, hit-count and log breakpoints, per-thread stops, stepping by call depth, remote launch and path mappings) with VS Code extension 0.5.1, `docs/VM.md` with `protost --disasm`, fixes to five VM bugs and to four defects found while preparing the FAST workshop, `nextLine` reading buffered lines without leaving the collector's quorum, and fewer allocations on the registry and class-rebuild paths.

- **Live registry entries are keyed by an interned symbol.** The pointer key
  of every `registryAdd`, `registryRemove` and startup pin was built as a
  heap string, which protoCore then interned on its slow path (97,508 such
  calls in the test suite); `createSymbol` gives the same key directly, and
  the transient pins around the key are gone.
- **Own attributes are enumerated with `processOwnAttributes`.** `addBehavior:`
  (copying the old class's own attributes onto the rebuilt class) and the
  REPL's snapshot of global names no longer walk `getOwnAttributes` with a
  sparse-list iterator; no iterator cells are allocated.
- **CI: the macOS job runs on `macos-15`** (arm64), since GitHub retires the
  macOS 14 images on 2026-11-02.
- **FAST workshop material** (2026-11-12): slides, guide and exercises under
  `docs/talks/2026-11-12-fas-workshop/` (Spanish).

- **Four defects found while preparing the FAST workshop, fixed** (STATUS S32-S35).
  (1) A call-form `name(` needs its `(` right after the name: a method ending in
  `^ y` followed, after a blank line, by a statement starting with `(` took that
  statement as `y(...)`'s arguments inside the method, silently; a unary method
  whose body began with `(` on the next line parsed as a call-form declaration.
  (2) `super` finds the defining class by identity (a `__class_id__` stamped at
  `__installMethod:as:`), not by name: `m Figure subclass: #Figure` made the
  imported `Figure`'s `super` recurse until the stack depth was exceeded.
  (3) `SmallInteger` is protoCore's 54-bit immediate: `maxVal` is 2^53 - 1 (it
  said 2^55 - 1) and `class` reads the tag, so a heap integer between 2^53 and
  2^55 no longer claims to be a `SmallInteger`. (4) `BlockCannotReturn` no
  longer says the home method has returned when the block was sent to another
  actor and its home is waiting: the text names both cases. New conformance
  programs `03-grammar/call-form-paren-must-touch-name`,
  `11-modules/super-resolves-by-identity-not-name`,
  `10-actors/nonlocal-return-to-another-actor`; `integer-bounds-bits-roots`
  updated. The first three fail on 0.7.0. 1,136 of 1,136 tests pass (Windows,
  MSVC).

- **`nextLine` takes a buffered line without leaving the collector's
  quorum.** `__fdReadLine:` (and so `nextLine`, `nextLineMax:`, `linesDo:`,
  `lines` and `Stdio stdin nextLine`) first asks protoIO's
  `readBufferedLine`, which answers only from the read-ahead buffer and never
  reads or waits, and enters `blocking()` (the worker-pool accounting and the
  `UnmanagedScope`) only when a read is needed, as protoClojure's `line-seq`
  does (its M49). Semantics are unchanged: both calls take lines off the same
  buffer. protoIO's floor rises to **0.2.3** (CMakeLists.txt, INSTALLATION.md).
  Measured on a 115 MB, 1,446,319-line log with a grep-shaped script
  (counts verified on every run): `UnmanagedScope` entries per 25,078 lines
  fell from 25,097 to 51 (gdb hit count), but wall time did not move —
  medians of 5 interleaved rounds, stdin 6.25 -> 6.17 s, file 6.56 -> 6.83 s,
  within the rounds' spread — and `perf stat -r 3` shows -1.2 % instructions,
  -0.2 % cycles: protoCore 3.3.1 already made the scope's entry cheap. The
  benchmark suite (`compare_builds.py`, 5 runs) shows no regression. New
  tests: `conformance/14-io/files_lines_buffered.st` (lines across many
  refills with LF and CRLF, a line longer than the buffer, a last line
  without a newline, `nextLine` mixed with `next:`, `atEnd` and `upToEnd`,
  `nextLineMax:` refusing a buffered line) and three stdin cases in
  `cli_io`. 1,134 of 1,134 tests pass, also with
  `PROTOCORE_GC_CONTEXT_THRESHOLD=200`.
- **CI builds against protoCore 3.3.1** (tag `v3.3.1`, `5a8cd7c7`) on
  Linux, macOS and Windows; the floor job stays on 3.0.0.
- **CI builds against protoIO 0.2.3** (`7920332`): reading a line no longer
  moves the rest of protoIO's 64 KiB read buffer to its front, which
  `readLine` / `nextLine` paid once per line. No protoST code changed in
  that step. Full suite: 1,132 of 1,132 tests pass against it (protoCore
  3.3.0, Linux).
- **The five VM bugs found while writing `docs/VM.md`, fixed.** (1) A `super`
  send is now its own instruction: the compiler pushes `self`, the arguments,
  then `SEND_SUPER`, so `super foo: self bar` runs the inherited `foo:` with
  the override's `bar` (it ran the override of `foo:` with the inherited
  `bar`), and every part of a cascade to `super` is a super send (only the
  first was). `PUSH_SUPER` and the frame flag it armed are gone; `super` as a
  value is `PUSH_SELF`, and `super name(...)` is a compile error instead of a
  send to `self`. (2) An inlined `to:do:` evaluates its receiver once (it
  evaluated it again for the value of the expression). (3) An inlined
  `ifTrue:`, `ifTrue:ifFalse:`, `whileTrue:` or `to:do:` body, and a
  call-form default, may be of any length: a jump whose distance does not
  fit in a byte is widened with `EXTEND` prefixes (it was a compile error
  past 255 words). (4) A frame's operand stack is sized from the deepest
  point of its code, floored at the former 48 slots, so expressions nest as
  deeply as written (60 levels failed); an overflow in a hand-built module
  is a catchable `Error`. (5) A keyword message may have any number of
  parts (9 or more failed at run time, past every `on:do:`). Errors the
  engine raised as bare C++ exceptions during a send (a method's arity, a
  value attribute sent a keyword message, an unresolvable `super`) are
  catchable `Error`s now. Removed as dead: the opcodes `NOP`, `PUSH_SUPER`,
  `DUP_RECEIVER` and `HALT` (their numbers stay reserved), the constant
  kinds `BlockRef`, `NilK`, `TrueK`, `FalseK`, and the empty
  `Compiler::patchJumpToHere`; the engine's local-slot check reads the whole
  wide operand. `protost --disasm` prints `stack N (deepest D)`. New
  conformance programs: `05-messages/super-send-argument-is-a-self-send`,
  `super-cascade`, `send-with-many-arguments`,
  `send-many-arguments-not-understood-is-catchable` (replacing
  `send-more-than-8-args-error`), `06-blocks/to-do-receiver-evaluated-once`,
  `inlined-bodies-longer-than-255-words`, `03-grammar/deep-expression-nesting`.

- **`docs/VM.md`: the virtual machine, documented for contributors, and
  `protost --disasm`.** A reading order through the source (parser,
  compiler, bytecode module, engine, primitives); the instruction encoding,
  the frame layout, closures and captured variables, the calling
  conventions, safepoints, exceptions and contexts; all 47 opcodes (43
  emitted, 4 never: `NOP`, `SEND_SUPER`, `DUP_RECEIVER`, `HALT`) with
  operand, stack effect, semantics and the construct that emits them; 18
  worked examples; how to add an opcode and a primitive; and the oddities
  found on the way. `protost --disasm script.st` prints the bytecode of the
  script and of every block and method body, then runs it
  (`src/runtime/Disassembler.cpp`). Every listing in VM.md is real output:
  the sources and golden listings live in `docs/vm-examples/`, and the ctest
  `docs/vm-examples` fails when a listing, or VM.md's copy of it, drifts.
  Five bugs found while writing it are listed in VM.md §6 and not fixed
  here: `super foo: self bar` sends `bar` to the superclass and `foo:`
  normally; an inlined `to:do:` evaluates its receiver twice; an inlined
  body over 255 words is a compile error; expressions nested deeper than the
  48-slot operand stack, and sends of more than 8 arguments, fail with an
  error `on:do:` cannot catch.

- **VS Code extension 0.5.1: integration tests, and two launch fixes they
  found.** `editor-integration/vscode/test/` runs the extension in a real VS
  Code Extension Development Host (`npm install && npm test`; the extension
  README's "Testing the extension"): a local launch with a breakpoint, stack,
  a local and stdout; F5 without a launch.json; the executable lookup; a
  remote launch through WSL with pathMappings; the refusal without them; the
  `raised` exception filter; the launch schema and snippets. CI runs the
  local ones on Linux under xvfb. Fixed: a refused launch (no `program`, a
  bad `remote`, `remote` without `pathMappings`) waited for the user to close
  the error notification, so the launch stayed pending as long as it was not
  dismissed; it now ends at once. A `protost.path` naming a file that does
  not exist is refused with a message naming the setting, instead of a bare
  spawn error. The language's `firstLine` pattern (`^#!.*\bprotost\b`) was
  written with JSON `\b` escapes, which are backspace characters, so a
  `#!...protost` first line never selected the language; it is `\\b` now.
- **Windows: the REPL can be interrupted when it was started with Ctrl-C
  ignored.** A process started in a new process group (ctest, a background
  start, some IDE terminals) inherits "ignore Ctrl-C", and no handler then sees
  it: an endless evaluation in the REPL could not be stopped. The REPL now
  takes Ctrl-C back (`SetConsoleCtrlHandler(nullptr, FALSE)`), as on POSIX its
  SIGINT handler replaces an inherited SIG_IGN; a script keeps the inherited
  disposition on both. `cli_sigint` runs on Windows as
  `test_cli_sigint_windows.py` (a console of its own, GenerateConsoleCtrlEvent:
  a script ends with STATUS_CONTROL_C_EXIT, the REPL is interrupted and goes
  on), and tutorial chapter 15's `env.st` asks the platform's shell in its own
  syntax (`%PUMP_MODE%` in cmd.exe): Windows CI excludes no test.
- **DAP: remote debugging and sources without a local file**
  (docs/debugging.md "Remote debugging", "Sources without a file").
  `launch` takes `pathMappings` ([{localRoot, remoteRoot}], longest prefix,
  either separator, any drive-letter case) and `cwd`; the adapter translates
  every path both ways. A file under no mapping -- the protoST library of a
  remote protost -- comes back as a `sourceReference` served by the new
  `source` request, and breakpoints on it work. Breakpoints now resolve by
  file in every loaded module (kernel, imports; a later import arms them and
  sends a `breakpoint` event) -- before, every source's lines were looked up
  in the program. New `loadedSources` request and `loadedSource` events;
  kernel frames are deemphasized. Test: cli_dap_sources; tried end to end from
  Windows into WSL.
- **VS Code extension 0.5.0**: `remote: {command, path}` launches protost
  through ssh / wsl / docker exec (with `pathMappings`, required);
  configuration snippets "Launch over SSH" and "Launch in WSL";
  `extensionKind: ["workspace"]` for Remote - SSH / WSL / Dev Containers.
  docs/debugging.md also covers JetBrains IDEs through LSP4IJ.
- **DAP: conditions, hit counts and logpoints** (docs/debugging.md).
  The exception filters take a condition (`supportsExceptionFilterOptions`),
  a Smalltalk expression with the exception bound as `ex`
  (`ex isKindOf: ZeroDivide`); line breakpoints take a `condition`, a
  `hitCondition` (`N`, `== N`, `>= N`, `> N`, `< N`, `<= N`, `% N`; anything
  else is unverified) and a `logMessage` with `{expr}` interpolation. They are
  evaluated on the thread that reached the breakpoint, in its frame, with
  every stop suppressed and the program's handlers hidden; a condition that
  fails stops and says why (stop description and Debug Console).
- **DAP: an evaluation no longer reaches the program's handlers.** An error in
  a Watch / Debug Console expression evaluated on a thread stopped inside an
  `on:do:` was caught by that handler, which unwound the program's frames from
  inside the debugger; the thread's handlers are now hidden while it runs. An
  inlined `to:do:`'s counter shows under its block argument's name (`i`), not
  as a hidden compiler slot.
- **DAP: exception breakpoints** (docs/debugging.md). `initialize` advertises
  two `exceptionBreakpointFilters`: `raised` (off by default) stops when an
  exception is signalled, `uncaught` (on by default) when no `on:do:` handler
  on the thread will take it -- both before any handler runs or anything
  unwinds, so the stack, the locals and `evaluate` are intact. The hook is in
  `signal` (`signalInstance`), the one funnel of user signals, primitive
  errors, protoCore's errors and `doesNotUnderstand:`; protoST's control flow
  (block `^`, `on:do:` unwinding, `resume:` / `retry` / `pass`, cooperative
  yields, `halt`) is C++ unwinding and never stops. New requests
  `setExceptionBreakpoints` and `exceptionInfo`.
- **DAP: threads, one-thread stop.** Every thread that runs script code is a
  DAP thread (`main`, `worker N`, named after the actor it is running when it
  stops: `worker 2: actor Counter#1`), with `thread` started / exited events.
  A thread that stops stops alone (`allThreadsStopped: false`); several can be
  stopped at once; `continue` / `next` / `stepIn` / `stepOut` / `pause` act on
  the request's thread. Stepping state is per thread (the debugger's global
  mode is gone), the breakpoint table is safe to rewrite while threads run,
  and `variables` / `evaluate` run on the stopped thread that owns the values.
  New request `pause`.
- **DAP: real step over / step out**, by source line and call depth across
  every engine of the thread; the call stack now includes the frames of the
  blocks primitives run (`[] in Calc>>sumOf:`), and a caller frame shows the
  line of its send. `evaluate` sees the frame's variables (`doubled + n`).
  Locals show `self`, and a temporary shared with a block shows its value
  (it was `nil`) -- a block frame also shows the enclosing temporaries it
  uses; compiler temporaries and unnamed slots are hidden.
- **Breakpoints inside methods and multi-line blocks** stop there: the
  instructions that install a method, and the code around a block literal,
  were tagged with the body's last line and took the breakpoint first.
- VS Code extension 0.4.0 (documentation only).

- **A guest in the host's ProtoSpace** (INTEROP.md §4.0). New constructor
  `STRuntime(proto::ProtoSpace& hostSpace)` runs protoST in a space another
  runtime created -- one ProtoSpace per process, protoCore's recommended model
  -- instead of building its own. The host keeps its primitive prototype slots,
  error factory, heap policy and resolution chain (protoST appends
  `provider:st`); protoST resolves its own sends to primitive values through its
  own prototypes (`methodHolder`), and an import or a send from host code
  crosses the boundary as a foreign one, on the caller's own context, from any
  thread of the space. `STRuntime::ownsSpace()` tells the two shapes apart.
  New test: `Guest: a protoST runtime runs in its host's ProtoSpace`. protoScala's
  interop tests now host protoST this way (one space in the process). CI builds
  against protoCore 3.1.0 (master, 267b9fc9); the declared minimum stays 3.0.0.

## 0.7.0 — on protoCore 3.0.0 (2026-10-22)

**Requires protoCore 3.0.0** (SOVERSION 4), the declared minimum for a build from source; the `.deb` depends on `protocore (>= 3.0.0)`. CI pins protoCore tag `v3.0.0` on every job.

- **Another runtime can send protoST messages** (INTEROP.md §4.3). `STRuntime`
  installs one protoCore method on `Object`, `__st_send__`: `self` is the
  receiver, the first positional argument is the selector, the rest are the
  message's arguments, and the message runs as a SEND (class-side isolation,
  `doesNotUnderstand:`, primitives). A caller in another `ProtoSpace` runs it on
  protoST's current context, from the thread that constructed the runtime only;
  the handlers of protoST code above the send are hidden while it runs, and an
  Error it does not handle leaves as a `proto::ProtoException` carrying the Error.
  protoScala uses it to call protoST objects from Scala. Tests:
  `tests/unit/test_cross_runtime_send.cpp`.

- **protoCore 3.0.0 unified exceptions** (LANGUAGE.md §8.7). Every language
  on protoCore raises `proto::ProtoException` carrying the thrown value and
  selects handlers by the value's prototype chain; protoST now takes part:
  - A `ProtoException` reaching a native boundary (a primitive, a module
    method, a module load) is **signalled with its value unchanged**. A
    module's error type is caught by naming it, `[geo circleArea(-1)] on:
    geo NegativeRadius do: [...]`, and the handler receives the raised
    value. Such a foreign exception is not an `Error` (`on: Error do:` does
    not select it); `on: Exception do:` is the one catch-all form and
    catches every signalled value, including a raised non-exception value
    such as a SmallInteger. A foreign exception is never resumable, answers the Exception protocol
    (`messageText`, `return:`, `retry`, `pass`, ...) while its handler runs,
    and, unhandled, leaves the runtime as the very `ProtoException` raised.
    Before, any `ProtoException` became a plain `Error` with the text
    `native exception`.
  - **protoCore's own errors arrive as protoST classes**, through the error
    factory protoST installs: `DivisionByZero` -> `ZeroDivide`, `Overflow`
    -> `ArithmeticError`, `InvalidArgument` and `TypeMismatch` -> the new
    kernel class `InvalidArgument` (an `Error`), `ArityMismatch` -> `Error`.
    protoST's numeric primitives raise their division by zero the same way
    (`ZeroDivideSignal` is gone).
  - **An unhandled `Error` leaves as a `ProtoException`** carrying the
    exception object: `UnhandledSTException` derives from
    `proto::ProtoException` (it keeps its message, trace and actor flag) and
    is no longer a `std::runtime_error`. Embedders catch
    `const proto::ProtoException&`; `describeUncaught(ctx, e)` formats it.
    The actor `__inflight_exception__` attribute is gone (protoCore pins an
    exception's value while it is in flight).
  - A C++ `std::exception` from a primitive or a module method is still a
    plain `Error` with `what()` as its text.
  - The control-flow carriers `NonLocalReturn`, `UnwindToHandler` and
    `ResumeSignal` pin the value they carry across the unwinding
    (`src/runtime/PinnedValue.h`); `ensure:` blocks that collect can no
    longer reclaim it. They are deliberately not `ProtoException`s.
  - Top levels report any exception instead of terminating: the worker
    loop, the actor drain (`catch (...)` added), Future callbacks, the
    REPL, the debugger, the DAP server, `-e`, `-d` and scripts. A script's
    uncaught exception is reported while the runtime still exists.
- **Call-form sends to protoCore methods.** `geo circleArea(2)` calls a
  `proto::ProtoMethod` a module exports (positional and named arguments)
  through protoCore's array call form; before, it answered
  `doesNotUnderstand`.
- **Tests:** `tests/unit/test_unified_exceptions.cpp` (the rule-14 contract
  from C++, the factory, a module error raised inside its own handler, and
  collections run inside `ensure:` while a `NonLocalReturn`,
  `UnwindToHandler`, `ResumeSignal` or foreign exception is in flight; the
  first three fail with the pin removed), and
  `tests/umd/`, Smalltalk programs that import protoCore's example geometry
  module through `protost_umd_host` (`tests/umd/umd_host.cpp`, which links
  the module's shared library and registers its provider). Built when the
  protoCore build provides `libprotocore_example_geometry`.

- **Built against protoCore 3.0.0** (SOVERSION 4), now the declared minimum:
  protoCore's methods receive their arguments as `ProtoArguments` /
  `ProtoKeywordArguments` descriptors (protoCore
  `docs/CALLING_CONVENTION.md`).  protoST's primitives have their own calling
  convention, so only the three thread entry points (`STRuntime`'s workers,
  the DAP server's thread, a unit test) change signature.  CI pins protoCore
  3.0.0 on every job.

- **Performance: inlined blocks do not capture.** The bodies of `ifTrue:`,
  `whileTrue:` and `to:do:` that the compiler inlines no longer count as
  capture scopes, so the temporaries they use stay in the frame instead of
  the method's mutable captured dictionary. A `to:do:` whose loop variable
  shadows a captured temporary hides it for the body only. Two conformance
  programs pin the semantics; `test_cli_gc_recursion_reclaims` now
  allocates an Array per call, since `fib` itself no longer allocates.
- **Performance: the send path.** Engine markers are read as own
  attributes, interned symbols are not pinned, frame ids come from a
  per-thread block, the bootstrap and debugger are engine members.
- **Measured** (`benchmarks/reports/2026-10-20-send-path-inline-captures.md`,
  protoCore 2.14.8, against 0.6.6 on 2.14.7): geomean ratio to CPython
  3.25× -> 1.92×; `fib` 8.9× -> 2.15×, `attr_lookup` 4.2× -> 1.28×,
  `range_iterate` 2.2× -> 1.03×; `parallel_speedup` 2.05× -> 2.63×.

## 0.6.6 — scripts as commands (2026-10-17)

- **Package:** the `.deb` is built in a clean Ubuntu 24.04 container with
  GCC 13 against protoCore 2.14.6 and protoIO 0.2.2, and declares
  `protocore (>= 2.14.6)`, as 0.6.5 did. `ctest`: 1105 cases (544
  conformance programs, 462 unit tests, 44 examples, 33 CLI tests, 22
  documentation checks), all passing there.
- **Scripts as commands.** A leading `#!` interpreter line is skipped, so a
  script starting with `#!/usr/bin/env protost` and made executable runs as
  a command and chains in pipelines with other commands (`LANGUAGE.md`
  §13.3, tutorial §15.3). The lexer skips the line wherever source is read,
  and keeps its newline, so diagnostics name the file's real line numbers.
  Before, such a file failed to parse (`malformed symbol literal`).
- **Examples:** `examples/unix/column.st` and `sum.st`, two executable
  filters (`printf 'pump-1 12\npump-2 30\n' | ./column.st 2 | ./sum.st`
  prints 42).
- **Tests:** a lexer unit case, and `tests/cli/test_cli_shebang.sh`, which
  runs the two examples as commands in real pipelines (exit statuses, an
  early `head`). The conformance runner reads the directive from the second
  line when the first is `#!`, and runs every program with an empty standard
  input, so a filter never waits on the terminal.

## 0.6.5 — on protoCore 2.14.6 (2026-10-12)

- **The `.deb` requires protoCore 2.14.6** (`protocore (>= 2.14.6)`), so a
  fresh install gets its faster integer arithmetic and its bounded mark work
  list; CI and the cross-platform workflow build against 2.14.6 (tag
  `v2.14.6`, `a6400cd`). Built from source the floor stays at 2.11.0. No
  code change in protoST: the version string and the documents only.
- **Measured** (`benchmarks/reports/2026-10-12-protocore-2.14.6.md`: the
  0.6.4 release binary on the two protoCore release libraries, 14
  interleaved runs each with A/A pairs, `perf stat`, CPython 3.14 alongside):
  the talk's demo 2 runs its one-thread half 45 % faster and its twelve
  actors 35 % faster, so the speedup it prints falls from a median of 4.4×
  to 3.8×; `exception_latency` is 7 % faster and
  `parallel_speedup` with the pool 8 %; the other workloads are unchanged
  within the noise, with 1-3.5 % fewer instructions. Under the fixed 640 MB
  limit, `(1 to: 500000) asArray` peaks at 690 MB of RSS instead of
  1,165 MB (`KNOWN_ISSUES.md` K5).
- **Talk materials and figures** (README "Performance", `docs/STATUS.md`,
  the FAST talk's scripts, notes, hard questions, decks and PDFs) quote that
  report; the hard questions explain why demo 2's ratio is lower.
  `benchmarks/compare_builds.py` gained `--libdir LABEL=DIR` (one binary,
  several protoCore libraries, `ldd` checked before every run).
- Test suite: 1101 cases on Linux, all passing against protoCore 2.14.6 (the
  `.deb`'s build in a clean Ubuntu 24.04 container with GCC 13, no
  warnings); CI green on the release branch on Linux (1100 gating cases,
  also against protoCore 2.11.0), macOS (1100) and Windows (1097)
  (CI 38306148733, cross-platform 38306152297; on `main` at the tag, CI
  38318673607 and cross-platform 38318673623).
- Verified in clean, minimized `ubuntu:24.04` containers: a fresh install
  with the talk checklist's exact commands (GitHub URLs of protoCore 2.14.6
  and protoST 0.6.5) and an upgrade from 0.6.4 with protoCore 2.14.5; `ldd`
  resolves `libprotoCore.so.3` to the installed 2.14.6 in both. Then, with
  `--network none`, a read-only root filesystem and an unprivileged user,
  all four talk demos 10/10 from `/usr/share/protoST/talks/2026-10-15-fas/demos`
  in both, `run_demo.sh`'s version-mismatch message and the missing-kernel
  exit 72. Offered alone on top of protoCore 2.14.5, `dpkg` refuses the
  package (`protost depends on protocore (>= 2.14.6)`). Release:
  https://github.com/gamarino/protoST/releases/tag/v0.6.5 (0.6.4 marked
  superseded).

## 0.6.4 — the talk demos where every installation keeps them (2026-10-12)

- **The talk's runnable material moved to `/usr/share/protoST/talks/`.**
  0.6.3 installed it under `/usr/share/doc/protoST/talks/`, and minimized
  Ubuntu images (the `ubuntu:24.04` Docker image, cloud and minimal server
  images) carry `path-exclude=/usr/share/doc/*` in dpkg's configuration: the
  files were listed by `dpkg -L` but never written. The release verification
  of 0.6.3 in a clean container found it. Standard desktop and WSL
  installations were not affected. The Markdown talk documents stay under
  the documentation directory. No code change: the runtime is 0.6.3's.
- Test suite: 1101 cases on Linux, all passing; CI green on Linux (1100
  gating cases, also against protoCore 2.11.0), macOS (1100) and Windows
  (1097) (CI 38280026970, cross-platform 38280027024). The `.deb` was built
  in a clean Ubuntu 24.04 container with GCC 13 from `eee2708` (no warnings;
  1101/1101 there).
- Verified in clean `ubuntu:24.04` containers (minimized images, the case
  that broke 0.6.3): a fresh install from the GitHub URLs exactly as the talk
  checklist gives them, and an upgrade from 0.6.2; `ldd` resolves
  `libprotoCore.so.3` to the installed protoCore 2.14.5; then, with
  `--network none`, a read-only root filesystem and an unprivileged user, all
  four talk demos 10/10 from `/usr/share/protoST/talks/2026-10-15-fas/demos`
  in both, the version-mismatch message of `run_demo.sh` and the
  missing-kernel exit 72. Release:
  https://github.com/gamarino/protoST/releases/tag/v0.6.4 (0.6.3 and 0.6.2
  marked superseded).

## 0.6.3 — transient pins released, kernel check, demos in the package (2026-10-12)

- **The threaded dispatch did not release transient pins** (found by the
  pre-talk audit through GCC 15's 1081 "jump to label ... does not destroy"
  warnings on `ExecutionEngine.cpp`). A computed goto runs no destructor, and
  23 dispatch points (every SEND, PUSH_BLOCK, the global and class-variable
  accesses) left a `TransientPin` alive that way, so the thread's scratch
  cursor never came back down: the 256 scratch slots were used up while the
  kernel loaded, and from then on `TransientPin` pinned nothing in a Release
  build (its overflow check is an `assert`). It affected the GCC builds, that
  is the Linux packages and Linux CI; Clang and MSVC use the switch dispatch,
  whose `goto` runs destructors. No crash has been traced to it. Those points
  now leave through an ordinary `goto` to the dispatch loop's head
  (`DISPATCH_SCOPED`); the build has no warnings left, so a new instance would
  show. Test: `TransientPin: every instruction releases its pins` (read 255
  against 0 before). Cost: `exception_latency` +13 % wall (+6 % cycles: the
  C++ unwinder searches `runLoop`'s call-site table further; the same
  exceptions are thrown); the other workloads are unchanged within the noise
  in cycles (`benchmarks/reports/2026-10-12-pre-talk-063.md`).
- **protost refuses to start without its kernel.** When the standard
  library directory has no `kernel/00-manifest.txt` (a copied binary, a wrong
  `PROTOST_LIB`), every mode that runs code (`script`, `-e`, `-d`, `-i`,
  `--dap`) now stops with "cannot find the protoST kernel", what was
  searched, and exit status 72; before, it ran with the primitives alone and
  failed later on an undefined `Transcript`. `STRuntime::kernelManifestPath`
  answers the question for embedders, whose runtime still starts without a
  kernel. Test: `cli_kernel` (fails on 0.6.2).
- **The talk demos are in the package**, under
  `/usr/share/doc/protoST/talks/2026-10-15-fas/`: the programs, the demo-4
  feed, the expected outputs, `run_demo.sh` and `check_all.sh` (executable)
  and the slide PDFs. `run_demo.sh` now runs the `protost` on `PATH` first
  and the source tree's `build_release` only when there is none (it picked a
  stale build before), names the binary it picked when the version does not
  match and how to choose another (`PROTOST=`), and writes nothing next to
  itself, so it runs from the read-only installed copy. Demo 2 prints its own
  speedup (`time speedup: N.Nx`).
- **S22 was a test race, now fixed.** Two Track Y unit tests wrote the same
  module file in the shared working directory; when ctest ran them at once,
  one truncated the file while the other loaded it, and the module came back
  without `Counter` (seen on CI on 2026-09-30 and again on 2026-10-12, on the
  protoCore 2.11.0 floor job). Reproduced locally at about 3 % with six
  copies in parallel (0.6.2 and 0.6.3 alike); each process now writes its own
  file, 0/600 after.
- **S19's test gates CI again.** `cli_concurrent_first_call` had been kept
  out of the gate since S19 (fixed in `06e1009`); it passed on the first
  attempt in all 59 CI runs since, and 150 times in a row locally (6,000
  launches, three runs at a time).
- **Measured and documented** (`benchmarks/reports/2026-10-12-pre-talk-063.md`):
  demo 2's lower speedup in an Ubuntu 24.04 container is not the container:
  the one-thread half runs about 10 % faster with glibc 2.39 than with the
  host's 2.43 (malloc, for the temporaries protoCore's integer comparison
  allocates), and the twelve actors take the same time. Under the old fixed
  limit (`PROTOCORE_ADAPTIVE_HEAP=0`) a process peaked at 1.19 GB resident
  with 640 MB of cells: the rest is the collector's mark work list, a vector
  outside the cell heap (K5).
- Docs: README "Performance" rewritten from the 2026-10-12 report (and a CI
  badge); the talk materials (es/en) corrected after the audit: figures and
  their sources, interoperability stated in the one direction that is
  proven, past tense for the 640 MB limit, uncapped peak memory and how to
  cap it, test counts per platform, decks (Spanish source committed as
  `deck.html`) and speaker notes extracted per language.
- Test suite: 1101 cases on Linux, all passing; CI green on Linux (1100 gating cases, also against protoCore 2.11.0), macOS
  (1100) and Windows (1097) (CI 38276967742, cross-platform 38276967598).
  The `.deb` was built in a clean Ubuntu 24.04 container with GCC 13 (no
  warnings; 1101/1101 there).

## 0.6.2 — the adaptive heap controller is the default (2026-10-12)

- **Memory policy: protoCore's adaptive heap controller by default**,
  replacing the fixed limit of 10M cells (640 MB). `STRuntime::configureHeap`
  calls `ProtoSpace::enableAdaptiveHeap()`, as protoPython, protoScala and
  protoClojure do: the budget is 75 % of physical memory or of the process
  memory limit, and a soft limit inside it (128 MB at start) grows only while
  the program waits for the collector. The environment still chooses a fixed
  limit; precedence: `PROTOCORE_ADAPTIVE_HEAP=1` (controller, with
  `PROTOCORE_HEAP_LIMIT_CELLS` as its budget), then
  `PROTOCORE_HEAP_LIMIT_CELLS` (a fixed limit of that size, `0` for none),
  then `PROTOCORE_ADAPTIVE_HEAP=0` (0.6.1's fixed 10M-cell limit), then the
  controller. The out-of-memory message names the limit in force. Documented
  in `KNOWN_ISSUES.md` K5 and `docs/INSTALLATION.md` ("Memory").
  Tests: `HeapPolicy` (five unit cases; the default one failed before) and
  `cli_oom_clean`, which now also runs under the controller.
- **K6: engine slot arrays sized on demand.** protoCore's stop-the-world
  root scan visits every slot of every thread's `automaticLocals`, and protoST
  pre-sized that array to 1 M slots (8 MB) for every thread that ran code, so
  each pause cost 4 ms with one thread and 25-31 ms with twelve workers. The
  fixed limit hid it by rarely collecting; with the controller alone,
  `parallel_speedup` with the full pool ran 14 % slower than on 0.6.1. The
  array now starts at 4,096 slots and doubles on demand up to the same
  maximum (the recursion depth limit is unchanged; the transient-pin scratch
  region moved to the bottom of the array). Longest pause on the benchmark
  suite: 0.27 ms. No protoCore change. Test: `EngineSlots`.
- **Measured** (`benchmarks/reports/2026-10-12-adaptive-heap-default.md`,
  0.6.1 against 0.6.2, same compiler and protoCore, medians of 7
  interleaved runs with A/A pairs, `perf stat` cycles and instructions, and
  collector pauses): faster on every workload but `attr_lookup` (unchanged):
  `parallel_speedup` -27 % with the full pool and -28 % on one worker,
  `message_throughput` -41 %, `saturation_big` -16 % (one worker) to -43 %
  (six), start-up -16 %; a fifth to a half of the memory. The talk's demo 2
  shows a median 4.45x instead of 4.3x. Costs: the collector now runs on
  `fib` and `exception_latency` (+18 % and +8 % CPU, still faster in wall
  time), and peak memory is no longer capped at 640 MB (an allocating
  3M-iteration loop: 0.88-3.5 GB, against 677 MB). `(1 to: 500000) asArray`,
  which ran out of memory under the fixed limit, now succeeds (K5).
- **Three tests fixed that the controller exposed** (none a runtime defect):
  the IvarGroups atomicity test's C++ reader thread spun without reaching a
  safepoint, so the first stop-the-world could never start (it now calls
  `ProtoContext::safepoint()`); the S15 test missed cycles that ran during a
  program and failed once on CI (it now observes every cycle and prints them
  on failure; the CI failure could not be reproduced locally);
  `cli_buried_actor_deadlock` assumed the single worker had started within a
  fixed 100 ms (it now waits for a flag).
- `benchmarks/compare_builds.py` also records user and system CPU time.
- The talk demos run under the default policy (`run_demo.sh` no longer sets
  a 20M-cell limit); the decks state the current test count.
- Test suite: 1100 cases on Linux, all passing; CI green on Linux (also
  against protoCore 2.11.0), macOS and Windows (CI 38259886339,
  cross-platform 38259890608).

## 0.6.1 — a recursion's garbage is reclaimed (2026-10-11)

- **S24: a program that only recurses never had its garbage reclaimed.**
  protoST hands a context's young generation to protoCore's collector at a
  safepoint (S15), which ran only at a loop back-edge and at engine entry. A
  send or block call within the same engine is neither, so the textbook
  `fib` (sends and an inlined `ifTrue:`, no loop) submitted nothing after
  start-up: `fib 29` ran out of memory under the default 640 MB limit ("the
  last collections reclaimed nothing", exit 3, also on 0.5.0 and 0.6.0), and
  under protoCore's adaptive controller `fib.st` took three times as long,
  every cycle walking millions of pinned cells and reclaiming none. Call
  entry is now a safepoint too (right after the callee's frame is pushed,
  every live value is in a traced slot). Cost on `fib.st` under the fixed
  limit: within the A/A noise. Under the controller `fib.st` takes 0.40 s
  instead of 1.19 s and 155 MB instead of 593 MB. Test:
  `cli_gc_recursion_reclaims` (failed before). Report:
  `benchmarks/reports/2026-10-11-fib-under-adaptive-controller.md`. The
  fixed heap limit stays the default (K5); the measurement says the reason
  given for it no longer holds.
- **The 4-6 % actor "regression" of 0.6.0's notes, bisected**
  (`benchmarks/reports/2026-10-11-actor-regression-bisection.md`): on a quiet
  machine, with every compared build made by one compiler, no wall-time
  difference beyond the noise floor (A/A pairs differ by 1-4 %). protoCore
  2.12.0 and later run one more (paced) collection on `saturation_big`, about
  4 % more CPU on the collector's core, which competes with the mutator only
  when every core is busy, as in the original sessions; plausible, not
  proven. No change made.
- `ctest`: 1094 cases on Linux, all passing.

## 0.6.0 — native Windows, write groups, protoCore 2.14.5 (2026-10-11)

What changed since 0.5.0, in brief (the entries below give the detail, newest
first):

- **protoCore 2.11.0 is the floor** (it was 2.6.1); the Linux `.deb` is built
  against protoCore 2.14.5, the release to install with it. A machine with
  the protoCore 2.6.2 package of 0.5.0 must upgrade protoCore too.
- **Native Windows build (MSVC)** and a self-contained Windows ZIP, built and
  tested in CI; the GitHub release attaches only the Linux `.deb`.
- **Runs of instance-variable assignments are published as one version**
  (`setAttributes`): fewer cells per construction, and another thread sees
  all of a run or none of it.
- **I/O moved onto protoIO** (a build-time static library; no behaviour change
  for correct programs, a few stricter edge cases listed below).
- **Fixes:** S23 (a chain of re-signalling handlers at the nesting limit now
  finishes), unwinds no longer re-throw inside a catch clause, `OSProcess
  shell:` hands the command line to the shell verbatim, REPL Ctrl-C cancels
  the line, `venv create` from an installed `protost` writes its activation
  scripts; new: `protost -` and `PROTOST_REPORT_PEAK_RSS`.
- **Performance** against 0.5.0 (report of 2026-10-11): the same instructions
  within 1 % single-threaded, about 12 MB less peak memory on several
  workloads; allocation-heavy actor workloads on one worker were measured
  4-6 % slower, under investigation at release time (see 0.6.1).
- `ctest`: 1093 cases on Linux, all passing; CI green on Linux (including the
  protoCore 2.11.0 floor job), macOS and Windows.

- **Re-measured on protoCore 2.14.5** (`benchmarks/reports/2026-10-11-protocore-2.14.5.md`,
  raw data in `benchmarks/reports/data/2026-10-11/`): 0.5.0 on protoCore 2.6.2,
  the 2026-10-03 build on 2.10.2 and `main` on 2.14.5, interleaved, medians of
  3, synthetic, one notebook-class machine. Single-threaded speed is unchanged
  (the same instructions within 1 %); peak memory is about 12 MB lower on
  several workloads; allocation-heavy actor workloads on one worker are 4-6 %
  slower on `main`, cause not isolated. New `benchmarks/compare_builds.py`
  runs several builds interleaved with every run verified. README
  "Performance", the tutorial timings of chapters 10 and 13 and the FAST talk
  materials use the new figures.
- **Decision: protoST keeps its fixed heap limit and does not enable
  protoCore's adaptive heap controller** (2026-10-04, kept with protoCore
  2.14.5). `STRuntime::configureHeap` still sets a hard limit of 10M cells
  (640 MB, at most a quarter of physical memory; `PROTOCORE_HEAP_LIMIT_CELLS`
  overrides it). Under the controller `fib.st` ran about three times slower:
  protoST's start-up live set (about 1.7 M cells) is just below the
  controller's initial soft limit (2 M cells), so a short program pays a
  collection that finds almost no garbage and another waited for at exit;
  protoCore 2.14.2's live-set floor (adjustment A3) reduced but did not
  remove the gap (protoCore `docs/reports/2026-10-04-final-remeasurement.md`,
  `docs/reports/2026-10-04-collector-adjustments.md`). To be revisited, for
  example with an initial soft limit above the start-up live set. Recorded in
  `docs/STATUS.md` and `KNOWN_ISSUES.md` (K5).
- **Documentation brought up to date with `main`.** `docs/STATUS.md` (test
  suite of 1093 cases, protoCore floor and memory policy, commit SHAs for
  S21, D12b and D10), `KNOWN_ISSUES.md` (K4: the process-global mutable
  table shipped in protoCore 2.6.0), `docs/INTEROP.md`, `docs/ROADMAP.md`,
  `docs/CONFORMANCE.md`, `docs/INSTALLATION.md` and `README.md` (the 0.5.0
  packages against building the current sources).

- **CI builds protoCore 2.14.5.** Every push and pull request job (Linux,
  macOS, Windows) now builds protoCore 2.14.5 (tag `v2.14.5`, `063c6d5`)
  instead of 2.14.2: the controller's out-of-memory rule uses the live set and
  the collector's registries outlive static destruction (2.14.4); an embedder
  exits LeakSanitizer-clean (2.14.5). The floor job stays on 2.11.0 and protoIO
  on 0.2.2. No source change was needed: the suite passes 1093/1093 locally
  against 2.14.5 (ldd-verified).
- **CI builds protoCore 2.14.2.** Every push and pull request job (Linux,
  macOS, Windows) now builds protoCore 2.14.2 (tag `v2.14.2`, `17bc118`)
  instead of 2.14.1: the collector adjustments of 2.14.2 (the multi-cursor
  sweep engages only when it pays; the adaptive controller's live-set floor).
  The floor job stays on 2.11.0 and protoIO on 0.2.2. No source change was
  needed: the suite passes 1093/1093 locally against 2.14.2 (ldd-verified), and CI on the branch is green (CI
  38099981622, cross-platform 38099983373). protoST still does not enable protoCore's adaptive heap: under the
  controller `fib.st` remains about three times slower than under protoST's
  fixed 640 MB limit (protoCore's collector-adjustments report, A3), so the
  fixed limit stays the default.

- **CI builds protoCore 2.14.1.** Every push and pull request job (Linux,
  macOS, Windows) now builds protoCore 2.14.1 (tag `v2.14.1`, `9495155`)
  instead of 2.12.0: the collector's pacing and multi-cursor sweep (2.12.0),
  its control law (2.13.0), the sweep's helper threads (2.14.0) and the
  thread-exit quorum fix (2.14.1). The floor job stays on 2.11.0 and protoIO
  on 0.2.2. No source change was needed: the suite passes 1093/1093 locally
  against 2.14.1 (ldd-verified), and CI on the branch is green (CI
  37169021888, cross-platform 37169023319). protoST does not enable
  protoCore's adaptive heap.

- **Runs of instance-variable assignments are published as one version;
  protoCore 2.11.0 is the floor.** Every assignment to an instance variable
  publishes a new version of `self` into protoCore's mutable table (a new
  snapshot plus a path copy in the table). A run of assignments in a method
  (`x := ax. y := ay. z := 0.`) whose values after the first can neither run
  code nor read a variable the run already assigned is now compiled as one
  group (`IVAR_GROUP` ... `IVAR_GROUP_END`) and published once with
  `ProtoObject::setAttributes` (protoCore 2.11.0): read the current snapshot
  once, derive the new version, one compare-and-swap. The object model and
  the program's meaning are unchanged; another thread now sees all of a run
  or none of it.
  - *What is grouped* (`Compiler::tryEmitInstVarGroup`): two to 16
    consecutive statements assigning instance variables (instance or class
    side); after the first, values may be literals, `self`, temporaries,
    arguments, captured variables, instance and class variables not yet
    assigned in the run, and `+ - < <= > >= =` on them. A send, a global (it
    can be undefined and signal an Error), a block, a nested assignment or a
    read of a variable the run assigned ends the run.
  - *Guards* (`ExecutionEngine.cpp`, "Instance-variable write groups"): with
    a debugger attached (`protost -d`, DAP) every assignment is published on
    its own; a SmallInteger operator that misses its fast path (another kind
    of number, an overflow) writes the run's pending assignments one by one
    before it sends its message, and the rest of the run continues per
    write. If a thread is terminated in the middle of a run, which of its
    assignments survive is indeterminate. `PROTOST_IVAR_GROUPS=off` turns
    runs off.
  - *Measured* (`P5 new setId: i`, five assignments, 50,000 constructions,
    no collection during the measurement; Release, protoCore 2.12.0): 72 to
    74 cells per construction per write, 32 grouped. Statically, 69 of the
    167 instance-variable assignments in `lib/`, `examples/` and
    `benchmarks/` (83 files) fall in 25 runs.
  - *Tests*: `tests/unit/test_instvar_groups.cpp` (17 cases, `[ivargroups]`):
    which runs are formed and what ends them; each program's answer with
    runs and with the per-write path; a `+` that misses and sends a message
    that reads the receiver (fails if the pending writes are not published
    first); an Error in a run's value (fails if a global is allowed in a
    run); cells per construction; and a protoCore thread taking snapshots of
    an object while this thread runs `a := k. b := k. c := k.` 200,000
    times: no torn snapshot with runs, about 13 million of 19 million torn
    per write. Suite: 1093 `ctest` cases on Linux, all passing.
  - CI builds protoCore 2.12.0 (tag `v2.12.0`, `f969d15`); the floor job
    builds 2.11.0 (`69b56af`). The developer fallback (`../protoCore`)
    refuses a tree whose `protoCore.h` has no `setAttributes`.

- **CI builds protoCore 2.10.2.** Every push and pull request job (Linux,
  macOS, Windows) now builds protoCore 2.10.2 (tag `v2.10.2`, `b7f6d82`)
  instead of 2.9.4; the floor job stays on 2.7.0 and protoIO on 0.2.2. No
  source change was needed: the suite passes 1076/1076 locally against
  2.10.2. protoST does not enable protoCore's adaptive heap.

- **CI builds protoCore 2.9.4 and protoIO 0.2.2; protoIO 0.2.2 is the floor.**
  Every push and pull request job (Linux, macOS, Windows) builds protoCore
  2.9.4 (tag `v2.9.4`, `9cb0ef5`); one Linux job, the only floor job, builds
  2.7.0 (`fc5d79d`), the minimum `CMakeLists.txt` declares. protoIO is pinned
  to 0.2.2 (`1611ff9`), and `find_package(protoIO 0.2.2)` (also checked
  against a sibling `../protoIO`) now requires it, as protoScala and
  protoClojure do: its listener on a host-less address is dual-stack.

- **S23: a chain of re-signalling handlers finishes at the engine nesting
  limit.** A handler block runs in a nested engine of its own; at the limit
  (1,000 nested engines) it could not start, and the limit error then
  crossed every primitive boundary below as a raw C++ exception, each one
  translating it into a new `Error` and running again the handlers the
  unwind had already left. The handler runs doubled per level, so a chain of
  about 500 handlers that each signal a new `Error` (or `pass`, `outer`,
  `signal`, `signal:`) did not finish. Now `signal` checks for room before it
  runs a handler; without room it raises the limit's documented `Error`
  ("stack depth exceeded (nested block evaluation)"), unwinds to the
  `on:do:` whose handler catches it (`NestingLimitUnwind`) and runs that
  handler there. Each handler runs at most once; 499 to 10,000 levels finish
  in about 0.1 s. Tests: `08-exceptions/nesting-limit-in-re-signalling-handler-chain`,
  `08-exceptions/nesting-limit-in-passing-handler-chain`;
  `08-exceptions/unwind-through-nested-handler-blocks` is back to 498 levels.
  Suite: 1076 `ctest` cases on Linux (544 conformance programs).

- **The Windows-port review, 2026-10-02.**
  - *protoIO 0.2.1 is the floor* (`find_package(protoIO 0.2.1)`; a sibling
    `../protoIO` is checked against it too). 0.2.0 refuses `.bat`/`.cmd`
    targets in `OSProcess run:` on Windows (an `Error`; cmd.exe would
    re-parse their arguments), answers 128 + signal for a crashed child,
    honours timeouts on console reads and lets open files be deleted or
    renamed; 0.2.1 adds `process::shell`. CI pins protoIO `c97a1ad`.
  - *`OSProcess shell:` and `command:` hand the command line to the shell
    verbatim.* On Windows they ran `cmd.exe /c` with C-runtime argument
    quoting, which cmd does not understand (`echo "a b"` printed
    `\"a b\"`); they now run `cmd.exe /d /s /c "<command>"`. Unchanged on
    POSIX.
  - *An unwind no longer re-throws inside a catch clause anywhere.* The last
    three sites -- the signal loop's handler block, an awaiting actor's
    helped message and a module's top level -- now re-throw after the clause,
    like the rest since the port.
  - *REPL: Ctrl-C at the prompt cancels the line* (and a multi-line form
    being entered) on every platform; before, POSIX kept the line and a
    second Ctrl-C at the prompt killed the session, and Windows ended the
    session. On Windows the console is read with `ReadConsoleW`, so
    non-ASCII input arrives intact (it arrived as NULs with code page 65001
    on the Windows 10 console host). A second Ctrl-C while the first is
    pending ends the process with `STATUS_CONTROL_C_EXIT` on Windows, as
    documented, instead of status 3.
  - *`protost -`* runs a script read from standard input, on every platform.
  - *`PROTOST_REPORT_PEAK_RSS=1`* prints the process's peak resident memory
    (peak working set on Windows) on stderr at exit; `cli_memory_bounded`
    checks the same 1 GB bound with it everywhere, instead of only the
    result on Windows.
  - *venv:* `venv create` writes `activate`, `activate.fish`,
    `Activate.ps1`, `activate.bat` and `deactivate.bat` on every platform,
    from templates compiled into `protost` (an installed `protost` wrote
    empty files: it read them from the source tree).
  - *Windows packages are self-contained:* protoCore's DLL (taken from the
    imported target, whatever its name), the OpenSSL 3 DLLs named exactly
    (no glob) with OpenSSL's Apache-2.0 licence, and the MSVC runtime
    (app-local, `InstallRequiredSystemLibraries`) go next to `protost.exe`;
    NSIS is a CPack generator only when `makensis` is found. CI unpacks the
    ZIP into an empty directory and runs it with only the Windows
    directories on `PATH`.
  - *MSVC:* `/W3` applies to protoST's own targets only, and the Windows CI
    job builds them with `/WX` (`PROTOST_WARNINGS_AS_ERRORS`).
  - `:load` and the DAP launch read files without `ftell` (32 bits on
    Windows); `utf8.manifest` no longer says protoScala.
  - `cli_kernel` takes the process launch off the start-up time on every
    platform, not only under Git Bash. Every remaining Windows difference,
    in behaviour and in the tests, is listed in `docs/INSTALLATION.md`,
    "Windows (MSVC)".
  - New tests: `cli_repl_console`, conformance
    `14-io/os_process_shell_verbatim`, `14-io/os_process_batch_file_refused`,
    `08-exceptions/unwind-through-nested-handler-blocks`,
    `08-exceptions/nesting-limit-through-handlers-is-catchable`,
    `07-non-local-return/nonlocal-return-through-many-handlers`.
  - Found while testing, not fixed: S23 (`docs/STATUS.md`), a handler chain
    that re-signals at the engine nesting limit does not finish.

- **Native Windows build (MSVC), 2026-10-01.** protoST builds with Visual
  Studio 2022 against an installed protoCore and the sibling protoIO; `protost`
  runs scripts, `-e` and the REPL from `cmd.exe` or PowerShell and installs with
  `cmake --install` (or `cpack -G ZIP`). 1065 of the 1067 tests registered on
  Windows 11 pass; the two left are a Git Bash harness limit (`cli_sigint`)
  and a tutorial example written in POSIX shell syntax. What differs there by
  design: binary standard streams and a UTF-8 console and code page, `/` in
  the paths the file primitives answer, `cmd.exe /c` as `OSProcess`'s shell,
  no readline, Ctrl-C ending a script with `STATUS_CONTROL_C_EXIT`. Every
  Windows difference is behind `WIN32` / `_WIN32` / `_MSC_VER` or has the same
  meaning everywhere; on Linux the suite passes 1068/1068 as before. See
  `docs/INSTALLATION.md`, "Windows (MSVC)".
- **protoCore 2.7.0 is the floor.** The sources spell protoCore's 64-bit
  integers `proto::proto_long` / `proto::proto_ulong` (`long` / `unsigned
  long` outside Windows, so nothing changes on Linux and macOS), which first
  exist in 2.7.0. CI pins protoCore 2.7.0 and the protoIO that builds on
  Windows and macOS.
- **An unwind no longer re-throws inside a catch clause.** Exceptions that a
  nested engine, a primitive boundary, `on:do:` or `ensure:` let through are
  re-thrown after the catch clause (`std::rethrow_exception`). Same meaning
  under GCC and Clang; under MSVC the old `throw;` kept every frame below each
  catch clause alive, and a recursion through `do:` blocks that hits the
  nesting limit crashed with a native stack overflow instead of raising its
  catchable `Error`.
- `.gitattributes` keeps `*.st`, `*.sh`, `*.md`, `*.out`, the kernel manifest
  and the venv template LF on every platform: a multi-line string literal
  keeps its line endings, so a CRLF checkout changed what programs and
  expected outputs meant.

- **I/O now built on protoIO 0.1.0.** The POSIX layer behind the I/O
  primitives (buffered descriptors, SIGPIPE handling, TLS, child processes,
  sockets, the chunked-body reader, percent-decoding) moved into
  [protoIO](https://github.com/gamarino/protoIO), a static library shared
  with the other protoCore runtimes, so a defect in it is fixed once for all
  of them. `src/primitives/io_prims.cpp` keeps only the bindings: argument
  conversion, leaving the collector's quorum around each blocking call, and
  mapping protoIO's error kinds onto protoST's error classes. No behaviour
  change for correct programs: every primitive, error class and `.st` file is as
  before, and the whole suite passes unchanged. protoIO is a new build
  requirement (a sibling checkout `../protoIO` or the `protoio-dev` package);
  the installed `protost` does not depend on it. Where protoIO is stricter
  than the old code, in edge cases no test, demo or documented example relies
  on: a line over `nextLineMax:`'s limit never consumes input, even when the
  limit falls on a pending CR; `setenv:to:` with an invalid variable name
  (empty, or containing `=`) raises an `Error` instead of silently doing
  nothing; a port outside 0-65535 raises an `Error` instead of being taken
  modulo 65536 (70000 used to connect to port 4464); and the messages of the
  `OSProcessError`s raised by `waitFor:` and `kill:` now name the process.

## 0.5.0 — input and output (2026-09-29)

Until this release a protoST program could only print. Design:
[`docs/superpowers/specs/2026-09-29-io-design.md`](docs/superpowers/specs/2026-09-29-io-design.md);
tutorial: [chapter 15](docs/tutorial/15-input-and-output.md); reference:
`docs/LANGUAGE.md` §12.11. `ctest`: 1068 cases, all passing (537 conformance
programs, 437 unit tests, 42 examples, 30 CLI tests, 22 documentation
checks).

- **Input and output in the kernel** (`lib/kernel/io.st`, always loaded),
  with Pharo's names where Pharo has them: `Stdio stdin` / `stdout` /
  `stderr` (a program can be a Unix filter); files and directories through
  `FileReference` (`'p' asFileReference`, `/`, `contents`, `contents:`,
  `lines`, `writeStreamDo:`, `children`, `copyTo:`, `deleteAll`, …) and
  `FileSystem temp` / `home` / `workingDirectory`; `Smalltalk arguments`
  (the words after the script path), `getenv:`, `setenv:to:`, `environment`,
  `exit:`; other programs through `OSProcess` (`run:arguments:input:`,
  `shell:`, `command:`, `spawn:`, `waitFor:`, `kill:`). Failures are classed
  errors: `FileDoesNotExist`, `FileAlreadyExists`, `FileSystemError`,
  `ConnectionRefused`, `ConnectionTimedOut`, `NameLookupFailure`,
  `NetworkError`, `OSProcessError`.
- **The `net` module** (`Import from: 'net'`): TCP `Socket` and
  `ServerSocket`, a TLS client with certificate and host-name verification
  (`tlsHost:`, OpenSSL, now a build requirement), and `UDPSocket`.
- **The `http` module** (`Import from: 'http'`): `HTTPClient` for http and
  https, JSON bodies, chunked responses and redirects; `HTTPServer`, which
  handles each connection on its own actor, with `start`,
  `startInBackground` and `stop`. No HTTP/2, WebSockets, keep-alive or TLS
  server.
- **Hardened after a code review, before release.** Actors can share a
  stream: reads and writes are serialised separately, and a descriptor is
  released only when no actor is still blocked on it. A child that exits
  without reading its input, or a peer that disappears, raises an error
  instead of killing the process with SIGPIPE. TLS handshakes, reads and
  writes honour the socket's timeout. The worker pool never holds its lock
  while creating a thread. HTTP refuses control characters in methods, URLs
  and headers; decodes chunked bodies and percent-escapes as UTF-8 bytes (`+`
  is a space only in the query); resolves relative redirects, drops the
  program's headers on a redirect to another origin and refuses https to
  http; and the server bounds request lines, headers and bodies (414, 431,
  400, 413, `maxBodySize:`) and answers handler errors with a plain 500
  reported on standard error. New: `nextLineMax:`, `LineTooLong`,
  `BodyTooLarge`. `fullName` no longer resolves symbolic links.
- **The worker pool grows under blocking I/O.** Every blocking call leaves
  the garbage collector's quorum while it waits. When an actor's worker
  enters one and fewer than the configured number of workers would remain
  free, the runtime starts another worker (up to 256 threads; they stay
  until the program ends), so actors that wait for replies cannot starve the
  actors that would send them, and one actor per connection scales past the
  core count.
- **A subclass of a kernel class sees the instance variables it inherits**
  (S20): `IOStream subclass: #LabelledStream` can read `fd`, a `WriteStream`
  subclass `buffer`; before, both were `undeclared variable`. Test:
  `04-object-model/inherited_kernel_ivars`.
- **Requires protoCore 2.6.1.** Growing the pool creates threads from worker
  threads, and before 2.6.1 protoCore's `newThread` called from a worker left
  the main program's variables unscanned by the collector. The DEB and RPM
  dependency floors are now 2.6.1, which also closes the DEB floor's gap
  with the SONAME (see `docs/INSTALLATION.md`).
- **Talk demo 4**, `docs/talks/2026-10-15-fas/demos/04-connected-twin.st`: a
  twin that reads its sensor feed on standard input (from a Python script),
  serves its state over HTTP while it runs and logs every reading to a file.
- Native Windows is not supported (POSIX I/O, GCC builtins); use WSL2 with
  Ubuntu 24.04 (`docs/INSTALLATION.md`). Streams read whole UTF-8
  characters (`next`, `next:`); `nextByteCount:` counts bytes.

## 0.4.1 — a script prints once (2026-09-29)

- `protost file.st` no longer prints the value of the script's last
  top-level statement after the program (deviation D12b is retired): a script
  shows only what it prints, so a script ending in `x printNl.` prints `x`
  once. `protost --print-last file.st` keeps the old behaviour; `-e` and the
  REPL still show values. The tutorial examples, the programs in `examples/`,
  the benchmark scripts and the talk demos print their results explicitly
  (their output is unchanged). Test: `cli_script_output`.
- K4 (another runtime in the process could not use protoST objects) is fixed
  in protoCore after 2.5.0; the documents now say which protoCore each claim
  needs. The integer workarounds (`IntegerDivision.h`, `wideBitOp`) stay while
  protoST's protoCore floor is 2.5.0: protoCore fixed both defects after it.

## 0.4.0 — the Smalltalk you know, verified (2026-10-03)

0.4.0 is the release prepared for a technical Smalltalk audience: an
adversarial audit (about 1,100 probe programs in two rounds, written the way a Pharo
programmer writes) drove every change below, and each fix of a wrong result,
crash or hang carries a regression test. `ctest` at commit `d3f7235`: 1041
cases, all passing (854 at the start of the audit): 513 conformance
programs, 437 unit tests, 42 examples, 28 CLI tests and 21 documentation
checks. Details of each item are in [`docs/STATUS.md`](docs/STATUS.md) and
[tutorial chapter 14](docs/tutorial/14-for-the-smalltalk-programmer.md).

### Silent wrong results fixed

- A method's instance variable was confused with a same-named file-level
  variable captured by a top-level block (the method wrote the file variable).
- A block parameter or temporary that reused an enclosing name aliased the
  outer variable; a block temporary named like an instance variable wrote the
  instance variable. Every block activation now has its own bindings.
- A method whose last line had no period swallowed the next top-level line.
  A method body now also ends at an unindented line or at the next method
  declaration.
- `(3/2) max: 1`, `(1/2) between: 0 and: 1` and other Fraction operations ran
  integer primitives on the Fraction.
- A class-side method that assigned an instance-variable name wrote it on the
  class, where every instance that had not set its own inherited it.
- `new` on a subclass of a built-in collection answered the built-in class.
- `(1->2) = (1->2)` was false; `#(at:put:)` held two symbols; a long Symbol's
  `asString` answered the Symbol; `respondsTo:` answered true for class-side
  selectors on instances.

### Hangs and crashes fixed

- **S19, the intermittent hang of `cli_concurrent_first_call`:** captured
  with gdb attached to the live hung process (new `PROTOST_ALLOW_PTRACE=1`):
  a worker asleep in `std::counting_semaphore::acquire()` with a permit
  available while `~STRuntime` joined it — a lost wakeup in libstdc++ 13's
  semaphore. Replaced by `protoST::Semaphore`, whose waiters sleep only on a
  zero count and whose every release notifies.
- A cycle of actors waiting on each other (`a` waits on `b`, which waits on
  `a`) signals a catchable "deadlock" `Error` instead of hanging.
- A `wait` inside a block that a primitive evaluates (`do:`, `collect:`,
  `inject:into:`, `ensure:`, `on:do:`) in an actor method ended the loop after
  one element, skipped `ensure:` and escaped handlers. Such a wait now blocks
  its worker, which runs other actors' messages meanwhile.
- `Future whenAny:` waited forever when every future was rejected.
- A negative integer attribute read through a unary send was taken for a
  primitive marker and aborted the interpreter.
- A heap ceiling (10M cells, 640 MB, by default; at most a quarter of the RAM; `PROTOCORE_HEAP_LIMIT_CELLS` raises it and the out-of-memory message says how)
  replaces the unbounded growth that could exhaust the machine; running out
  exits with a clear message. Ctrl-C interrupts a running program cleanly.

### Smalltalk semantics

- `new` sends `initialize`; `Character`; exact `Fraction` division (`3/4`),
  floored `//` and `\\`, `ArithmeticError` / resumable `ZeroDivide`;
  `SubscriptOutOfBounds`, `KeyNotFound`, `NotFound`; `doesNotUnderstand:`
  overrides receive a `Message`; class variables assignable from instance
  methods; class-instance variables; `Transcript`; `printOn:`-based printing
  everywhere; classes print as their names; floats print as Smalltalk
  literals (`1.0e16`).
- Errors inside an actor keep their class across `wait`; an uncaught error
  prints its class, its text and a trace (`at Class>>selector (file:line)`).
- The compiler reports undeclared variables in methods, assignments to
  arguments and pseudo-variables, and duplicated or redeclared instance
  variables. A blank line ends a method body; top-level `| temps |` work.
- Protocol: the collection, string, stream, number (bit operations, `**`,
  `roundTo:`), exception (`retryUsing:`, `isResumable`), reflection
  (`instVarNamed:`, `selectors`, `canUnderstand:`, `subclasses`, `deepCopy`,
  `inspect`) and system (`Time`, `Date`, `Duration`, `Random`,
  `Smalltalk version`) messages a Pharo programmer expects. A `Collection`
  subclass that defines `do:` gets the derived enumeration protocol
  (`groupedBy:` also needs `add:`).

### Tooling and honesty

- Every benchmark verifies its result and the harness fails on a missing or
  wrong value (`bench_harness_selftest`); CPython twins run the same N.
- Interop claims narrowed to what a two-runtime process shows
  ([`docs/INTEROP.md`](docs/INTEROP.md) §0, `KNOWN_ISSUES.md` K4).
- The version comes from one place; `ctest` no longer needs stdin redirected.

### Earlier changes in this release (after v0.3.0)

### Language

- **Call-form sends and method declarations** (commit `2af8c5c`).
  `recv name(p1, p2, k1 = v1)` sends and `Class >> name(p1, k1 = default)`
  declarations follow the protoCore method convention (positional arguments
  plus named arguments). They are designed so that methods exposed by other
  protoCore runtimes could be called without selector mangling; that is not
  implemented: a call-form send reaches protoST methods only
  ([`docs/INTEROP.md`](docs/INTEROP.md) §0). Named-argument defaults are
  evaluated at call time. A class cannot host both a unary `>> bar` and a
  call-form `>> bar(...)`; actor receivers do not accept call-form sends yet.
- **Class variables** (commit `6b3cf39`). A non-empty
  `classVariableNames:` clause is honoured: each name is installed on the
  class and readable from instance and class methods, including in
  subclasses. (At that commit assignment was allowed only from class-side
  methods; 0.4.0 lifts that restriction — see above.)

### Collections

- **`Set` and `Dictionary` share one mechanism: protoCore's `ProtoMap` and its
  hashed-collection helper** (Track S). A `Set` used to store a protoCore
  `ProtoSet`, keyed by protoCore's hash with no collision handling; a
  `Dictionary` used to store a hand-rolled hash-to-bucket `ProtoSparseList`.
  Both now store a `ProtoMap` — a `Set` being the map that holds each element
  under itself — reached only through `hashedPut` / `hashedGet` /
  `hashedRemove` / `hashedForEach`, with protoST supplying the equality and the
  hash. One hash, one equality, one collision policy, shared with protoScala
  and protoClojure.
- **Hashed collections decide membership with `=`** — closes **D32**, which had
  been open on a language decision nobody had taken. A `Set` holding `1` now
  includes `1.0`, a `Dictionary` keyed by `1` answers for `1.0`, and `Bag` is
  unchanged because that is already what it did. A NaN is found only by
  identity, which is what §12.2's `Float nan = Float nan` being false requires,
  so a `Set` holding one NaN no longer includes a different NaN object — the
  one behaviour change that is not a strict widening. `LANGUAGE.md` §9.6-9.8
  now state the rule, and also state that iteration order is unspecified; the
  decision, its argument and the cost of reversing it are in `docs/STATUS.md`,
  marked `[agent, pending review]`.

### Actors

- **Actor mailboxes are protoCore `ProtoMPSCQueue`s** (Track S). A send used
  to be a compare-and-swap retry loop that rebuilt the actor's whole mailbox
  `ProtoList` and republished it under `__mailbox__`; a pop rebuilt it again.
  The mailbox is now a lock-free multi-producer / single-consumer queue whose
  contents the garbage collector traces, the queue object's identity never
  changes, and a send is one `push` — O(1), one cell, no retry. A turn takes a
  whole batch with `takeAll` and parks anything it has not processed in a new
  `__pending__` attribute, which is read ahead of the queue on the next turn,
  so per-actor FIFO order is unchanged and a batch interrupted by a
  `FutureYield` is neither lost nor reordered.

  The old representation was quadratic in the depth of an undrained mailbox,
  in time and in memory. Measured on this machine (`benchmarks/reports/2026-09-24-mailbox-protompscqueue.md`): a single-producer send falls from
  1256 ns to 788 ns (and from 63.6 us to 3.5 us with `PROTOST_WORKERS=1`,
  where producers and sink share one thread), and the drain of a 2000-message
  backlog from 1.585 s to 4.1 ms; 200 000 undrained messages, which the old
  mailbox could not reach at all — 10 000 was already killed by the OOM reaper
  at over 10 GB resident — now enqueue in 510 ms and drain in 1.75 s.

  Requires protoCore 2.1.0, so protoST's version floor moves from 2.0 to 2.1.
- **Three priority bands** (commit `3efb31d`). `asHighPriorityActor` and
  `asLowPriorityActor` join `asActor` (Medium, the default). Workers drain
  strictly by priority — High, then Medium, then Low — with the
  single-method invariant unchanged in every band. There is no starvation
  guard. See `docs/tutorial/10-actors-and-futures.md` §10.11 and
  `examples/actors/05_priority_bands.st`.

### Interop

- **`provider:st` serves a caller in another runtime's `ProtoSpace`** (commit
  `e82682b`, S17). A cross-runtime import of a protoST module now resolves:
  protoScala's `import st.<module>` loads the module, its members bind by name,
  and the values are the protoST objects themselves — the same cell address and
  the same `getHash` read from either runtime, printed by
  `tests/unit/test_cross_runtime_provider.cpp`.

  `ModuleProvider::tryLoad(path, ctx)` receives the **caller's** context, and
  `STModuleProvider` resolved its runtime from `ctx->space` — a space protoST
  does not own when the caller is a co-resident runtime, so the lookup missed and
  a module that was there was reported as absent. The fix is in the provider and
  **protoCore is unchanged**: a `ModuleProvider` is an object with its own state,
  so it takes its runtime from that state (`soleSTRuntime()`) and uses `ctx` only
  to allocate the result in the caller's context.

  Two consequences worth knowing if you write a provider. The load runs in
  protoST's **own** space, because running a module's top level on a foreign
  context interns its literals in the foreign symbol table and leaves protoST's
  own later lookups missing. And the module **namespace** is rebuilt with keys
  interned in the **caller's** space, because an attribute key is an interned
  symbol's address and protoCore interns per `ProtoSpace`; only the mapping is
  rebuilt, never the values. protoCore embeds a short string in the pointer word,
  so a 5-byte member name matched across spaces by accident and a 7-byte one
  missed silently.

  `STRuntime::isOwnerThread()` is new. A cross-runtime load runs on protoST's
  root context, which only the constructing thread may allocate on (D26), so an
  import from any other thread is refused with a message rather than raced; a
  process holding two `STRuntime`s is refused as ambiguous. What this does **not**
  deliver — a foreign runtime *calling* a protoST method, imports from more than
  one thread, more than one runtime, a namespace that changes after import — is
  listed in [`docs/INTEROP.md`](docs/INTEROP.md) §4.2.

### Runtime

- **Blocking OS calls run inside `ProtoContext::UnmanagedScope`** (commit
  `481a413`): `Object>>sleep:`, REPL input and `:load` file reads, the DAP
  server's message read and the CLI debugger prompt. A garbage-collector
  stop-the-world phase no longer waits for a thread blocked in these calls.
- **`doesNotUnderstand:` errors name the receiver's class** (commit
  `0075449`), e.g. `doesNotUnderstand: fooBar (receiver class: SmallInteger)`.

### Performance

- The compiler inlines `ifTrue:`, `ifFalse:`, `ifTrue:ifFalse:` and
  `whileTrue:` with literal blocks (commit `0d396ca`), and
  `start to: end do: [:i | body]` with a literal one-argument block (commit
  `ea5c952`).
- SmallInteger fast-path opcodes for `+ - <= < >= > =` (commit `b623448`).
- Dictionary key hashes are canonicalised through the symbol table, which
  enables the rope-aware `,` concatenation fast path (commit `8dd93f2`).
- Cheaper frame setup (commits `ac1e8b4`, `2c0ca61`), a single-guard fast
  path for `on:do:` (commit `f6a1aa2`) and cached attribute-key symbols in
  the exception primitives (commit `dad27b9`).
- Dated reports for this work are in `benchmarks/reports/` (2026-05-24).

### Fixes

- **An actor mailbox batch could be collected in the middle of the turn that
  was processing it** (S16). `MailboxCursor::adopt` re-pinned the batch with
  `pin_.reset(new TransientPin(...))` on a `std::unique_ptr`, and
  `std::unique_ptr::reset` builds the replacement before destroying what it
  replaces — so a turn that adopted twice (a `__pending__` tail then a queue
  batch, or two non-empty `takeAll`s) released a scratch pin slot above one
  still in use, and the next `TransientPin` on that thread overwrote the live
  one. The batch then had no GC root at all: a collection freed the messages
  the turn had not reached and `next()` read a freed cell, segfaulting about
  two runs in five. The cursor now holds one pin for its lifetime and
  re-points it with `TransientPin::reset`. Introduced by the mailbox migration
  and invisible until S15 was fixed — a runtime that reclaims nothing cannot
  expose a lost root. Found by `cli_actor_payload_gc` on its first run.
- **protoST reclaimed nothing** (S15). A collection cycle
  never returned a cell to the heap: measured on the pre-fix build, four runs
  of a 20,000-iteration allocating loop left 2,748,398 cells in the root
  context's young generation, cycles ran and every one reclaimed exactly 0,
  and the heap grew monotonically. protoCore chains every cell a context
  allocates onto that context's young generation and treats the chain as a GC
  root until the context submits it — which happens when the context is
  destroyed, or from `ProtoContext::safepoint()` past a per-context
  threshold. protoST creates no `ProtoContext` of its own (a program runs on
  the runtime's root context, an actor turn on its worker's) and the
  interpreter called `safepoint()` nowhere at all, so every cell a program
  ever allocated stayed live by definition. `ExecutionEngine::gcSafepoint`
  now calls it at the loop back-edge and at engine entry, the two points
  where every live object is in a traced slot rather than a C++ local. After
  the fix the same workload holds the heap flat at 1,048,576 cells and
  reclaims about 730,000 cells per cycle, against a heap that grew to
  3,866,624 cells and reclaimed nothing before it. The same call is also
  protoCore's park point, so it very probably closes **S3** too — but that
  could not be proved, because no protoST loop is actually allocation-free
  (every shape measured allocates about three cells per iteration), so S3
  stays open. Cost, worst case, on a 20,000,000-iteration integer loop that
  does nothing but add: +2.7 % instructions, +4 % cycles.
  `PROTOST_NO_GC_SAFEPOINT=1` disables the hook. Regression tests: one `[gc]`
  unit case, `cli_gc_reclaims` (which also requires the hook-disabled run to
  fail) and `cli_actor_payload_gc`.
- **Heap corruption when several workers ran a method for the first time at
  the same moment** (D25). The per-module caches of interned selector
  symbols, instance-variable keys and parsed call-form descriptors were
  filled lazily without synchronisation; two threads could both reallocate a
  cache and one wrote into the buffer the other had freed. It showed up as
  intermittent `malloc(): unaligned tcache chunk detected` /
  `tcache_thread_shutdown()` aborts (for example in
  `examples/programs/traffic_intersection.st`), crashes, or an instance
  variable reading as `nil`. The caches are now fixed-size tables published
  once with a compare-and-swap and filled through atomics; call descriptors
  are published immutably. Regression tests: `cli_concurrent_first_call`
  and two `[bytecode][concurrency]` unit tests.
- **Memory corruption when an actor method contained a string literal longer
  than six bytes or a Float literal** (D26). Literals were allocated on the
  main thread's context whichever thread ran the method, so worker threads
  took cells from the main thread's unsynchronised free list; every run of
  such a program crashed, hung or printed garbled text. Literals are now
  allocated on the calling thread's context. `STRuntime::materialize` takes
  that context, and a module loaded through `loadModuleFromFile` runs on the
  caller's context through the new `runTopLevel(module, ctx)` overload.
  Regression tests: `conformance/10-actors/literal-string-in-actor-method.st`
  and `literal-float-in-actor-method.st`.
- **Use-after-free and ABA hazard in the scheduler's lock-free ready queue**
  (D27). `ReadyStack` freed a node on every pop, so a concurrent pop could
  read the successor of a node another worker had already freed, and an ABA
  interleaving (glibc's tcache hands a freed node straight back to the next
  allocation) could install a freed node as the queue head, losing actors and
  double-freeing memory. The old comment claiming glibc does not recycle freed
  pointers quickly was wrong. The stack now keeps its nodes in a type-stable
  pool with a lock-free free list and tags each head with a 32-bit generation
  counter, so a stale compare-and-swap always fails; it also no longer calls
  malloc/free per message. It moved to `src/runtime/ReadyStack.h`. Regression
  tests: two `[scheduler][readystack]` unit tests.
- **`Import from:` inside an actor method answered `module not found`** (S6).
  The module provider found its runtime through a thread-local pointer set
  only on the thread that constructed the runtime, so imports failed on every
  worker thread (and on the DAP debuggee thread). The provider now resolves
  the runtime from the calling context's `ProtoSpace`. Imports from several
  threads also exposed a race: actors importing the same not-yet-loaded module
  each ran its top level and received distinct module objects. The new
  `STRuntime::importModuleFile` runs a module's top level once per runtime;
  concurrent importers wait for that load and receive the same module object,
  a failed load is not cached, and an import cycle raises
  `cyclic module import: <path>` instead of waiting forever. Regression
  tests: `conformance/11-modules/import-from-actor-method.st`,
  `import-concurrent-runs-once.st`, `import-cycle-errors.st` and
  `import-cycle-concurrent.st`.
- **An error raised by an imported module's top level reached the importer's
  handler twice** (D28). The module's top level ran through the entry point
  meant for a script's own top level, which turns an unwind that escapes it
  into a new error: the importer's handler ran once with the module's error
  and again with `exception unwind: no matching on:do: handler activation`,
  and `return:`, `retry` and `pass` in that handler misbehaved the same way.
  A module's top level now runs without that conversion, so the unwind
  reaches the importer's frames: the handler runs once with the original
  error, handler actions behave as for any other error, and a nested import
  failure crosses both modules. A failed load is still not cached. The same
  path also freed a failed module's compiled code while classes it had
  already declared stayed global, so calling one of their methods crashed;
  the code is now retained whether or not the load completes. Regression
  tests: the `import-error-*.st` and `import-warning-resume.st` tests in
  `conformance/11-modules/`.
- `--help` and engine errors no longer show internal milestone labels such
  as "(F7)", "— F2" or "F2 limit". A send with more than 8 arguments now
  reports "send of #<selector> has <n> arguments; at most 8 are supported per
  send", and a non-unary send to a value attribute reports "#<selector>: the
  attribute is a value, not a method; only unary sends read value
  attributes".
- Inlined conditionals and loops reject non-Boolean receivers with
  `doesNotUnderstand:`, as the non-inlined sends do, through the new
  `ASSERT_BOOL_OR_DNU` opcode (commit `cf2ebc3`).
- **The compiler crashed on block literals in inlined `to:do:` bodies**
  (D29). A program such as `1 to: 2 do: [ :i | [ 1 ] value ]` or the nested
  loop `1 to: 2 do: [ :i | #(1 2) do: [ :x | s := s + x ] ]`, at top level or
  in a method, died before running (segmentation fault or floating-point
  exception), or on some heap layouts compiled silently through freed memory.
  The inlined loop held a reference into the compiler's scope stack while it
  compiled the body, and the scope pushed for any block literal could move
  that stack. The scope stack no longer moves its entries. A second defect
  in the same inlining is fixed too: when the enclosing method or module
  captured a variable with the same name as the loop variable, the loop body
  read that captured variable (`i := 5. b := [ i ]. s := 0. 1 to: 3 do:
  [ :i | s := s + i ]` summed to 15, not 6). Such a loop is now compiled as
  a real block send. Regression tests: the `to-do-*.st` and
  `inlined-control-nests-blocks.st` tests in `conformance/06-blocks/`, two
  tests in `conformance/07-non-local-return/` and two `on-do-inside-to-do-*`
  tests in `conformance/08-exceptions/`.
- **A stop-the-world phase could start while a thread still ran** (S4).
  protoST removed a blocked thread from protoCore's running-thread count
  (`GcSafeBlocking.h`) while its REPL, `sleep:`, DAP and debugger reads used
  protoCore's unmanaged regions, which count the thread as parked; a thread
  counted both ways let the collector's quorum complete early. The DAP
  debuggee also ran on a plain `std::thread` sharing the adapter thread's
  context, so an `evaluate` while stopped overwrote the debuggee's frame
  slots (`doesNotUnderstand: do: (receiver: nil)`). Every blocking wait —
  idle and paused workers, `Future>>wait` outside actors, a concurrent
  import's wait, the shutdown join, a debugger stop — now runs in an
  unmanaged region on the blocked thread's own context, and the runtime no
  longer writes protoCore's thread counters. The debuggee is a protoCore
  thread with its own context; the CLI debugger prompt and `print` use the
  halted thread's context. `GcSafeBlocking.h`, `GcSafeMutex.h` and the unused
  per-actor lock were removed. An idle worker submits its young generation
  before sleeping; a thread waiting outside an actor does so only when no
  primitive-created engine is below the wait. Regression tests: two `[s4]`
  unit tests, `cli_gc_shutdown_stress` and `cli_dap_gc`.
- **Comparisons with NaN answered true** (D31). The numeric comparison
  primitives, `min:`, `max:`, `between:and:` and the element and key equality
  of `OrderedCollection`, `Bag` and `Dictionary` used protoCore's `compare`,
  a total order that reports a NaN as equal to every number:
  `Float nan = Float nan`, `Float nan = 1`, `Float nan <= 1` and
  `1 = Float nan` were true, `Float nan min: 1` answered NaN, a `Bag` holding
  only a NaN included 7, a `Dictionary` keyed by one NaN answered its value
  for another NaN, and `remove: 2.5` removed a NaN. They now use protoCore's
  `partialCompare`, which follows IEEE 754: a NaN is unordered with every
  number, so every ordering and `=` are false and `~=` is true; `min:` and
  `max:` answer the argument; these collections find a NaN only by identity.
  Comparisons of other numbers are unchanged. `Set` membership was not
  affected by this change; it was fixed separately (D32, above). Regression
  tests: `conformance/12-builtins/float-nan-comparison.st` and
  `conformance/09-collections/nan-elements.st`.
- **In the REPL, a block assigning a session variable declared a new block
  variable instead** (S10). After `s := 0.`, the input
  `#(1 2) do: [ :x | s := s + x ]` failed with
  `doesNotUnderstand: + (receiver: nil)` and `[ s := 7 ] value` left `s` at 0:
  the compiler turned an assignment inside a block into a block-local
  declaration whenever it was not at module scope, although a read of the
  same name resolved to the session global. In REPL mode an assignment in a
  block of module-level code to a name that no enclosing scope binds now
  updates the global; block temporaries and arguments still shadow it, and
  method bodies and scripts are unchanged. Regression test: new cases in
  `tests/cli/test_cli_repl.sh`.
- **An actor suspended in `wait` could resume before the awaited future
  settled** (S13). Any scheduler wakeup of a suspended actor resumed its
  method: a message sent to it while it waited, or one that arrived during
  the turn in which it suspended, made `wait` answer `nil`
  (`nil + 1` → `doesNotUnderstand:`). The unit test "F6 v3 E3: cooperative
  chain survives aggressive GC on 4 workers" failed intermittently for this
  reason (4 runs in 300 on this tree, 12 in 300 on the build before S4). A
  wakeup now resumes the method only when the awaited future has settled;
  otherwise the actor stays suspended, its queued messages wait for the
  method to finish, and the future's settlement reschedules it. Regression
  test: `conformance/10-actors/resume-waits-for-awaited-future.st`.

### Build

- **protoCore 2.0.0** (soname `libprotoCore.so.2`). protoST builds and passes
  its whole suite against it unchanged — no source change was needed. The ABI
  bump means every protoST build directory must be recreated from clean; a
  stale object now fails to load with a missing-soname error instead of
  linking against an incompatible layout. Of the four behaviour changes
  2.0.0 lists for embedders, only one is observable here, and only in
  documentation: see "Documentation" below. protoST calls no `setParents` and
  no `isInstanceOf`; its single `hasParent` call (`HandlerStack`, exception
  guard matching) keeps its semantics and is now allocation-free.
- **A fresh configure could link a stale protoCore** (S14). `CMakeLists.txt`
  searched `../protoCore/build` before `../protoCore/build_release`, so a
  leftover `build/` from an older checkout shadowed the library the ecosystem
  rebuilds (here a June build was picked over the current one). The search
  order is now `build_release`, `build`, `build_check`, as in protoClojure, and
  the not-found message suggests `build_release`. The README documents the
  order and how to override it.

### Documentation

- **The `addBehavior:` rationale claimed something protoCore 2.0.0 makes
  false.** `docs/STATUS.md` D21, `docs/LANGUAGE.md` §4.12, the tutorial
  (§11.3, §14.4), `src/primitives/object_prims.cpp` and
  `tests/unit/test_t3c_addbehavior.cpp` all stated that a direct
  `addParent`/`setParents` on a live class is invisible to **all** of that
  class's instances, past *and* future — "verified by direct probing" under
  protoCore 1.x, where `newChild` copied the prototype's birth-state chain.
  protoCore 2.0.0 copies the prototype's *current* chain, so instances created
  **after** such a mutation do see it; only instances that already exist do
  not. Re-probed directly against 2.0.0 and corrected everywhere. **D21 itself
  is unchanged** — its subject is pre-existing instances, and `addBehavior:`
  still has "future instances only" semantics — but the rebuild is no longer
  the only route to those semantics, which is now recorded where the rebuild
  is justified.

### Known issues

- The large-rope garbage-collector issue (K2) is fixed in protoCore; see
  [`KNOWN_ISSUES.md`](KNOWN_ISSUES.md).
- A thread running an allocation-free loop never reaches a garbage-collector
  park point, so a requested collection waits for its loop to end (S3, open).
  The S15 fix adds `ProtoContext::safepoint()` at every loop back-edge, which
  is that park point, but the close is unproved: no protoST loop allocates
  nothing, so S3 cannot be exhibited. See `docs/STATUS.md`.

### Tests

- **CLI tests no longer fail intermittently on SIGPIPE** (S11).
  `tests/cli/test_cli_help.sh` failed about 4 runs in 1000 with
  `--help missing 'Usage:'` although the text was there: under
  `set -o pipefail`, `protost --help | grep -q …` let `grep` exit at the
  first match and close the pipe while `protost` was still writing, so
  `protost` died of SIGPIPE (exit 141) and the pipeline failed. Every CLI
  test script now captures a command's output before searching it
  (`out=$(…)` then `grep … <<< "$out"`), which also removes the latent
  `echo "$out" | grep -q` form (46 checks in 6 scripts) that can fail the same
  way once the output exceeds a pipe buffer.
- 753 → 833 `ctest` cases (352 conformance, 42 examples, 12 CLI, 427 unit).

## 0.3.0 — yieldable iteration (2026-05-23)

Adds `doYielding:` — the compiler-recognised yieldable counterpart of
`do:`. Lifts the old "no `wait` inside a `do:` block of an actor
method" limitation that blocked driver-actor multi-producer patterns.

### New language feature

- **`coll doYielding: [ :elem | body ]`** — when the compiler sees
  this exact shape (literal block, one formal parameter), it emits a
  bytecode loop using `at:` + `value:` instead of dispatching the
  primitive `doYielding:`. The block's `value:` send uses the
  engine's inline block-frame fast path (yieldable), so a `wait`
  inside the block parks cooperatively without losing iteration state.
  See `docs/tutorial/10-actors-and-futures.md` §10.8 for a worked
  example.
- **Limit**: only `SequenceableCollection`s (Array, OrderedCollection,
  Interval, String) support it — receivers without `at:` / `size`
  raise `doesNotUnderstand: doYielding:` at runtime. `do:` itself is
  unchanged.
- **Limit**: the integer `to:do:` and `whileTrue:` primitives are
  still non-yieldable. For a counted loop with `wait` inside, build
  an index list first and iterate with `doYielding:`.

### Runtime fixes

- **`finishDrain` stale-wakeup must respect `suspended`** (commit
  `7637e4f`). The 2026-05-23 stale-wakeup optimisation released the
  actor whenever schedState==2 with an empty mailbox, even when the
  drain exited via `FutureYield` (`suspended=true`). That LOST
  WAKEUPS for an actor whose awaited future had already settled by
  the time the engine tried to register it as a waiter:
  `appendFutureWaiter` returned `parked=0`, the handler called
  `schedule()` to mark state 1→2 ("re-schedule me to consume the
  value"), and finishDrain interpreted the 2 as stale and unanchored.
  The actor was left with `__suspended_frame__` set but no one to
  schedule it. Symptom: `doYielding:` in an actor method, w >= 2,
  N >= 3 iterations deadlocked. Fix: re-enqueue when `suspended=true
  || mailboxHasWork`.

### New runtime opcode

- `Op::JUMP_BACK` — backward complement of the existing forward
  `JUMP`. Used by the `doYielding:` desugar to close the loop. Five
  lines in `ExecutionEngine.cpp`; no other call sites.

### New benchmarks

- `benchmarks/actors/multi_producer.st` — rewritten to use
  `doYielding:` (was blocked by the limitation). Each of 8 driver
  actors fan-outs to 12 private sinks over 1000 rounds (96 000 ping
  messages, summing settle counts to 48 048 000). The benchmark
  RUNS and produces the correct answer; **throughput is comparable
  to `mt100a`, NOT the 10× the design speculated** — each driver
  pays a yield-resume cycle on every fan-out element, more expensive
  than main's blocking wait. The real value of `doYielding:` is
  correctness + composability, not aggregate throughput. The
  honest performance number remains the 0.2.0 figure
  (mt100a w=2 ~ 67-72 K msg/s on the 5500U notebook).

### Test count

751 → 753: two new conformance tests — `tests/conformance/do_yielding.st`
for the `doYielding:` desugar, and `tests/conformance/do_yielding_actor.st`
for the race fix (3 sinks × driver-actor with `doYielding:` containing
`wait`, multi-worker stable).

## 0.2.0 — Performance pass (2026-05-23)

An optimisation pass on the actor dispatch path.
Headline: `mt100a` (the round-trip throughput benchmark) moves from
**~ 30 K msg/s** to **71.9 K msg/s** on the Ryzen 5 5500U notebook — a
**+143 %** improvement. The report projects, without measuring, about
135–150 K msg/s on a Ryzen 7 7700X / Intel i9-13900K.

Three concurrent-runtime bug fixes also landed alongside the
optimisations — each was a real correctness improvement uncovered by
the performance investigation, not a tuning knob.

### Throughput (best of 3, AMD Ryzen 5 5500U, 6 physical cores)

| benchmark | before (0.1.0) | after (0.2.0) | factor |
|---|---|---|---|
| `mt100k` w=1  | ~ 20.6 K msg/s | 36.6 K        | +78 %  |
| `mt100a` w=1  | ~ 29.6 K       | 68.5 K        | +131 % |
| `mt100a` w=2  | — (not optimal) | **71.9 K**    | peak |
| `mt100a` w=4  | regression       | 71.9 K (no regression) | fixed |
| `saturation_big` w=6 scaling | regression at w=8 | **3.88×** at w=6 | fixed |

Full report and projections to other hardware:
[`benchmarks/reports/2026-05-23-performance.md`](benchmarks/reports/2026-05-23-performance.md).

### Bug fixes (correctness)

- **`finishDrain` spurious re-enqueue.** Pre-fix, every actor with
  multiple pending sends would be processed TWICE — once for real,
  once with an empty mailbox. Per-worker stats made the pattern
  visible (one worker accumulating 27 parks while others sat at 2-3).
  Fix: CAS the schedState transition first, THEN check the mailbox;
  if empty, release without re-enqueue.
- **Nested-engine cooperative-yield snapshot.** A `Future>>wait` from
  inside a block invoked by a primitive (`ifTrue:`, `whileTrue:`,
  `do:` etc.) only saved the outermost engine's frame stack; the
  inner block frame was silently dropped. Single-block patterns
  (`x ifTrue: [ ... wait ]`) now round-trip correctly. Iteration
  primitives (`coll do: [ ... wait ]`) get partial coverage — the
  full lift is documented as open work.
- **`WorkerPool` pause-and-load.** New runtime API
  (`WorkerPool stopProcessing` / `startProcessing`) that lets a
  benchmark pre-fill every actor mailbox before releasing the
  workers — needed to measure pool drain capacity in isolation
  from main's SEND rate.

### Performance optimisations

- **`createSymbol` on the SEND-* hot path replaced by Bootstrap
  cache.** Per-SEND `createSymbol("__class_name__")` +
  `createSymbol("__class_side__")` were ~ 45 % of CPU in `perf
  record` on saturation w=8, hammering the SymbolTable shard
  mutex. Both keys cached on Bootstrap; ~ 10× speedup at w=4.
- **`newFuture` reduced to one `setAttribute`.** Each new Future
  used to stamp three attributes (state, value, error); only
  `__state__` is load-bearing for the settle CAS. Saves ~ 6 cells
  per future, ~ 600 K cells on a 100 K-msg run.
- **SEND envelope built immutable.** The message envelope is read
  by the worker exactly once and never mutated; building it
  immutable saves the mutable shard-root CAS on every send.
- **Worker spin-before-park.** Workers now spin a few µs checking
  the ready stack before parking on the futex. Catches bursty
  SENDs from main in-flight and skips the futex entirely.
  `mt100a` parks dropped from 100 001 to 8 923 at w=1 (-91 %).

### protoCore changes (companion)

Companion protoCore commits (`ea2c17f4`, `ed38a499`, `90aade34`):

- **GC no longer triggers on freelist exhaustion when no heap cap
  is set.** Pre-fix, every freelist refill woke the GC unconditionally
  — and with no STW safepoint in protoST's bytecode dispatch, the GC
  parked waiting for the workers and never ran until the program
  ended. Now the GC runs only when a soft/hard heap cap is configured,
  the public `triggerGC()` API is called, or on shutdown.
- **`Cell::internalSetNextRaw` and `mutableRoot[shard].root` reads
  relaxed.** Both were `seq_cst` / `acquire`; x86 TSO already
  provides the necessary ordering and the operations have no
  cross-thread synchronisation contract.

### Operational guidance

- Use `PROTOST_WORKERS=N` with N = **physical core count** for
  CPU-bound workloads. Above that, SMT contention regresses the
  result (10-15 % slower per SMT pair active).
- For producer-bounded benchmarks (anything where main is the sole
  thread issuing SENDs), the sweet spot is **w=2**. Beyond that
  the workers compete for work that does not exist.
- The new `PROTOST_WORKER_STATS=1` env var prints per-worker drain
  + park counters on shutdown — useful for fairness diagnosis.

## 0.1.0 — Initial release (2026-05-22)

The first public release of **protoST** — an actor-native Smalltalk runtime
built on the [protoCore](https://github.com/numaes/protoCore) kernel.

### Language

- Lexer, parser and a non-recursive bytecode VM; closures with capture;
  classes, instances, methods and instance variables; `self` / `super`.
- Non-local return and the full Smalltalk exception protocol
  (`on:do:`, `signal`, `ensure:`, `ifCurtailed:`, resumable / retry).
- A real collection hierarchy and the iteration protocol.
- A standard library (Stream, Math, Random, JSON, Time) and a file-based
  module system integrated with protoCore's UMD module discovery.
- Advanced object model — multiple inheritance, mixins (`uses:`), runtime
  behaviour composition (`addBehavior:`).

### Actors and concurrency

- A first-class, language-embedded actor model: `asActor`, asynchronous
  message sends returning `Future`s, one-message-at-a-time per actor, a
  parallel worker-pool scheduler, and cooperative yield/resume.
- **Lock-free actor mailbox and Future** — no per-actor or per-future mutex;
  both run on protoCore's atomic attribute compare-and-swap.
- **`Atom`** — a shared mutable cell with optimistic-concurrency CAS
  (`value:ifCurrent:`, `swap:`), plus `Object>>setInstVar:from:to:` — the raw
  CAS on any instance variable.

### Tooling

- An interactive REPL, a Debug Adapter Protocol debugger (VS Code), native
  installers (CPack), a dual-audience tutorial, ~40 runnable examples and a
  benchmark suite.

### Tests

- 751 tests pass via `ctest` (the conformance suite, the unit suite, the
  examples and the CLI stress tests), each run in its own process.

### Known issues

Recorded honestly, with bounds, in [`KNOWN_ISSUES.md`](KNOWN_ISSUES.md):
one `STRuntime` per process (K1), very large ropes (K2), no `%` string
formatting (K3). None affects the shipped CLI configuration.
