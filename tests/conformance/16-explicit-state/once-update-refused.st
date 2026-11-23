"EXPECT: #('update:with: cannot write the write-once variable ''oid'' of a Node; assign it with :=' 'compareAndSet:from:to: cannot write the write-once variable ''oid'' of a Node; assign it with :=' 'compareAndSet:from:to: cannot write the write-once variable ''oid'' of a Node; assign it with :=' nil 1)"
"Spec §6.5, M5, conformance 66: update:with: never writes a write-once
 variable, and compareAndSet: from anything but nil is refused, whether the
 variable is unset or set. The update block never runs; oid keeps 1."
Object subclass: #Node instanceVariableNames: '' writeOnceNames: 'oid'.
Node >> oid
  ^ oid

| o ran r1 r2 r3 |
o := Node new.
r2 := [o compareAndSet: #oid from: 5 to: 6] on: ModificationForbidden do: [:e | e messageText].
o compareAndSet: #oid from: nil to: 1.
r1 := [o update: #oid with: [:old | ran := true. old + 1]]
  on: ModificationForbidden do: [:e | e messageText].
r3 := [o compareAndSet: #oid from: 1 to: 2] on: ModificationForbidden do: [:e | e messageText].
{r1. r2. r3. ran. o oid}
