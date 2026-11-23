"EXPECT: #(true true 'v' 'v' 'v' 'v')"
"Spec §6.3, conformance 64: nil never replaces a value. Assigning nil to an
 unset write-once variable answers nil and leaves it unset (a later write
 installs its value); assigning nil to a set one answers the current value
 and signals nothing."
Object subclass: #Holder instanceVariableNames: '' writeOnceNames: 'key'.
Holder >> setKey: x
  ^ key := x
Holder >> key
  ^ key

| o unsetAnswer stillUnset nilAnswer |
o := Holder new.
unsetAnswer := o setKey: nil.
stillUnset := o key isNil.
o setKey: 'v'.
nilAnswer := [o setKey: nil] on: Error do: [:e | 'signalled: ' , e messageText].
{ unsetAnswer isNil. stillUnset. o key. nilAnswer. o setKey: nil. o key }
