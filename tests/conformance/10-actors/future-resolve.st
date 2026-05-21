"EXPECT: 7"
"§10.3: `resolve:` is a Future selector — a directly-constructed Future
(`Future new`) is a first-class, manually-resolvable promise: resolving it
makes `wait` yield that value."
f := Future new.
f resolve: 7.
f wait.
