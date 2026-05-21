"XFAIL: EXPECT: 7"
"§10.3: the spec lists `resolve:` as a Future selector — 'resolve the future with
a value (settles waiters/callbacks)' — so a directly-constructed Future that is
resolved should make wait yield that value. NEW — undocumented discrepancy (not
in §14): `Future new` + `resolve: 7` + `wait` fails with 'Object is not an
integer type'. See report."
f := Future new.
f resolve: 7.
f wait.
