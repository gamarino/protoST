"EXPECT: high=5"
"-- 05_priority_bands.st ----------------------------------------------------"
"-- Actor priority bands. `asActor` creates a medium-priority actor;        --"
"-- `asHighPriorityActor` and `asLowPriorityActor` create actors that the    --"
"-- scheduler drains before / after medium respectively. Three actors are    --"
"-- created and each receives 5 sends in low-then-medium-then-high enqueue   --"
"-- order. With strict-priority draining, the high-priority actor's value    --"
"-- is visible first when we wait on it.                                     --"
"-- The single-method invariant is unchanged; priority only affects WHICH    --"
"-- actor a worker picks next, never the within-actor ordering.              --"
"-- Expected output: high=5                                                  --"

Object subclass: #Counter instanceVariableNames: 'value'.
Counter >> initialize
  value := 0.
  ^ self.
Counter >> add: n
  value := value + n.
  ^ value.
Counter >> value
  ^ value.

high := Counter new initialize asHighPriorityActor.
med  := Counter new initialize asActor.
low  := Counter new initialize asLowPriorityActor.

"Enqueue in low-then-medium-then-high order — without priority bands the    "
"low actor would finish first.                                              "
1 to: 5 do: [:i | low add: 1].
1 to: 5 do: [:i | med add: 1].
1 to: 5 do: [:i | high add: 1].

('high=', (high value) wait printString) printNl.
