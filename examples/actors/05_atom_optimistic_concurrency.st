"EXPECT: 800"
"-- 05_atom_optimistic_concurrency.st --"
"--                                                                  --"
"--   An Atom is a shared mutable cell with lock-free,                --"
"--   optimistic-concurrency updates. It is the third concurrency     --"
"--   tool in protoST, alongside immutable values and actors:         --"
"--                                                                  --"
"--     * immutable value — share it freely, it cannot change;        --"
"--     * actor          — serialise arbitrary logic over a state;    --"
"--     * Atom           — a bare cell many threads update directly,  --"
"--                        lock-free, with a compare-and-swap retry.  --"
"--                                                                  --"
"--   Use an Atom when many actors must update one shared value and   --"
"--   funnelling every update through a single owner actor would be   --"
"--   a bottleneck — a shared counter, a registry, a world graph.     --"
"--                                                                  --"
"--   Here four 'meter' actors run in parallel, each adding 200 to a  --"
"--   shared running total. No lock is taken anywhere; the total is   --"
"--   exactly 4 * 200 = 800 because the CAS-retry loop never loses    --"
"--   an update."

"The shared cell — an Atom holding the running total, starting at 0."
total := Atom on: 0.

Object subclass: #Meter.

"Each meter adds `amount` to the shared total, `times` times.

 `swap:` is the convenience form: it reads the current total, applies the
 block to it, and compare-and-swaps the result back. If another meter won
 the race in between, swap: re-reads the now-current total and retries —
 so the block is always applied to a fresh value and no addition is lost."
Meter >> report: amount times: count into: anAtom
  1 to: count do: [ :i |
    anAtom swap: [ :running | running + amount ] ].
  ^ self.

"For full control you can drive the CAS yourself instead of using swap: —
 read, transform, and retry with whatever validation/back-off policy you
 want. This is the same loop swap: runs internally:

   [ old := anAtom value.
     new := old + amount.
     anAtom value: new ifCurrent: old ] whileFalse.

 A lost CAS (whileFalse: sees false) re-reads `anAtom value` — the updated
 graph — and retries from there."

m1 := Meter new asActor.
m2 := Meter new asActor.
m3 := Meter new asActor.
m4 := Meter new asActor.

"Fire the four meters concurrently — each send returns a Future."
f1 := m1 report: 1 times: 200 into: total.
f2 := m2 report: 1 times: 200 into: total.
f3 := m3 report: 1 times: 200 into: total.
f4 := m4 report: 1 times: 200 into: total.

"Wait for all four to finish, then read the final total."
f1 wait. f2 wait. f3 wait. f4 wait.
total value printNl.
