"XFAIL: EXPECT: true"
"§7 / §9.2: the language reference's own examples send `isEven` to an integer
(e.g. `aCollection do: [ :x | x isEven ifTrue: [ ^ x ] ]` in §7, and
`select: [ :e | e isEven ]` in §9.2). NEW — undocumented discrepancy (not in
§14): `isEven` is not bound on SmallInteger — it is a doesNotUnderstand. See
report."
4 isEven.
