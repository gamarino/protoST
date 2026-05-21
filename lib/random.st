"-- random.st: the Random standard-library module --

Provides Random — a deterministic, seedable pseudo-random number generator.
Pure protoST: no new primitives.

DETERMINISM CONTRACT
  Random is fully deterministic. A generator created from a given seed always
  produces exactly the same sequence of values, on every run and every
  platform. `Random new` uses a fixed default seed, so it too is reproducible.
  Random is NOT a cryptographic generator and must not be used for security.

ALGORITHM
  A 32-bit linear congruential generator (LCG), the Numerical Recipes
  constants:

    state := (state * 1664525 + 1013904223) \\ 4294967296

  The state lives in [0, 2^32). The intermediate product `state * 1664525`
  is at most ~2^53, comfortably inside protoST's 56-bit SmallInteger range,
  so every step uses only integer `* + \\` and never needs a LargeInteger.
  An LCG was chosen over xorshift because it relies on no bitwise operators.

USAGE
  m := Import from: 'random'.
  r := m Random seed: 42.
  r next.               => a Float in [0, 1)
  r nextInt: 6.         => an integer in 1..6
  r between: 10 and: 20. => an integer in 10..20
  r next: 3.            => an Array of 3 Floats in [0, 1)

  m Random new          => a generator with a fixed default seed (reproducible)."

"=== Random ================================================================="

Object subclass: #Random
  instanceVariableNames: 'state'.

"Class-side constructor: a generator seeded deterministically from anInteger.
 The seed is reduced into [0, 2^32) so any integer is accepted."
Random class >> seed: anInteger
  | r |
  r := self new.
  r setSeed: anInteger.
  ^ r.

"Class-side constructor: a generator with a fixed default seed. `Random new`
 is therefore reproducible — every `Random new` yields the same sequence."
Random class >> new
  | r |
  r := super new.
  r setSeed: 305419896.
  ^ r.

"Private initialiser used by the constructors. The raw seed is forced into
 the [0, 2^32) state range; a negative seed maps in via the modulus."
Random >> setSeed: anInteger
  state := anInteger \\ 4294967296.

"Advance the LCG one step and return the new 32-bit state. Private."
Random >> _step
  state := (state * 1664525 + 1013904223) \\ 4294967296.
  ^ state.

"The next pseudo-random Float, uniformly distributed in [0, 1)."
Random >> next
  ^ self _step asFloat / 4294967296.0.

"An Array of the next n pseudo-random Floats in [0, 1)."
Random >> next: n
  | result |
  result := OrderedCollection new.
  [ result size < n ] whileTrue: [ result add: self next ].
  ^ result asArray.

"A pseudo-random integer in 1..n inclusive. Signals an Error when n < 1."
Random >> nextInt: n
  n < 1 ifTrue: [ ^ Error signal: 'Random>>nextInt: argument must be >= 1' ].
  ^ (self _step \\ n) + 1.

"A pseudo-random integer in lo..hi inclusive. Signals an Error when lo > hi."
Random >> between: lo and: hi
  lo > hi ifTrue: [ ^ Error signal: 'Random>>between:and: requires lo <= hi' ].
  ^ lo + (self _step \\ (hi - lo + 1)).
